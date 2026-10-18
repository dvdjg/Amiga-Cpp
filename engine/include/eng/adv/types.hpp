#pragma once

/// \file types.hpp
/// **Tipos base del contenido de aventura** (`eng::adv`): identificadores y centinelas que
/// comparten el contenido convertido (pipeline de importación, ver
/// `docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md`) y el runtime de la capa de aventura
/// (`docs/engine/architecture/ADVENTURE_LAYER.md`). JSON del SIM v2 → tablas del engine:
/// los `id` de sala/hotspot son de 16 bits (holgura sobre los 86–127 de MI1/MI2) y los
/// valores inválidos son centinelas explícitos, nunca `-1` mágicos.

#include <eng/core/types/types.hpp>

namespace eng::adv {

/// Identificador de sala (1..kMaxRooms; 0 reservado).
using RoomId = eng::u16;
/// Identificador de hotspot = id de objeto del juego original (1..65534).
using HotspotId = eng::u16;

/// Sala inválida (no existe contenido cargado).
inline constexpr RoomId kInvalidRoom = 0xffffu;
/// Hotspot inválido (hit-test sin resultado).
inline constexpr HotspotId kInvalidHotspot = 0xffffu;
/// Índice de walkbox inválido (punto fuera de toda caja).
inline constexpr eng::u16 kWalkboxInvalid = 0xffffu;

/// Flags del byte `flags` de una walkbox SCUMM V5 (`engines/scumm/boxes.h:39/41`).
/// El contenido convertido los conserva tal cual: el runtime decide qué honrar.
namespace walkbox_flags {

/// La caja solo la usan actores controlados por el jugador (`kBoxPlayerOnly`).
inline constexpr eng::u8 kPlayerOnly = 0x20u;
/// La caja es invisible para el pathfinding (`kBoxInvisible`).
inline constexpr eng::u8 kInvisible = 0x80u;

} // namespace walkbox_flags

} // namespace eng::adv
