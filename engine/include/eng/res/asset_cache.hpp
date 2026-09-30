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
#include <eng/memory/arena.hpp>

namespace eng::res {

using AssetId = eng::u16; ///< 0 = inválido

/// Handle no propietario. La generación invalida copias después de evict/reload.
struct AssetHandle {
	AssetId id = 0u;
	eng::u16 generation = 0u;

	[[nodiscard]] constexpr bool valid() const noexcept { return id != 0u && generation != 0u; }
};

/// Vista no propietaria con identidad verificable contra `AssetCache::valid`.
struct AssetView {
	eng::Span<const eng::u8> data {};
	AssetHandle handle {};
	eng::MemoryKind kind = eng::MemoryKind::Any;

	[[nodiscard]] constexpr bool empty() const noexcept { return data.empty(); }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return data.size(); }
};

/// Estado de un asset.
enum class AssetState : eng::u8 { Empty = 0, Loading, Ready, Error };

/// La política de caché usa la misma clasificación de memoria que los bloques y el allocator.
using MemBank = eng::MemoryKind;

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
	eng::MemoryBlock block {};   ///< reserva y banco efectivo; las vistas no son propietarias
	eng::u32 reserved_size = 0u;
	eng::u16 generation = 0u;    ///< sube al reservar/liberar y descarta vistas antiguas
	eng::u16 dma_users = 0u;     ///< leases DMA explícitos; bloquean evict y shutdown
};

/// Presupuesto por banco.
struct CacheConfig {
	eng::u32 chip_budget = 0;
	eng::u32 fast_budget = 0;
	eng::u16 max_assets = 16u;
	eng::u32 slow_budget = 0u;
};

/// **Caché de assets**. `Backend` debe ofrecer:
	///   `eng::MemoryBlock alloc(eng::u32 bytes, eng::MemoryKind bank);`
	///   `void free(const eng::MemoryBlock& block);`
///   `bool load(AssetId id, const char* path, eng::Span<eng::u8> dst);` (arranca la lectura async)
template <class Backend, eng::u16 MaxAssets = 16u>
class AssetCache {
public:
	bool init(Backend& backend, const CacheConfig& cfg) noexcept {
		if (!shutdown()) return false;
		m_backend = backend;
		m_cfg = cfg;
		m_count = 0u;
		m_used_chip = 0u;
		m_used_fast = 0u;
		m_used_slow = 0u;
		m_frame = 0u;
		for (AssetSlot& s : m_slots) {
			reset_slot(s);
		}
		return true;
	}

	/// Libera todos los slots y reinicia la contabilidad. Falla si una lectura asíncrona o lease DMA
	/// sigue activa; el llamador debe completar/cancelar E/S y cerrar leases antes del teardown.
	[[nodiscard]] bool shutdown() noexcept {
		for (eng::u16 i = 0u; i < MaxAssets; ++i) {
			if (m_slots[i].dma_users != 0u || m_slots[i].state == AssetState::Loading) return false;
		}
		for (eng::u16 i = 0u; i < MaxAssets; ++i) {
			free_slot(m_slots[i]);
			reset_slot(m_slots[i]);
		}
		m_count = 0u;
		m_used_chip = 0u;
		m_used_fast = 0u;
		m_used_slow = 0u;
		return true;
	}

