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
/// en host (`tests/host/graphics/132_rotozoom`). El `double` vive solo en la generación
/// compile-time de la tabla; el bucle emitido es entero.
///
/// **Por qué el punto fijo va crudo (`s32` 16.16).** Es deliberado: tipar las
/// coordenadas como `Fixed<s32,16>` ensancharía cada producto a 64 bits
/// (`mul_repr<s32>`) y en 68000 eso es `__muldi3` (rutina de libgcc que no enlaza). El
/// bucle incremental solo necesita sumas y desplazamientos sobre `s32`, así que el
/// campo se queda como entero de 16.16 y el layout lo comparte el ASM
/// `support/rotozoom_loop.s`.

#include <eng/core/types/domains.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/core/types/types.hpp>

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

/// Estado incremental del rotozoom: `(u,v)` del primer píxel y los pasos por píxel,
/// todo en punto fijo 16.16. Lo consume el bucle C++ (`rotozoom_into`) y la vía asm
/// (`support/rotozoom_loop.s`), que **debe** partir de los mismos números para que
/// ambas rutas sean equivalentes.
struct RotozoomSteps {
	s32 u = 0;  ///< Coordenada u del píxel (0,0), 16.16.
	s32 v = 0;  ///< Coordenada v del píxel (0,0), 16.16.
	s32 du = 0; ///< Paso de u por píxel, 16.16.
	s32 dv = 0; ///< Paso de v por píxel, 16.16.
};

/// Calcula `RotozoomSteps` para un área `w`×`h`. El zoom se aplica en 8.8 (256 = 1.0)
/// para que todos los productos quepan en 32 bits: el runtime m68k-amiga NO enlaza
/// `__muldi3` (libgcc), así que un `s64` aquí no compilaría/lincharía. La pérdida de
/// precisión es de 1/256 de texel, invisible.
template <u16 TW, u16 TH>
constexpr RotozoomSteps rotozoom_steps(const Rotozoom& r, u16 w, u16 h) {
	const s32 ca = rotozoom_detail::kSin16[static_cast<u8>((r.angle + 64u) & 0xffu)]; // cos
	const s32 sa = rotozoom_detail::kSin16[static_cast<u8>(r.angle & 0xffu)];         // sin
	const s32 cx = static_cast<s32>(w) / 2;
	const s32 cy = static_cast<s32>(h) / 2;
	const s32 zi = r.zoom >> 8;

	RotozoomSteps s {};
	s.du = (ca * zi) >> 8; // paso por píxel en u
	s.dv = (sa * zi) >> 8; // paso por píxel en v
	// (u,v) del centro de rotación (x=0,y=0): se reparte `-cx*du + cy*dv` en vez de
	// multiplicar por el zoom (mismo resultado exacto, sin productos grandes).
	s.u = r.offset_x - cx * s.du + cy * s.dv;
	s.v = r.offset_y - cx * s.dv - cy * s.du;
	return s;
}

/// Muestrea `tex` (TW×TH texeles, potencias de dos) hacia `dst` (w×h índices, 1 B/píxel,
/// `w` múltiplo de 16 para que el C2P lo acepte). `dst` debe medir al menos `w*h` bytes.
template <u16 TW, u16 TH>
void rotozoom_into(IndexedTexture tex, const Rotozoom& r, ChunkyBuffer dst, u16 w, u16 h) {
	static_assert(TW > 0 && (TW & (TW - 1u)) == 0, "TW debe ser potencia de dos");
	static_assert(TH > 0 && (TH & (TH - 1u)) == 0, "TH debe ser potencia de dos");

	const RotozoomSteps st = rotozoom_steps<TW, TH>(r, w, h);
	const u8* texels = tex.data();
	u8* out = dst.data();
	s32 u_row = st.u;
	s32 v_row = st.v;

	for (u32 y = 0; y < h; ++y) {
		s32 u = u_row;
		s32 v = v_row;
		u8* row = out + static_cast<u32>(y) * w;
		for (u32 x = 0; x < w; ++x) {
			const u32 tx = (static_cast<u32>(u) >> 16) & (TW - 1u);
			const u32 ty = (static_cast<u32>(v) >> 16) & (TH - 1u);
			row[x] = texels[tx * TH + ty];
			u += st.du;
			v += st.dv;
		}
		// El inicio de la fila siguiente NO es el acumulador tal como queda el bucle
		// (que termina en `inicio + w·paso`), sino `inicio ∓ paso_de_fila`: se lleva
		// aparte en `u_row`/`v_row`. La vía asm debe respetar lo mismo.
		u_row -= st.dv; // paso en y de u
		v_row += st.du; // paso en y de v
	}
}

} // namespace eng::graphics
