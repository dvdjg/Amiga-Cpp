#pragma once

/// \file caverna_gameplay.hpp
/// **Lógica pura de Caverna**: colisión de caja contra tilemap, cámara con
/// dead-zone y clasificación del contacto jugador/enemigo. Vive fuera de
/// `main.cpp` para poder validarla en host sin Amiga ni sprites.

#include <eng/core/types/box.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace caverna {

/// Lado del tile y de la caja del jugador (px).
inline constexpr eng::u16 kTile = 16u;
inline constexpr eng::u16 kPlayerSize = 16u;
/// Rango de ids de tile sólidos (0/1 son decoración; 2..13 terreno/plataformas).
inline constexpr eng::u16 kSolidFirst = 2u;
inline constexpr eng::u16 kSolidLast = 13u;

/// `true` si la celda es sólida. Fuera del mapa se considera sólido (borde).
/// \param cells  celdas fila-mayor del tilemap.
/// \param width  ancho del mapa en tiles.
/// \param height alto del mapa en tiles.
/// \param tx,ty  celda consultada.
[[nodiscard]] inline bool tile_is_solid(eng::Span<const eng::u16> cells, eng::u16 width,
					eng::u16 height, eng::s32 tx, eng::s32 ty) noexcept {
	if (tx < 0 || ty < 0 || tx >= width || ty >= height) return true;
	const eng::u16 tile =
		cells[static_cast<eng::u32>(ty) * width + static_cast<eng::u16>(tx)];
	return tile >= kSolidFirst && tile <= kSolidLast;
}

/// Colisión de una caja contra las celdas sólidas. Se resuelve con `>> 4`
/// (tile 16 px) y sin división en el bucle de juego.
/// \param cells  celdas del tilemap.
/// \param width,height  dimensiones del mapa en tiles.
/// \param x,y,w,h  caja en coordenadas de mundo (px).
[[nodiscard]] inline bool box_hits_solid(eng::Span<const eng::u16> cells, eng::u16 width,
					 eng::u16 height, eng::s32 x, eng::s32 y, eng::s16 w,
					 eng::s16 h) noexcept {
	if (x < 0 || y < 0) return true;
	const eng::s32 tx0 = x >> 4;
	const eng::s32 ty0 = y >> 4;
	const eng::s32 tx1 = (x + w - 1) >> 4;
	const eng::s32 ty1 = (y + h - 1) >> 4;
	for (eng::s32 ty = ty0; ty <= ty1; ++ty) {
		for (eng::s32 tx = tx0; tx <= tx1; ++tx) {
			if (tile_is_solid(cells, width, height, tx, ty)) return true;
		}
	}
	return false;
}

/// Cámara horizontal con **dead-zone**: el jugador puede moverse dentro de la
/// banda `[dead_left, dead_right]` sin que la cámara se mueva; al salir, la
/// cámara le sigue manteniendo el borde de la banda. Se acota al mundo.
/// \param player_x  posición del jugador en mundo (px).
/// \param cam_x     cámara actual (px).
/// \param world_w   ancho del mundo (px).
/// \param viewport_w ancho visible (px).
/// \param dead_left,dead_right  banda de pantalla sin scroll (px).
/// \return nueva cámara.
[[nodiscard]] inline eng::s32 dead_zone_camera(eng::s32 player_x, eng::s32 cam_x,
					       eng::s32 world_w, eng::s32 viewport_w,
					       eng::s32 dead_left, eng::s32 dead_right) noexcept {
	const eng::s32 sx = player_x - cam_x;
	if (sx < dead_left) cam_x = player_x - dead_left;
	else if (sx > dead_right) cam_x = player_x - dead_right;
	const eng::s32 max_x = world_w - viewport_w;
	if (cam_x < 0) cam_x = 0;
	if (max_x < 0) return 0;
	if (cam_x > max_x) cam_x = max_x;
	return cam_x;
}

/// Resultado del contacto jugador/enemigo.
enum class EnemyHit : eng::u8 {
	None,   ///< no se tocan
	Stomp,  ///< el jugador cae sobre el enemigo: muerte/bote
	Damage, ///< contacto lateral: daño al jugador
};

/// Clasifica el contacto. `Stomp` exige que el jugador baje (`vy > 0`) y que su
/// borde inferior esté en la mitad superior del enemigo; el resto es `Damage`.
/// \param player  caja del jugador (mundo).
/// \param player_vy velocidad vertical actual (positiva = bajando).
/// \param enemy   caja del enemigo (mundo).
[[nodiscard]] inline EnemyHit classify_enemy_hit(const eng::Box& player, eng::s16 player_vy,
						  const eng::Box& enemy) noexcept {
	if (!eng::overlaps(player, enemy)) return EnemyHit::None;
	const eng::s16 player_bottom = static_cast<eng::s16>(player.y + player.h);
	const eng::s16 enemy_mid = static_cast<eng::s16>(enemy.y + enemy.h / 2);
	return (player_vy > 0 && player_bottom <= enemy_mid) ? EnemyHit::Stomp
							     : EnemyHit::Damage;
}

} // namespace caverna
