#pragma once

/// \file actor.hpp
/// Cabecera de familia del sistema de actores: reúne los tipos (`Actor`, `ActorDesc`,
/// `TransparencyPlan`…), el almacén generacional (`ActorStore`) y la composición de
/// sprites hardware. Los consumidores pueden seguir incluyendo este header sin cambios.

#include <eng/scene/actor_sprite.hpp>
#include <eng/scene/actor_store.hpp>
#include <eng/scene/actor_types.hpp>