	/// Registra un asset (path + tamaño). Devuelve su id (1..N) o 0 si no cabe.
	AssetId declare(const char* path, eng::u32 size, MemBank bank = MemBank::Any,
			eng::u8 prio = 128u) noexcept {
		if (m_count >= MaxAssets || m_count >= m_cfg.max_assets) {
			return 0u;
		}
		AssetSlot& s = m_slots[m_count];
		reset_slot(s);
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
			return raw_view(s);
		}
		if (s.state == AssetState::Empty || s.state == AssetState::Error) {
			(void)prefetch(id);
		}
		return {};
	}

	/// Devuelve una vista con generación. No inicia cargas y queda invalidada por `evict`, `shutdown`
	/// o una nueva reserva del mismo slot.
	[[nodiscard]] AssetView view(AssetId id) const noexcept {
		if (!valid(id)) return {};
		const AssetSlot& s = m_slots[id - 1u];
		if (s.state != AssetState::Ready || !s.block.valid()) return {};
		// `MemoryBlock` es el resultado crudo del allocator; esta conversión marca su uso como bytes.
		return AssetView {eng::Span<eng::u8> {static_cast<eng::u8*>(s.block.data), s.size}.as_const(),
			AssetHandle {id, s.generation}, s.block.kind};
	}

	/// Comprueba que una vista todavía refiere al slot y a la reserva que la creó.
	[[nodiscard]] bool valid(AssetView v) const noexcept {
		if (!v.handle.valid() || !valid(v.handle.id)) return false;
		const AssetSlot& s = m_slots[v.handle.id - 1u];
		return s.state == AssetState::Ready && s.generation == v.handle.generation &&
			s.block.data == v.data.data() && s.size == v.data.size() && s.block.kind == v.kind;
	}

	/// Adquiere una lease de ejecución DMA para una vista publicada. Mientras exista, la caché
	/// impide desalojar el slot y `shutdown()` falla sin liberar memoria.
	[[nodiscard]] bool acquire_dma(AssetHandle handle) noexcept {
		if (!handle.valid() || !valid(handle.id)) return false;
		AssetSlot& s = m_slots[handle.id - 1u];
		if (s.state != AssetState::Ready || s.generation != handle.generation ||
		    s.block.kind != eng::MemoryKind::Chip || s.dma_users == 0xffffu) return false;
		++s.dma_users;
		return true;
	}

	/// Cierra una lease DMA al confirmar que el hardware dejó de leer el bloque.
	[[nodiscard]] bool release_dma(AssetHandle handle) noexcept {
		if (!handle.valid() || handle.id > m_count) return false;
		AssetSlot& s = m_slots[handle.id - 1u];
		if (s.state != AssetState::Ready || s.generation != handle.generation || s.dma_users == 0u) return false;
		--s.dma_users;
		return true;
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
	[[nodiscard]] eng::u32 used_slow() const noexcept { return m_used_slow; }

private:
	[[nodiscard]] bool valid(AssetId id) const noexcept { return id >= 1u && id <= m_count; }

	/// Contador de bytes usados por banco efectivo.
	[[nodiscard]] eng::u32& used(MemBank b) noexcept {
		if (b == MemBank::Chip) return m_used_chip;
		if (b == MemBank::Slow) return m_used_slow;
		return m_used_fast;
	}
	/// Presupuesto de bytes del banco efectivo.
	[[nodiscard]] eng::u32 budget(MemBank b) const noexcept {
		if (b == MemBank::Chip) return m_cfg.chip_budget;
		if (b == MemBank::Slow) return m_cfg.slow_budget;
		return m_cfg.fast_budget;
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
			if (s.state != AssetState::Ready || s.pinned || s.refcount > 0u || s.dma_users > 0u) {
				continue;
			}
			if (bank != MemBank::Any && s.block.kind != bank) {
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
		if (s.state != AssetState::Ready || s.dma_users != 0u || s.refcount != 0u || s.pinned) {
			return;
		}
		free_slot(s);
		s.state = AssetState::Empty;
	}

	/// Libera el bloque del slot y descuenta del banco.
	void free_slot(AssetSlot& s) noexcept {
		if (s.block.valid()) {
			m_backend.get()->free(s.block);
			used(s.block.kind) -= s.reserved_size;
			s.block = {};
			s.reserved_size = 0u;
		}
		s.generation = next_generation(s.generation);
	}

	/// Reserva espacio (desalojando si hace falta) y arranca la carga del asset.
	bool start_load(AssetId id) noexcept {
		AssetSlot& s = m_slots[id - 1u];
		const bool automatic_bank = s.bank == MemBank::Any;
		MemBank requested_bank = s.bank;
		if (automatic_bank) {
			requested_bank = ensure_space(s.size, MemBank::Fast, id) ? MemBank::Fast : MemBank::Slow;
		}
		if (!ensure_space(s.size, requested_bank, id)) {
			s.state = AssetState::Error;
			return false;
		}
		s.block = m_backend.get()->alloc(s.size, requested_bank);
		if (!s.block.valid() && automatic_bank && requested_bank == MemBank::Fast) {
			requested_bank = MemBank::Slow;
			if (ensure_space(s.size, requested_bank, id)) {
				s.block = m_backend.get()->alloc(s.size, requested_bank);
			}
		}
		if (!s.block.valid()) {
			s.state = AssetState::Error;
			return false;
		}
		MemBank effective_bank = MemBank::Any;
		if (s.block.kind == eng::MemoryKind::Chip) effective_bank = MemBank::Chip;
		else if (s.block.kind == eng::MemoryKind::Fast) effective_bank = MemBank::Fast;
		else if (s.block.kind == eng::MemoryKind::Slow) effective_bank = MemBank::Slow;
		else {
			m_backend.get()->free(s.block);
			s.block = {};
			s.state = AssetState::Error;
			return false;
		}
		if (!ensure_space(s.block.size, effective_bank, id)) {
			m_backend.get()->free(s.block);
			s.block = {};
			s.generation = next_generation(s.generation);
			s.state = AssetState::Error;
			return false;
		}
		s.reserved_size = s.block.size;
		s.generation = next_generation(s.generation);
		used(effective_bank) += s.reserved_size;
		s.state = AssetState::Loading;
		if (!m_backend.get()->load(id, s.path, raw_view(s))) {
			free_slot(s);
			s.state = AssetState::Error;
			return false;
		}
		return true;
	}

	[[nodiscard]] static eng::u16 next_generation(eng::u16 generation) noexcept {
		++generation;
		return generation == 0u ? 1u : generation;
	}

	static void reset_slot(AssetSlot& s) noexcept {
		const eng::u16 generation = next_generation(s.generation);
		s.path = nullptr;
		s.state = AssetState::Empty;
		s.bank = MemBank::Any;
		s.priority = 128u;
		s.pinned = false;
		s.refcount = 0u;
		s.last_use = 0u;
		s.size = 0u;
		s.block = {};
		s.reserved_size = 0u;
		s.generation = generation;
		s.dma_users = 0u;
	}

	[[nodiscard]] static eng::Span<eng::u8> raw_view(AssetSlot& s) noexcept {
		// La lectura del backend escribe bytes en el rango devuelto por el allocator.
		return {static_cast<eng::u8*>(s.block.data), s.size};
	}
	eng::Ref<Backend> m_backend {};
	CacheConfig m_cfg {};
	AssetSlot m_slots[MaxAssets] {};
	eng::u16 m_count = 0u;
	eng::u32 m_used_chip = 0u;
	eng::u32 m_used_fast = 0u;
	eng::u32 m_used_slow = 0u;
	eng::u32 m_frame = 0u;
};

} // namespace eng::res
