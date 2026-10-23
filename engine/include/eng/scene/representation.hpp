#pragma once

/// \file representation.hpp
/// Vocabulario de **representaci?n** de un actor (sprite HW / BOB / playfield / CPU).
/// La decisi?n efectiva la toma el compositor con `SpriteAllocator` + `SpriteLoadReport`;
/// ya no hay un asignador grueso que consuma presupuesto en el alta.

#include <eng/core/types/types.hpp>

namespace eng::scene {

/// C?mo se materializa un actor.
enum class Representation : eng::u8 {
	Cpu = 0,     ///< dibujado por CPU (marcadores, casos peque?os)
	Sprite = 1,  ///< sprite hardware
	Bob = 2,     ///< BOB por Blitter
	Layer = 3,   ///< capa/playfield propio (objeto grande con scroll; patr?n Jim Power)
};

} // namespace eng::scene
