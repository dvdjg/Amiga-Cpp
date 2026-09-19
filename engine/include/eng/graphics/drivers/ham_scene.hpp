#pragma once

/// \file ham_scene.hpp
/// **Shim de compatibilidad**: el driver se renombro a `PlanarScene`
/// (`eng/graphics/drivers/planar_scene.hpp`) porque su alcance es planar generico (HAM
/// y planos normales), no el modo HAM. Este fichero solo reexporta el nuevo nombre;
/// incluye `<eng/graphics/drivers/planar_scene.hpp>` en codigo nuevo.

#include <eng/graphics/drivers/planar_scene.hpp>
