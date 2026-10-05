#pragma once

/// \file arena.hpp
/// Modelo de memoria inicial del engine.
///
/// Esta unidad introduce la pieza mas importante para no escribir "C con clases":
/// una abstraccion pequena, explicita y portable para gestionar memoria por areas.
///
/// En Amiga 500 no podemos tratar toda la RAM igual:
/// - Chip RAM: visible por el chipset. Necesaria para bitplanes, sprites, audio,
///   copperlists y buffers usados por el blitter.
/// - Slow RAM: expansion trapdoor/bogo. Aumenta capacidad, pero no es Fast RAM real.
/// - Fast RAM: memoria privada de CPU en maquinas con aceleradora. El A500 objetivo
///   inicial no la tiene.
///
/// La clase `LinearArena` no pide memoria al sistema. Solo administra un bloque que
/// otro backend le entrega. Esto permite dos estrategias futuras:
/// - OS-friendly: reservar con Exec/AllocMem.
/// - Close-to-the-metal: tomar el sistema y construir arenas sobre rangos fisicos.
///
/// ```text
///   backend (entrega el bloque)        LinearArena (NO pide memoria)          consumidor
///   ──────────────────────────         ────────────────────────────           ──────────
///   Exec/AllocMem | rango físico ───►  [ pool base · cursor · límites ]
///   (MemoryKind: Chip/Slow/Fast/Any)          │
///                                      allocate(size, alineación) ──► MemoryBlock {data,size,kind}
///                                      reset() / mark·release ──────► cursor atrás (sin free por bloque)
/// ```

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>

namespace eng {

/// Resultado de una reserva dentro de una arena.
///
/// Un bloque invalido (`data == nullptr`) indica fallo. No hay excepciones ni heap
/// implicito; quien llama debe comprobar `valid()`.
struct MemoryBlock {
	void* data = nullptr;
	u32 size = 0;
	MemoryKind kind = MemoryKind::Any;

	constexpr bool valid() const {
		return data != nullptr && size != 0;
	}

	/// Vista tipada mutable del bloque (dominio `Tag`): evita `static_cast` y
	/// conecta directamente con APIs que piden `Bytes<Tag>`.
	template <class Tag>
	[[nodiscard]] constexpr Bytes<Tag> buffer() const noexcept {
		return Bytes<Tag> { static_cast<u8*>(data), size };
	}
	/// Vista tipada de solo lectura del bloque.
	template <class Tag>
	[[nodiscard]] constexpr ByteView<Tag> view() const noexcept {
		return ByteView<Tag> { static_cast<const u8*>(data), size };
	}
	/// Bloque tipado (mutable) del dominio, con el `MemoryKind` de la reserva.
	template <class Tag>
	[[nodiscard]] constexpr Block<Tag> block() const noexcept {
		return Block<Tag> { buffer<Tag>(), kind };
	}
};

/// Foto inmutable de una arena.
///
/// Se usa para overlays, logs y futuros informes de profiler sin exponer punteros
/// internos mutables.
struct ArenaSnapshot {
	u32 base = 0;
	u32 capacity = 0;
	u32 used = 0;
	u32 peak = 0;
	u32 remaining = 0;
	MemoryKind kind = MemoryKind::Any;
};

/// **Marcador de la arena de scratch** (para `mark`/`release`): el cursor antes de un tramo.
/// Copia trivial; guardarlo es gratis (se puede anidar).
struct ArenaMark {
	u32 used = 0u;
	u32 peak = 0u;
};

/// Arena lineal de bump allocation.
///
/// Tutorial mental:
/// 1. El backend entrega un bloque base + tamano.
/// 2. Cada `allocate` devuelve el siguiente trozo alineado.
/// 3. No hay liberacion individual.
/// 4. `clear()` reinicia el offset completo.
///
/// Esto es ideal para recursos de escena, frame scratch y buffers cocinados, porque
/// evita fragmentacion y hace visible el coste de memoria.
class LinearArena {
public:
	constexpr LinearArena() = default;

	constexpr LinearArena(void* base, u32 size, MemoryKind kind)
		: m_base(static_cast<u8*>(base)), m_size(size), m_kind(kind) {}

	/// Reasocia la arena a otro bloque.
	///
	/// No libera la memoria anterior. El propietario real es el backend o el sistema
	/// de memoria superior.
	void reset(void* base, u32 size, MemoryKind kind) {
		m_base = static_cast<u8*>(base);
		m_size = size;
		m_kind = kind;
		m_used = 0;
		m_peak = 0;
		m_overflow = false;
	}

	/// Reinicia la arena sin tocar el contenido.
	///
	/// Para `FrameScratch` esto se llamara normalmente una vez por frame.
	void clear() {
		m_used = 0;
		m_overflow = false;
	}

