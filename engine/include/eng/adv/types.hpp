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

/// Lista de **argumentos de valor** de una primitiva del original (parámetros de
/// `startScript`, clases de `setClass`, rango de `setVarRange`, `soundKludge`…).
/// El traductor (I1) la materializa con `w.list(...)` dentro de la llamada, de modo que el
/// guion generado no hace asignaciones dinámicas: la lista vive en la pila del guion.
/// `N = 0` (p. ej. `cutscene([])`) reserva un elemento para no depender de arrays de
/// longitud cero (extensión no estándar): el contrato es `N`, no `sizeof`.
template <eng::u32 N>
struct arg_list {
	eng::s32 v[N == 0 ? 1 : N];
};

/// Sub-operaciones de `ActorOps` (SCUMM V5). Nombres = vocabulario de descumm
/// (`../SCUMM/scummvm-tools-2.9.0/engines/scumm/descumm-common.cpp`), semántica en
/// `../SCUMM/scummvm-2026.4.0/engines/scumm/script_v5.cpp` (`o5_actorOps`, :65/:145/:225).
/// El guion generado emite **una llamada por sub-operación** (`w.actor_op(a, code, args…)`).
enum class ActorOp : eng::u8 {
	init,          ///< `Init()`: resetea el actor a su estado por defecto.
	costume,       ///< `Costume(id)`: cambia el traje.
	palette,       ///< `Palette(n)`: ciclo de paleta del traje.
	talk_color,    ///< `TalkColor(n)`: color del texto de diálogo.
	talk_anim_nr,  ///< `TalkAnimNr(n)`: animaciones de habla (boca).
	ignore_boxes,  ///< `IgnoreBoxes()`: ignora walkboxes al andar.
	scale,         ///< `Scale(a, b)`: escala forzada del actor.
	elevation,     ///< `Elevation(n)`: altura sobre el suelo.
	never_z_clip,  ///< `NeverZClip()`: nunca recortar en Z.
	walk_speed,    ///< `WalkSpeed(n)`: velocidad de caminado.
	set_z_clip,    ///< `SetZClip(n)`: altura de recorte en Z.
	follow_boxes,  ///< `FollowBoxes()`: vuelve a respetar walkboxes.
	name,          ///< `Name("…")`: nombre mostrado en diálogos.
	walk_anim_nr,  ///< `WalkAnimNr(n)`: animación de caminado.
	sound,         ///< `Sound(n)`: sonido de pasos.
	width,         ///< `Width(n)`: ancho de colisión.
	stand_anim_nr, ///< `StandAnimNr(n)`: animación en reposo.
	init_anim_nr,  ///< `InitAnimNr(n)`: animación inicial.
	anim_speed,    ///< `AnimSpeed(n)`: velocidad de animación.
};

/// Sub-operaciones de `VerbOps` (SCUMM V5); nombres de descumm, semántica en
/// `o5_verbOps` (`../SCUMM/scummvm-2026.4.0/engines/scumm/script_v5.cpp`).
enum class VerbOp : eng::u8 {
	set_xy,        ///< `SetXY(x, y)`: posición del verbo en pantalla.
	key,           ///< `Key(k)`: atajo de teclado.
	text,          ///< `Text("…")`: etiqueta del verbo.
	on,            ///< `On()`: verbo visible/seleccionable.
	sound,         ///< `sound(n)`: sonido asociado al verbo.
	new_verb,      ///< `New()`: crea/reinicia el verbo.
	off,           ///< `Off()`: verbo oculto.
	color,         ///< `Color(n)`: color del texto.
	hi_color,      ///< `HiColor(n)`: color resaltado.
	center,        ///< `Center()`: centra el texto.
	unknown8,      ///< `unknown8()`: sub-op sin semántica conocida (se conserva tal cual).
	set_to_object, ///< `SetToObject(obj)`: verbo ligado a objeto.
	newline,       ///< `newline()`: salto de línea en la etiqueta.
	back_color,    ///< `BackColor(n)`: color de fondo.
	dim,           ///< `Dim()`: verbo atenuado (no disponible).
	dim_color,     ///< `DimColor(n)`: color atenuado.
	set_to_string, ///< `SetToString(str)`: etiqueta desde recurso de cadena.
	x88,           ///< `x88()`: sub-op sin semántica conocida (se conserva tal cual).
	delete_verb,   ///< `Delete()`: elimina el verbo.
	start_anim,    ///< `startAnim(n)`: animación del icono del verbo.
};

} // namespace eng::adv
