#pragma once

/// \file asset_cache.hpp
/// **Caché de assets** (`eng::res`): slots con estado, presupuesto por banco (Chip/Fast),
/// prioridad, `refcount`, `pin` y **desalojo LRU**. La app pide por `AssetId`; si no cabe, los
/// assets no fijados, no referenciados y de menor prioridad **salen solos**. Ver
/// `docs/engine/architecture/RESOURCE_SYSTEM.md` §1.
///
/// Es **puro** respecto a la E/S y la memoria: recibe un `Backend` con `alloc`/`free`/`load`
/// (el de Amiga usa `MemorySystem` + `os::file_read_async`; el de host, un arena falsa). El
/// llamador completa la carga con `on_load_done`.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::res {

using AssetId = eng::u16; ///< 0 = inválido

/// Estado de un asset.
enum class AssetState : eng::u8 { Empty = 0, Loading, Ready, Error };

/// Banco de memoria.
enum class MemBank : eng::u8 { Any = 0, Chip, Fast };

/// Un slot de asset.
struct AssetSlot {
	const char* path = nullptr;
	AssetState state = AssetState::Empty;
	MemBank bank = MemBank::Any;
	eng::u8 priority = 128; ///< 255 = casi nunca se desaloja
	bool pinned = false;
	eng::u16 refcount = 0;
	eng::u32 last_use = 0;      ///< frame stamp
	eng::u32 size = 0;
	eng::Span<eng::u8> data {}; ///< vista del bloque reservado (no propietaria)
};

/// Presupuesto por banco.
struct CacheConfig {
	eng::u32 chip_budget = 0;
	eng::u32 fast_budget = 0;
	eng::u16 max_assets = 16u;
};

/// **Caché de assets**. `Backend` debe ofrecer:
///   `eng::Span<eng::u8> alloc(eng::u32 bytes, MemBank bank);`
///   `void free(eng::Span<eng::u8> block, MemBank bank);`
///   `bool load(AssetId id, const char* path, eng::Span<eng::u8> dst);` (arranca la lectura async)
template <class Backend, eng::u16 MaxAssets = 16u>
class AssetCache {
public:
	bool init(Backend& backend, const CacheConfig& cfg) noexcept {
		m_backend = backend;
		m_cfg = cfg;
		m_count = 0u;
		m_used_chip = 0u;
		m_used_fast = 0u;
		m_frame = 0u;
		for (AssetSlot& s : m_slots) {
			s = AssetSlot {};
		}
		return true;
	}

	/// Registra un asset (path + tamaño). Devuelve su id (1..N) o 0 si no cabe.
	AssetId declare(const char* path, eng::u32 size, MemBank bank = MemBank::Any,
			eng::u8 prio = 128u) noexcept {
		if (m_count >= MaxAssets || m_count >= m_cfg.max_assets) {
			return 0u;
		}
		AssetSlot& s = m_slots[m_count];
		s = AssetSlot {};
		s.path = path;
		s.size = size;
		s.bank = bank;
		s.priority = prio;
		++m_count;
		return static_cast<AssetId>(m_count);
	}

	/// `Ready` → datos; si no, lanza la carga y devuelve vacío (la app dibuja un placeholder).
	eng::Span<eng::u8> get(AssetId id) noexcept {
		if (!valid(id)) {
			return {};
		}
		AssetSlot& s = m_slots[id - 1u];
		if (s.state == AssetState::Ready) {
			s.last_use = m_frame;
			return s.data;
		}
		if (s.state == AssetState::Empty || s.state == AssetState::Error) {
			(void)prefetch(id);
		}
		return {};
	}

	/// Lanza la carga si no está ya cargando/lista.
	bool prefetch(AssetId id) noexcept {
		if (!valid(id)) {
			return false;
		}
		AssetSlot& s = m_slots[id - 1u];
		if (s.state == AssetState::Ready || s.state == AssetState::Loading) {
			return true;
		}
		return start_load(id);
	}

	/// Fija/desfija el asset: un asset fijado **no** se desaloja.
	void pin(AssetId id, bool on) noexcept {
		if (valid(id)) {
			m_slots[id - 1u].pinned = on;
		}
	}
	/// Ajusta la prioridad de desalojo (mayor = más difícil de desalojar).
	void set_priority(AssetId id, eng::u8 prio) noexcept {
		if (valid(id)) {
			m_slots[id - 1u].priority = prio;
		}
	}
	/// Suma una referencia (protege del desalojo) y marca uso.
	void add_ref(AssetId id) noexcept {
		if (valid(id)) {
			++m_slots[id - 1u].refcount;
			m_slots[id - 1u].last_use = m_frame;
		}
	}
	/// Suelta una referencia.
	void release(AssetId id) noexcept {
		if (valid(id) && m_slots[id - 1u].refcount > 0u) {
			--m_slots[id - 1u].refcount;
		}
	}