	/// Reserva bytes alineados dentro de la arena.
	///
	/// Si la arena no tiene espacio, devuelve un `MemoryBlock` invalido. La alineacion
	/// debe ser potencia de dos; en Amiga normalmente usaremos 2, 4, 16 o 64 segun el
	/// recurso.
	///
	/// \warning PEYOTE DE ALINEACION (bug demo 201, septiembre 2026):
	/// AllocMem de AmigaOS 1.3 solo garantiza 8 bytes de alineacion, no 16. Si la
	/// base de la arena no esta alineada a `alignment`, se genera padding interno
	/// que consume espacio real del bloque. Cuando se hacen varias asignaciones
	/// alineadas a 16 dentro de la misma arena, el padding acumulado puede superar
	/// el tamano total declarado y la ultima asignacion falla silenciosamente
	/// (devuelve bloque invalido).
	///
	/// Ejemplo real (6 planos EHB + copperlist en chip):
	///   need = 6*10240 + 4096 = 65536
	///   1a allocate(61440, 16): padding = 8, used = 61448
	///   2a allocate(4096, 16):  next = 61448 + 4096 = 65544 > 65536  => FALLA
	///
	/// Regla: cuando se pida memoria alineada a 16 dentro de una arena cuyo base
	/// puede no estar alineada a 16 (como AllocMem de AmigaOS 1.3), incluir
	/// +16 bytes de headroom en el tamano total pedido al backend.
	///
	/// Si overflow, el bloque devuelto es invalido y `overflow_detected()` sera true.
	MemoryBlock allocate(u32 bytes, u32 alignment = 2) {
		if (alignment == 0) {
			alignment = 1;
		}

		const uintptr raw = reinterpret_cast<uintptr>(m_base) + m_used;
		const uintptr aligned = align_up_ptr(raw, alignment);
		const u32 padding = static_cast<u32>(aligned - raw);
		const u32 next = m_used + padding + bytes;

		if (!m_base || next > m_size) {
			m_overflow = (m_base != nullptr && bytes != 0);
			return {};
		}

		m_used = next;
		if (m_used > m_peak) {
			m_peak = m_used;
		}

		return {reinterpret_cast<void*>(aligned), bytes, m_kind};
	}

	/// Reserva tipada: devuelve el bloque ya como `Block<Tag>` (vista del dominio y
	/// `MemoryKind` de la arena), de modo que el consumidor no necesite casts.
	template <class Tag>
	[[nodiscard]] Block<Tag> allocate_block(u32 bytes, u32 alignment = 2) {
		const MemoryBlock mb = allocate(bytes, alignment);
		return Block<Tag> { mb.buffer<Tag>(), mb.kind };
	}

	constexpr u32 capacity() const { return m_size; }
	constexpr u32 used() const { return m_used; }
	constexpr u32 peak() const { return m_peak; }
	constexpr u32 remaining() const { return m_size - m_used; }
	constexpr MemoryKind kind() const { return m_kind; }
	/// Dirección base de la arena como `Address<Any>` (el banco es un dato, `kind()`): evita
	/// exponer un `void*` crudo en la API.
	constexpr Address<MemoryKind::Any> base() const {
		return Address<MemoryKind::Any>::from_storage(m_base);
	}

	/// True si la ultima allocate() fallo por falta de espacio (overflow).
	/// Util para diagnosticar bug de alineacion sin examinar cada puntero.
	constexpr bool overflow_detected() const { return m_overflow; }

protected:
	/// Cursor actual (bytes usados). Para subclases que necesitan `mark`/`release` (scratch).
	[[nodiscard]] constexpr u32 used_cursor() const noexcept { return m_used; }
	/// Retrocede el cursor a `m` sin tocar el `peak` (usado por `ScratchArena::release`).
	constexpr void rewind_to(u32 used) noexcept {
		if (used <= m_used) {
			m_used = used;
			m_overflow = false;
		}
	}

public:
	constexpr ArenaSnapshot snapshot() const {
		return {
			static_cast<u32>(reinterpret_cast<uintptr>(m_base)),
			m_size,
			m_used,
			m_peak,
			remaining(),
			m_kind,
		};
	}

private:
	u8* m_base = nullptr;
	u32 m_size = 0;
	u32 m_used = 0;
	u32 m_peak = 0;
	bool m_overflow = false;
	MemoryKind m_kind = MemoryKind::Any;
};

/// Arena de **Chip RAM**: como `LinearArena`, pero `allocate_block<Tag>()` devuelve
/// `Block<Tag, MemoryKind::Chip>` — el medio va en el **tipo** (DMA), sin comprobación en runtime.
/// Hereda de `LinearArena` para que `res::load`/`glyph_cache`/`ArenaAlloc` (que toman `LinearArena&`)
/// sigan funcionando sin saber del medio.
struct ChipArena : LinearArena {
	constexpr ChipArena() = default;
	using LinearArena::LinearArena;
	using LinearArena::reset;

