#pragma once

/// \file memory_kind.hpp
/// Clasificacion logica de la memoria desde el punto de vista del engine.
///
/// Vive en `core/` (y no en `memory/`) porque forma parte de la identidad de una
/// reserva: `eng::Block<Tag>` lleva su `MemoryKind` junto al dominio, de modo que
/// una lista de Copper sabe a la vez que **es** una copperlist y **de que** memoria
/// procede. Separar dominio (que es el dato) de medio (donde vive) permite, por
/// ejemplo, construir una lista de Copper en Fast RAM y copiarla a Chip con el
/// Blitter antes de instalarla.

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

} // namespace eng
