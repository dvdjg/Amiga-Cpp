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

#include <eng/core/types.hpp>

namespace eng {

/// Tipo logico de memoria desde el punto de vista del engine.
///
/// No describe necesariamente el flag exacto de Exec. Por ejemplo, la Slow RAM del
/// A500 puede aparecer como MEMF_FAST para AmigaOS, pero el engine la etiqueta como
/// `Slow` porque sigue sin ser Fast RAM CPU-privada.
enum class MemoryKind : u8 {
	Chip,
	Slow,
	Fast,
	Any,
};

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
	}

	/// Reinicia la arena sin tocar el contenido.
	///
	/// Para `FrameScratch` esto se llamara normalmente una vez por frame.
	void clear() {
		m_used = 0;
	}

	/// Reserva bytes alineados dentro de la arena.
	///
	/// Si la arena no tiene espacio, devuelve un `MemoryBlock` invalido. La alineacion
	/// debe ser potencia de dos; en Amiga normalmente usaremos 2, 4, 16 o 64 segun el
	/// recurso.
	MemoryBlock allocate(u32 bytes, u32 alignment = 2) {
		if (alignment == 0) {
			alignment = 1;
		}

		const uintptr raw = reinterpret_cast<uintptr>(m_base) + m_used;
		const uintptr aligned = align_up_ptr(raw, alignment);
		const u32 padding = static_cast<u32>(aligned - raw);
		const u32 next = m_used + padding + bytes;

		if (!m_base || next > m_size) {
			return {};
		}

		m_used = next;
		if (m_used > m_peak) {
			m_peak = m_used;
		}

		return {reinterpret_cast<void*>(aligned), bytes, m_kind};
	}

	/// Reserva un array de objetos triviales.
	///
	/// De momento no llama constructores. Esta funcion esta pensada para PODs,
	/// tablas, comandos y estructuras de runtime controladas.
	template <typename T>
	T* allocate_array(u16 count, u32 alignment = alignof(T)) {
		MemoryBlock block = allocate(sizeof(T) * static_cast<u32>(count), alignment);
		return static_cast<T*>(block.data);
	}

	constexpr u32 capacity() const { return m_size; }
	constexpr u32 used() const { return m_used; }
	constexpr u32 peak() const { return m_peak; }
	constexpr u32 remaining() const { return m_size - m_used; }
	constexpr MemoryKind kind() const { return m_kind; }
	constexpr void* base() const { return m_base; }

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
	MemoryKind m_kind = MemoryKind::Any;
};

/// Tres arenas base que todo backend debe intentar ofrecer.
struct MemorySystem {
	LinearArena chip;
	LinearArena slow;
	LinearArena frame;
};

/// Peticion de memoria para inicializar un backend o una demo.
///
/// Esta configuracion no pretende representar toda la RAM del Amiga. Solo dice:
/// "quiero que el backend reserve estos bloques para que el engine los administre".
struct MemoryConfig {
	u32 chip_bytes = 0;
	u32 slow_bytes = 0;
	u32 frame_bytes = 0;
};

/// Resultado de la configuracion de memoria.
struct MemoryReport {
	ArenaSnapshot chip {};
	ArenaSnapshot slow {};
	ArenaSnapshot frame {};
	bool chip_ok = false;
	bool slow_ok = false;
	bool frame_ok = false;

	constexpr bool ok() const {
		return chip_ok && slow_ok && frame_ok;
	}
};

} // namespace eng