	/// Completa una carga (`result` = bytes o <0). El llamador lo invoca al recibir `FileDone`.
	void on_load_done(AssetId id, eng::s32 result) noexcept {
		if (!valid(id)) {
			return;
		}
		AssetSlot& s = m_slots[id - 1u];
		if (s.state != AssetState::Loading) {
			return;
		}
		if (result < 0 || static_cast<eng::u32>(result) < s.size) {
			free_slot(s);
			s.state = AssetState::Error;
			return;
		}
		s.state = AssetState::Ready;
		s.last_use = m_frame;
	}

	void set_frame(eng::u32 frame) noexcept { m_frame = frame; }

	/// Estado del asset (`Empty` si el id no es válido).
	[[nodiscard]] AssetState state(AssetId id) const noexcept {
		return valid(id) ? m_slots[id - 1u].state : AssetState::Empty;
	}
	[[nodiscard]] eng::u16 count() const noexcept { return m_count; }
	[[nodiscard]] eng::u32 used_chip() const noexcept { return m_used_chip; }
	[[nodiscard]] eng::u32 used_fast() const noexcept { return m_used_fast; }

private:
	[[nodiscard]] bool valid(AssetId id) const noexcept { return id >= 1u && id <= m_count; }

	/// Contador de bytes usados del banco (`Chip` o `Fast`).
	[[nodiscard]] eng::u32& used(MemBank b) noexcept {
		return (b == MemBank::Chip) ? m_used_chip : m_used_fast;
	}
	/// Presupuesto de bytes del banco (`Chip` o `Fast`).
	[[nodiscard]] eng::u32 budget(MemBank b) const noexcept {
		return (b == MemBank::Chip) ? m_cfg.chip_budget : m_cfg.fast_budget;
	}

	/// Desaloja víctimas hasta que quepan `bytes`. `false` si no hay víctima válida.
	bool ensure_space(eng::u32 bytes, MemBank bank, AssetId except) noexcept {
		if (bytes > budget(bank)) {
			return false;
		}
		while (used(bank) + bytes > budget(bank)) {
			const AssetId v = pick_victim(bank);
			if (v == 0u || v == except) {
				return false;
			}
			evict(v);
		}
		return true;
	}

	/// Menor prioridad y, a igualdad, el más viejo (LRU). Solo `Ready`, no fijados, `refcount==0`.
	[[nodiscard]] AssetId pick_victim(MemBank bank) const noexcept {
		AssetId best = 0u;
		eng::u8 best_prio = 0xffu;
		eng::u32 best_use = 0xffffffffu;
		for (eng::u16 i = 0u; i < m_count; ++i) {
			const AssetSlot& s = m_slots[i];
			if (s.state != AssetState::Ready || s.pinned || s.refcount > 0u) {
				continue;
			}
			if (bank != MemBank::Any && s.bank != bank) {
				continue;
			}
			if (s.priority < best_prio ||
			    (s.priority == best_prio && s.last_use < best_use)) {
				best = static_cast<AssetId>(i + 1u);
				best_prio = s.priority;
				best_use = s.last_use;
			}
		}
		return best;
	}

	/// Desaloja un asset `Ready`: libera su bloque y lo deja `Empty`.
	void evict(AssetId id) noexcept {
		AssetSlot& s = m_slots[id - 1u];
		if (s.state != AssetState::Ready) {
			return;
		}
		free_slot(s);
		s.state = AssetState::Empty;
	}

	/// Libera el bloque del slot y descuenta del banco.
	void free_slot(AssetSlot& s) noexcept {
		if (!s.data.empty()) {
			m_backend.get()->free(s.data, s.bank);
			used(s.bank) -= s.size;
			s.data = {};
		}
	}

	/// Reserva espacio (desalojando si hace falta) y arranca la carga del asset.
	bool start_load(AssetId id) noexcept {
		AssetSlot& s = m_slots[id - 1u];
		const MemBank bank = (s.bank == MemBank::Any) ? MemBank::Fast : s.bank;
		if (!ensure_space(s.size, bank, id)) {
			s.state = AssetState::Error;
			return false;
		}
		s.bank = bank;
		s.data = m_backend.get()->alloc(s.size, bank);
		if (s.data.empty()) {
			s.state = AssetState::Error;
			return false;
		}
		used(bank) += s.size;
		s.state = AssetState::Loading;
		if (!m_backend.get()->load(id, s.path, s.data)) {
			free_slot(s);
			s.state = AssetState::Error;
			return false;
		}
		return true;
	}

	eng::Ref<Backend> m_backend {};
	CacheConfig m_cfg {};
	AssetSlot m_slots[MaxAssets] {};
	eng::u16 m_count = 0u;
	eng::u32 m_used_chip = 0u;
	eng::u32 m_used_fast = 0u;
	eng::u32 m_frame = 0u;
};

} // namespace eng::res
