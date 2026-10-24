#pragma once

/// \file representation.hpp
/// Representaci?n de **una parte compuesta** (sprite HW o BOB). El actor simple ya
/// no declara preferencia: la composici?n decide con `SpriteAllocator` y
/// `SpriteLoadReport`. Este enum se conserva solo para el planner de partes.

#include <eng/core/types/types.hpp>

namespace eng::scene {

/// C?mo se materializa una parte compuesta.
enum class Representation : eng::u8 {
	Sprite = 0, ///< sprite hardware.
	Bob = 1,    ///< BOB por Blitter.
};

} // namespace eng::scene
