#pragma once

/// \file playfield.hpp
/// Cabecera de familia de playfields: reúne la base (`Playfield`,
/// `PlayfieldHardwareView`, `RasterPolicy`, en `playfield_base.hpp`) y las dos
/// implementaciones (`CanvasPlayfield` interleaved y `ContiguousPlayfield` contiguo).
/// Los consumidores pueden seguir incluyendo este header sin cambios; quien solo
/// necesite la base puede incluir `playfield_base.hpp` directamente.

#include <eng/field/canvas_playfield.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/playfield_base.hpp>
