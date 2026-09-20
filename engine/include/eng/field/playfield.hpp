#pragma once

/// \file playfield.hpp
/// Cabecera de familia de playfields: reúne la base (`Playfield`,
/// `PlayfieldHardwareView`, `RasterPolicy`, en `playfield_base.hpp`) y las dos
/// implementaciones (`CanvasPlayfield` interleaved y `ContiguousPlayfield` contiguo).
/// Los consumidores pueden seguir incluyendo este header sin cambios; quien solo
/// necesite la base puede incluir `playfield_base.hpp` directamente.
///
/// ```text
///   Playfield (base: framebuffer + mapeo lógico→físico + primitivas CPU)   ← NO dibuja
///     ├─ CanvasPlayfield      (interleaved: HUD, fondo estático, capa de actores)
///     └─ ContiguousPlayfield  (planos uno tras otro: escenas EHB/HAM)
///            ▲ m_target (Ref, no propietario)
///   Surface (origen + tamaño + clip)   ← ÚNICO contexto de dibujo
///            ▲
///   DrawTarget (Surface + Rasterizer + FramePlan + clip)
/// ```

#include <eng/field/canvas_playfield.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/playfield_base.hpp>
