#pragma once

/// \file block_pool.hpp
/// **Pool de bloques con `free`** (`eng::BlockPool`): asignador *first-fit* sin heap sobre un
/// buffer dado, con fusión de huecos. Es el asignador **persistente** del engine: reserva y
/// **libera** recursos individualmente (gráficos, sonido, …) en cualquier orden — a diferencia de
/// la arena *bump* (`LinearArena`), que es LIFO y solo sirve para scratch. **No** está atado a un
/// tipo de memoria: opera sobre cualquier buffer y lleva su `MemoryKind` (Chip/Fast/Slow/Any).
///
/// ```text
///   buffer del llamador (Chip/Fast/Slow)         BlockPool
///   ───────────────────────────────────         ──────────────────────────
///   [ base (alineada) ······················· ]  allocate(size,align) -> hueco first-fit
///                                               free(ptr)            -> marca libre + fusiona
///   fragmentación visible: huecos libres = free_bytes()
/// ```
///
/// La **base se alinea una sola vez** al crear el pool (el "peyote" de `AllocMem` de 1.3, que
/// garantiza 8 y no 16): así el padding de alineación **no se acumula** por reserva y una reserva
/// que cabe siempre cabe (ver `LinearArena::allocate` para el fallo que esto evita).
///
/// El pool es **genérico** (cualquier medio y cualquier consumidor): el medio es un dato.

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/memory/arena.hpp>

namespace eng {

/// Asignador de bloques **first-fit con `free`** y fusión de huecos, sobre un buffer del llamador
/// (sin heap). `kMaxSlots` es el número máximo de huecos/segmentos que puede distinguir: cada
/// reserva que no llena un hueco exacto lo parte en dos (reservado + resto). Con `kMaxSlots`
/// agotado, una reserva que no encaje en un hueco existente **falla** (no corrompe): súbelo si el
/// juego fragmenta mucho en runtime (el setup no es caliente).
template <u16 kMaxSlots = 64u>
class BlockPoolT {
	static_assert(kMaxSlots >= 2u, "BlockPoolT: al menos 2 huecos");

public:
	static constexpr u16 kMaxBlocks = kMaxSlots;

	constexpr BlockPoolT() = default;
	/// Construye el pool sobre `base`, `size` bytes, con medio `kind` y alineación por defecto
	/// `align`. La base se **alinea a `align`** (el trozo de cabecera se descarta): así ninguna
	/// reserva posterior paga padding acumulativo.
	constexpr BlockPoolT(void* base, u32 size, MemoryKind kind = MemoryKind::Any,
			     u32 align = 16u) noexcept {
		m_kind = kind;
		m_align = align != 0u ? align : 1u;
		reset(base, size);
	}
	/// Construye el pool **delegando en una `LinearArena` existente** (mismo buffer y mismo
	/// cursor): una arena y un banco que comparten buffer no se solapan. **`free` pasa a no-op**
	/// (la arena es *bump*), así que este modo es solo para *scratch*: un recurso persistente se
	/// crea con un pool sobre buffer propio, no con backing. Ver `MEMORY_OWNERSHIP.md`.
	constexpr BlockPoolT(LinearArena& arena, MemoryKind kind) noexcept {
		configure_backing(arena);
		m_kind = kind;
	}

	/// **Alinha la base al pool** y lo re-siembra. `base`/`size` los entrega el backend. Se puede
	/// reasociar a otro buffer (p. ej. tras otra reserva de `AllocMem`).
	constexpr void reset(void* base, u32 size) noexcept {
		m_base_raw = static_cast<u8*>(base);
		m_size_raw = size;
		m_count = 0u;
		if (base == nullptr || size == 0u) {
			m_base = nullptr;
			m_size = 0u;
			return;
		}
		const uintptr raw = reinterpret_cast<uintptr>(base);
		const uintptr aligned = (raw + m_align - 1u) & ~(static_cast<uintptr>(m_align) - 1u);
		const u32 drop = static_cast<u32>(aligned - raw);
		if (drop >= size) {
			m_base = nullptr;
			m_size = 0u;
			return;
		}
		m_base = reinterpret_cast<u8*>(aligned);
		m_size = size - drop;
		m_blocks[0] = Slot {0u, m_size, 0u, 0u};
		m_count = 1u;
	}

	/// Enlaza el pool a una **`LinearArena` existente**: desde aquí `allocate` **delega en la
	/// arena** (mismo cursor). `free` pasa a **no-op** (la arena es *bump*): este modo es solo
	/// para *scratch*, no para recursos persistentes. Ver `configure_backing`.
	void configure_backing(LinearArena& arena) noexcept {
		m_backing = eng::Ref<LinearArena> {arena};
		m_base = nullptr;
		m_base_raw = nullptr;
		m_size = 0u;
		m_size_raw = 0u;
		m_count = 0u;
	}