	template <class Tag>
	/// Reserva tipada en Chip: `Block<Tag, Chip>` (lo que va a DMA sin comprobación runtime).
	[[nodiscard]] Block<Tag, MemoryKind::Chip> allocate_block(u32 bytes, u32 alignment = 2) {
		return Block<Tag, MemoryKind::Chip> {LinearArena::allocate_block<Tag>(bytes, alignment).view,
						     MemoryKind::Chip};
	}
};

/// **Arena de scratch (LIFO)**: arena *bump* con **marcadores** (`mark`/`release(mark)`) para
/// liberar un tramo completo en orden inverso al de reserva. Es el patrón correcto para la
/// memoria **temporal** de una fase/frame (listas de trabajo, staging, buffers que se reinician
/// enteros), **no** para recursos persistentes: `mark/release` es **LIFO**, no libera huecos.
///
/// Dos vidas útiles de la memoria del engine (ver `MEMORY_OWNERSHIP.md`):
/// - **Persistente** (assets, escena): `BlockPool` (free en cualquier orden).
/// - **Scratch de fase/frame**: `ScratchArena` (bump + `mark/release`).
///
/// ```cpp
/// eng::ScratchArena scratch {base, bytes, eng::MemoryKind::Fast};
/// const eng::ArenaMark m = scratch.mark();       // antes del frame
/// auto tmp = scratch.allocate(512u, 4u);         // temporales del frame
/// scratch.release(m);                            // al terminar el frame (LIFO)
/// ```
struct ScratchArena : LinearArena {
	constexpr ScratchArena() = default;
	using LinearArena::LinearArena;
	using LinearArena::reset;
	using LinearArena::allocate;
	using LinearArena::allocate_block;

	/// Construye la scratch sobre un buffer (como `LinearArena`).
	constexpr ScratchArena(void* base, u32 size, MemoryKind kind) : LinearArena(base, size, kind) {}

	/// Toma un **marcador** del cursor actual. Anidable: guarda uno por cada nivel.
	[[nodiscard]] constexpr ArenaMark mark() const noexcept {
		return ArenaMark {used_cursor(), 0u};
	}

	/// Libera **todo** lo reservado desde `m` (LIFO). El `peak` se conserva (telemetría de uso).
	constexpr void release(ArenaMark m) noexcept { rewind_to(m.used); }
};

/// Tres arenas base que todo backend debe intentar ofrecer. `chip`/`slow` son **persistentes**
/// (`ChipArena`/`LinearArena`); `frame` es **scratch** de fase/frame (`ScratchArena`, LIFO). El
/// backend puede respaldar `chip`/`slow` con `BlockPool` (persistente) y usar `frame` para
/// temporales que se reinician con `reset_frame()`.
struct MemorySystem {
	ChipArena chip;
	LinearArena slow;
	ScratchArena frame;

	/// **Reinicia la scratch de frame**: invalida lo reservado en `frame` (LIFO total). Llamar al
	/// empezar/terminar cada frame; **no** toca los recursos persistentes (`chip`/`slow`).
	void reset_frame() { frame.clear(); }
};

/// Peticion de memoria para inicializar un backend o una demo.
///
/// Esta configuracion no pretende representar toda la RAM del Amiga. Solo dice:
/// "quiero que el backend reserve estos bloques para que el engine los administre".
struct MemoryConfig {
	u32 chip_bytes = 0;
	u32 slow_bytes = 0;
	u32 frame_bytes = 0;
	u32 fast_bytes = 0; ///< Fast RAM (solo CPU); 0 = no reservar (o no hay)
	/// Si `true`, el backend reserva **toda** la memoria disponible de cada banco (ignora los
	/// `*_bytes`): caso de **máquina desnuda**, donde la app no quiere dimensionar a mano.
	bool all = false;
};

/// Resultado de la configuracion de memoria.
struct MemoryReport {
	ArenaSnapshot chip {};
	ArenaSnapshot slow {};
	ArenaSnapshot frame {};
	bool chip_ok = false;
	bool slow_ok = false;
	bool frame_ok = false;
	bool fast_ok = false; ///< Fast RAM reservada (opcional; `false` = no había o no se pidió)

	constexpr bool ok() const {
		return chip_ok && slow_ok && frame_ok;
	}
};

} // namespace eng
