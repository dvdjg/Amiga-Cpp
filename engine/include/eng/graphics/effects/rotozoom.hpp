#pragma once

/// \file rotozoom.hpp
/// **Rotozoom por píxel** (efecto de "chunky" clásico) hacia un buffer de índices.
///
/// Rota y escala una **textura indexada** (`IndexedTexture`, 1 byte por texel, nibble
/// bajo = índice de color) y escribe un `ChunkyBuffer` de `w`×`h` índices, que el
/// llamador convierte a planar con `c2p_1x1_4` (o lo consume cualquier otra vía).
///
/// Geometría: para cada píxel de pantalla `(x,y)` se calcula
///
///     r = R(θ) · (x - c)                (c = centro de la pantalla)
///     (u,v) = offset + r · zoom          (en texeles, punto fijo 16.16)
///     dst[y][x] = textura[u mod TW][v mod TH]
///
/// con `R(θ)` una matriz de rotación en punto fijo 16.16 generada en compile-time
/// (`SineTable`). El bucle caliente es **incremental**: una vez por frame se calculan
/// los pasos por píxel (`du/dv`) y por fila (`eu/ev`) y el punto de arranque; dentro
/// del bucle solo hay **sumas** e indexado (sin multiplicar ni dividir por píxel).
///
/// Es una utilidad de **efecto puro** (matemáticas y memoria, sin hardware): se valida
/// en host (`tests/host/022_rotozoom`). El `double` vive solo en la generación
/// compile-time de la tabla; el bucle emitido es entero.

#include <eng/core/domains.hpp>
#include <eng/core/sinetable.hpp>
#include <eng/core/types.hpp>

namespace eng::graphics {

namespace rotozoom_detail {

/// Seno 16.16 (65536 = 1.0) de 256 fases: `kSin16[i] = sin(2π·i/256)`. El coseno es
/// `kSin16[(i+64)&255]`. Una sola tabla, generada en compile-time (sin datos a mano).
inline constexpr SineTable<65536, 256> kSin16 {};

} // namespace rotozoom_detail

/// Parámetros del rotozoom. Todos los campos de punto fijo usan **16.16** (65536 = 1.0).
struct Rotozoom {
	u16 angle = 0;    ///< Fase de rotación, 0..255 (vuelta completa = 256).
	s32 zoom = 65536; ///< Zoom: texeles por píxel de pantalla (1.0 = 65536).
	s32 offset_x = 0; ///< Coordenada de textura (u) en el centro de la pantalla.
	s32 offset_y = 0; ///< Coordenada de textura (v) en el centro de la pantalla.
};

/// Muestrea `tex` (TW×TH texeles, potencias de dos) hacia `dst` (w×h índices, 1 B/píxel,
/// `w` múltiplo de 16 para que el C2P lo acepte). `dst` debe medir al menos `w*h` bytes.
template <u16 TW, u16 TH>
void rotozoom_into(IndexedTexture tex, const Rotozoom& r, ChunkyBuffer dst, u16 w, u16 h) {
	static_assert(TW > 0 && (TW & (TW - 1u)) == 0, "TW debe ser potencia de dos");
	static_assert(TH > 0 && (TH & (TH - 1u)) == 0, "TH debe ser potencia de dos");
	using rotozoom_detail::kSin16;

	const s32 ca = kSin16[static_cast<u8>((r.angle + 64u) & 0xffu)]; // cos
	const s32 sa = kSin16[static_cast<u8>(r.angle & 0xffu)];         // sin
	const s32 cx = static_cast<s32>(w) / 2;
	const s32 cy = static_cast<s32>(h) / 2;

	// Zoom en 8.8 (256 = 1.0): mantiene `ca*zi` por debajo de 2^26, así que todos los
	// productos caben en 32 bits. El runtime m68k-amiga NO enlaza `__muldi3` (libgcc),
	// de modo que un `s64` aquí no compilaría/lincharía. La pérdida de precisión es de
	// 1/256 de texel, invisible.
	const s32 zi = r.zoom >> 8;
	const s32 du = (ca * zi) >> 8; // paso por píxel en u
	const s32 dv = (sa * zi) >> 8; // paso por píxel en v

	// (u,v) del centro de rotacion (x=0,y=0). Se reparte `-cx*du + cy*dv` en vez de
	// multiplicar por el zoom: mismo resultado exacto (dx=-cx, dy=-cy) sin productos
	// grandes.
	s32 u_row = r.offset_x - cx * du + cy * dv;
	s32 v_row = r.offset_y - cx * dv - cy * du;

	const u8* texels = tex.data();
	u8* out = dst.data();

	for (u32 y = 0; y < h; ++y) {
		s32 u = u_row;
		s32 v = v_row;
		u8* row = out + static_cast<u32>(y) * w;
		for (u32 x = 0; x < w; ++x) {
			const u32 tx = (static_cast<u32>(u) >> 16) & (TW - 1u);
			const u32 ty = (static_cast<u32>(v) >> 16) & (TH - 1u);
			row[x] = texels[tx * TH + ty];
			u += du;
			v += dv;
		}
		u_row -= dv; // paso en y de u
		v_row += du; // paso en y de v
	}
}

} // namespace eng::graphics