	/// Reserva `bytes` alineados a `alignment` (0 = la alineación por defecto del pool).
	/// `MemoryBlock` inválido si no cabe.
	MemoryBlock allocate(u32 bytes, u32 alignment = 0u) noexcept {
		if (m_backing.valid()) {
			return m_backing.get()->allocate(bytes, alignment != 0u ? alignment : m_align);
		}
		if (bytes == 0u || m_base == nullptr) {
			return {};
		}
		const u32 a = alignment != 0u ? alignment : m_align;
		// First-fit **con alineación del inicio**: el hueco empieza en `m_base + off`; el inicio
		// alineado dentro del hueco puede dejar un hueco-cola pequeño delante. Como la base ya
		// está alineada a la alineación por defecto, el caso común (a <= m_align) no paga nada.
		for (u16 i = 0u; i < m_count; ++i) {
			if (m_blocks[i].state != 0u) {
				continue;
			}
			const uintptr raw = reinterpret_cast<uintptr>(m_base + m_blocks[i].offset);
			const uintptr aligned = (raw + a - 1u) & ~(static_cast<uintptr>(a) - 1u);
			const u32 pad = static_cast<u32>(aligned - raw);
			const u32 need = align_up(bytes, a);
			if (pad + need > m_blocks[i].size) {
				continue;
			}
			const u32 used = pad + need;
			// Parte el hueco: [pad libre][reservado][resto libre].
			if (used < m_blocks[i].size && m_count + (pad != 0u ? 1u : 0u) <= kMaxSlots) {
				split(i, pad, used, need);
			} else {
				m_blocks[i].state = 1u;
				m_blocks[i].used = need;
			}
			m_used += need; // sólo los bytes útiles del bloque (sin el padding del hueco)
			if (m_used > m_peak) {
				m_peak = m_used;
			}
			return MemoryBlock {reinterpret_cast<void*>(aligned), need, m_kind};
		}
		return {};
	}

	/// Reserva tipada (como `LinearArena::allocate_block`): `Block<Tag>` con el medio del pool.
	template <class Tag>
	[[nodiscard]] Block<Tag> allocate_block(u32 bytes, u32 alignment = 0u) noexcept {
		const MemoryBlock mb = allocate(bytes, alignment);
		return Block<Tag> {mb.buffer<Tag>(), mb.kind};
	}

	/// Libera un bloque de `allocate` (y fusiona huecos contiguos). **Idempotente**: liberar un
	/// puntero que no pertenece a un bloque **en uso** (doble `free`, puntero ajeno o ya liberado)
	/// es un no-op; así un `release` repetido no corrompe las listas. Con respaldo de arena
	/// (bump) es no-op por diseño.
	void free(void* ptr) noexcept {
		if (m_backing.valid()) {
			return; // la arena es *bump*: no recicla (modo scratch)
		}
		if (ptr == nullptr || m_base == nullptr) {
			return;
		}
		const u8* p = static_cast<const u8*>(ptr);
		if (p < m_base || p >= m_base + m_size) {
			return; // puntero ajeno: no-op (no corrompe el pool)
		}
		const u32 off = static_cast<u32>(p - m_base);
		for (u16 i = 0u; i < m_count; ++i) {
			if (m_blocks[i].offset == off && m_blocks[i].state == 1u) {
				m_blocks[i].state = 0u;
				m_used = m_used >= m_blocks[i].used ? m_used - m_blocks[i].used : 0u;
				m_blocks[i].used = 0u;
				coalesce();
				return;
			}
		}
	}

	/// Reinicia un pool con almacenamiento propio (no respaldado por arena) para hacer rollback de
	/// una inicialización fallida. No libera el bloque raíz que entregó el backend.
	void reset() noexcept {
		if (m_backing.valid()) return;
		m_base = nullptr;
		m_size = 0u;
		m_size_raw = 0u;
		m_kind = MemoryKind::Any;
		m_align = 2u;
		m_used = 0u;
		m_peak = 0u;
		m_count = 0u;
	}

