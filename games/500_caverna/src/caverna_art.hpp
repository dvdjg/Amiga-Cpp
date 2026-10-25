#pragma once

/// \file caverna_art.hpp
/// **Arte procedural de 500 Caverna** separado de la lógica de juego: tablas de
/// tiles de la cueva y del terreno, mapa jugable y hojas de los actores. `main.cpp`
/// consume este módulo a través de sus vistas y callbacks; no conoce las tablas ni
/// los generadores de píxeles.
///
/// El arte es un **placeholder determinista** de la vertical slice (V5 sustituye
/// estas tablas por assets cargados por manifiesto). Se construye una vez en setup:
/// convertir píxel a planos en el bucle de juego costaría ciclos de CPU que el
/// presupuesto de 50 fps no admite.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace caverna_art {

/// Lado de un tile en píxeles; el pipeline de tiles del engine usa 16.
inline constexpr eng::u16 kTile = 16u;
/// Ancho del mapa en tiles; potencia de dos para indexar con desplazamiento.
inline constexpr eng::u16 kMapWidth = 256u;
/// Alto del mapa en tiles.
inline constexpr eng::u16 kMapHeight = 20u;
/// Planos de cada playfield (DPF 3+3).
inline constexpr eng::u8 kPlanes = 3u;

/// Construye las tablas de tiles, el mapa y las hojas de actores. Idempotente; se
/// llama una vez en `init`.
void build();

/// Fila de un tile de la cueva (PF1) ya convertida a planos.
/// \param glyph    índice de glifo 0..15 (se enmascara).
/// \param variant  variante de pintado 0..3 (se enmascara).
/// \param row      fila dentro del tile 0..15.
/// \param plane    plano 0..kPlanes-1.
/// \return palabra de bits de esa fila y plano.
[[nodiscard]] eng::u16 cave_row(eng::u8 glyph, eng::u8 variant, eng::u8 row,
				eng::u8 plane);

/// Fila de un tile del terreno (PF2) ya convertida a planos. Mismo contrato que
/// `cave_row`; el índice 0 es transparente y deja ver la cueva.
[[nodiscard]] eng::u16 terrain_row(eng::u8 glyph, eng::u8 variant, eng::u8 row,
				   eng::u8 plane);

/// Celdas del terreno jugable (PF2), en orden fila-mayor, `kMapWidth` por fila.
[[nodiscard]] eng::Span<const eng::u16> map_cells();

/// Celdas de la cueva de fondo (PF1), en orden fila-mayor.
[[nodiscard]] eng::Span<const eng::u16> background_cells();

/// Paleta de 32 colores del display (PF1, PF2 y sprites).
[[nodiscard]] eng::Span<const eng::u16> palette();

/// Copia la pose `phase` del tipo de actor `type` en el bloque DATA de un slot.
/// \param words  bloque DATA de sprites (32 words por slot: DAT/DATB por línea).
/// \param slot   slot del actor dentro del bloque.
/// \param type   0 = jugador; 1..3 = enemigos.
/// \param phase  0/1 (pose de animación).
void write_actor_art(eng::Span<eng::u16> words, eng::u8 slot, eng::u8 type,
		     eng::u8 phase);

} // namespace caverna_art
