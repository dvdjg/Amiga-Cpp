#pragma once

/// \file point.hpp
/// **Punto 2D genérico de navegación** (`eng::ai::NavPoint<S>`): mismos campos `x`/`y` que
/// `eng::Point2s`, pero sobre el escalar `S` (`s16`/`s32`/`float`…), de modo que los algoritmos de
/// navegación (`navmesh_lite`, `waypoints`) mantienen las lecturas `p.x`/`p.y` y no fijan el tipo.
///
/// Ver la regla de genericidad: `docs/engine/architecture/CODING_STYLE.md` y `AGENTS.md` §1.10.

#include <eng/core/types/types.hpp>

namespace eng::ai {

template <class S>
struct NavPoint {
	S x {};
	S y {};
};

} // namespace eng::ai
