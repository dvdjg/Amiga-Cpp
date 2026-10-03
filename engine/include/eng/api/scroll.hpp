#pragma once

/// \file scroll.hpp
/// **Fachada de scroll de juego**: el vocabulario con el que un juego pide y conduce el scroll de
/// una capa, sin conocer registros, geometrías ni buffers.
///
/// El scroll se separa en **dos capas**, y esta cabecera las fija:
///
/// - **Vocabulario de juego** (lo que el dev escribe): `eng::ScrollSpec` —la técnica pedida, el
///   período del mapa si es toroidal y la velocidad máxima— y `eng::Camera2D` —la ventana al mundo
///   (`move_by`/`scroll_x`/`scroll_y`). Son datos **sin hardware**: valen igual para EHB, single o DPF.
/// - **Motores de scroll** (lo que el engine construye): `playfield::StripScrollLayer` (camino de
///   tiras, 50 fps single) y `playfield::XlimitedScene` (corcóscru XYLimited). El juego los
///   **registra** con `App::add_scroll_layer` —que los arranca con su memoria y su backend y los
///   **conduce por frame**— y los alimenta con su cámara; **no ve** el compositor, los `BPLxPT`, los
///   buffers ni el tipo de backend.
///
/// El camino limpio para una capa de fondo con scroll (una capa por llamada):
///
/// ```cpp
/// // --- setup (Game::init) ---
/// m_layer.set_tilemap(tilemap);              // asset: banco + mapa (ids de tile) + paleta
/// m_layer.set_sizes(ring_bytes, column_bytes);
/// m_layer.track_camera(&m_cam_x, &m_cam_y);  // la cámara (px de mundo) conduce la capa
/// app.add_scroll_layer(m_layer);             // App la arranca y la conduce por frame
///
/// // --- por frame (Game::update) ---
/// m_cam_x += vx;                             // el vocabulario de juego: mover la vista
/// m_cam_y += vy;
/// ```
///
/// `Camera2D` es la cámara de un mundo **acotado** (recorta a sus límites). Una capa **toroidal**
/// (el atlas se envuelve) usa una posición en px **sin recortar**: el motor de tiras envuelve por su
/// cuenta. Elegir la representación de cámara de cada tipo de mapa es la línea que cierra
/// `ROADMAP_GAME_API.md` §7 (planner de cámara/tilemap): hasta entonces el juego elige el motor que
/// ya conoce y declara su cámara con el tipo que le corresponde.

#include <eng/field/scroll_route.hpp>
#include <eng/field/strip_layer.hpp>
#include <eng/field/strip_scroller.hpp>
#include <eng/field/tilemap_view.hpp>
#include <eng/scene/virtual_scene.hpp>
#include <eng/scene/world.hpp>

namespace eng {

/// **Vocabulario de scroll** de una capa, re-exportado al espacio de nombres de juego para que el
/// desarrollo se escriba sin prefijos internos (`scene::`).
using scene::Camera2D;
/// **Técnica de scroll** pedida por una capa (`None`/`Fine`/`BlitterColumns`/`CopperRing`/
/// `CopperSplit`/`Strip`); el planner la acepta, degrada o rechaza según su coste.
using scene::ScrollKind;
/// **Especificación de scroll**: técnica + período del mapa toroidal (en `words`) + velocidad.
using scene::ScrollSpec;

} // namespace eng