	/// Bytes libres (suma de huecos libres).
	[[nodiscard]] u32 free_bytes() const noexcept {
		if (m_backing.valid()) {
			return m_backing.get()->remaining();
		}
		u32 t = 0u;
		for (u16 i = 0u; i < m_count; ++i) {
			if (m_blocks[i].state == 0u) {
				t += m_blocks[i].size;
			}
		}
		return t;
	}
	[[nodiscard]] u32 capacity() const noexcept {
		return m_backing.valid() ? m_backing.get()->capacity() : m_size;
	}
	/// Bytes **útiles** reservados (suma de los bloques vivos; sin padding de huecos).
	[[nodiscard]] u32 used_bytes() const noexcept { return m_used; }
	/// **Pico** de `used_bytes` desde el arranque (máximo histórico): para presupuesto.
	[[nodiscard]] u32 peak_bytes() const noexcept { return m_peak; }
	[[nodiscard]] MemoryKind kind() const noexcept {
		return m_backing.valid() ? m_backing.get()->kind() : m_kind;
	}
	[[nodiscard]] u16 block_count() const noexcept { return m_count; }
	/// Huecos/segmentos que el pool puede seguir distinguiendo. Si llega a 0, la próxima reserva
	/// que no encaje exacta fallará: señal de fragmentación excesiva (sube `kMaxSlots`).
	[[nodiscard]] u16 slots_left() const noexcept { return static_cast<u16>(kMaxSlots - m_count); }

private:
	struct Slot {
		u32 offset = 0u;
		u32 size = 0u;
		u32 used = 0u; ///< bytes útiles reservados (para el contador `m_used`); 0 si libre
		u8 state = 0u; ///< 0 = libre, 1 = usado
	};

	/// Redondea `bytes` al alineamiento dado (potencia de dos; 0 = 1).
	[[nodiscard]] static constexpr u32 align_up(u32 bytes, u32 alignment) noexcept {
		const u32 a = (alignment != 0u) ? alignment : 1u;
		return (bytes + a - 1u) & ~(a - 1u);
	}

	/// Parte el hueco `i` en `[pad libre][reservado used][resto libre]`, insertando slots según
	/// haga falta. `used` = pad + tamaño alineado del bloque; `need` = bytes útiles (sin padding).
	void split(u16 i, u32 pad, u32 used, u32 need) noexcept {
		Slot& h = m_blocks[i];
		const u32 rest = h.size - used;
		if (pad != 0u) {
			// [pad libre][reservado][resto]: inserta dos slots tras el hueco-cola.
			insert(i + 1u, Slot {h.offset + pad, used - pad, need, 1u});
			h.size = pad;
			h.state = 0u;
			if (rest != 0u) {
				insert(i + 2u, Slot {h.offset + used, rest, 0u, 0u});
			}
		} else {
			// [reservado][resto]: el propio hueco pasa a reservado y se inserta el resto.
			const u32 off = h.offset;
			h.offset = off;
			h.size = used;
			h.used = need;
			h.state = 1u;
			if (rest != 0u) {
				insert(i + 1u, Slot {off + used, rest, 0u, 0u});
			}
		}
	}

	/// Inserta un slot en la posición `at` (desplaza el resto).
	void insert(u16 at, Slot s) noexcept {
		if (m_count >= kMaxSlots) {
			return;
		}
		for (u16 j = m_count; j > at; --j) {
			m_blocks[j] = m_blocks[j - 1u];
		}
		m_blocks[at] = s;
		++m_count;
	}

	/// Fusiona huecos libres contiguos (tras `free`).
	void coalesce() noexcept {
		for (u16 i = 0u; i + 1u < m_count;) {
			if (m_blocks[i].state == 0u && m_blocks[i + 1u].state == 0u &&
			    m_blocks[i].offset + m_blocks[i].size == m_blocks[i + 1u].offset) {
				m_blocks[i].size += m_blocks[i + 1u].size;
				for (u16 j = static_cast<u16>(i + 1u); j + 1u < m_count; ++j) {
					m_blocks[j] = m_blocks[j + 1u];
				}
				--m_count;
			} else {
				++i;
			}
		}
	}

	u8* m_base = nullptr; ///< base **alineada** (la que usan las reservas)
	u8* m_base_raw = nullptr;
	u32 m_size = 0u;
	u32 m_size_raw = 0u;
	MemoryKind m_kind = MemoryKind::Any;
	u32 m_align = 16u;
	eng::Ref<LinearArena> m_backing {}; ///< si es válido, `allocate` delega en esta arena (cursor único)
	Slot m_blocks[kMaxSlots] {};
	u16 m_count = 0u;
	u32 m_used = 0u; ///< bytes útiles vivos
	u32 m_peak = 0u; ///< pico de `m_used`
};

/// Alias por defecto (64 huecos): el tipo que usan `MemBank` y los consumidores.
using BlockPool = BlockPoolT<64u>;

} // namespace eng
