#pragma once

/// \file planar_band.hpp
/// **Banda planar** (`eng::graphics`): un rectángulo de bitplanes como bloque de
/// palabras (1 palabra = 16 px; bit 15 = píxel izquierdo, el convenio del chipset).
/// Es la unidad del **parcheo CPU del display** — actores, cursores, elementos de HUD —
/// sin pagar un C2P por frame: se captura una vez el fondo, se restaura la ventana del
/// actor y se vuelve a pintar con máscara (cookie-cut planar).
///
/// ```text
///   banda (w palabras × h filas × planes)          bitmap planar del display
///     [p][r][0..w-1]                                  base + p·plane_stride
///          │                                                 + r·row_bytes
///          │                                                 + wx·2
///          ▼                                                      │
///     get_band ────────────────► (copia de fondo)                 │
///     put_band_masked ── dst = (dst & ~mask) | (src & mask) ◄─────┘
/// ```
///
/// `make_band_from_pixels` construye una banda desde un patrón de píxeles (índices de
/// color → `planes` bits) **desplazado `shift` px** dentro de una ventana de
/// `band_window_words(w)` palabras: los 16 desplazamientos de un actor se precomputan
/// en compilación y en el frame solo se indexan (cero shifts en runtime).
///
/// ## Coste
///
/// Por palabra: una carga + una escritura (+ y/or en la variante con máscara). Un actor
/// de 16×16 en 5 planos con ventana de 2 palabras ≈ 320 operaciones de palabra por
/// frame, muy por debajo del presupuesto de un field (ver `measure-fps` de la demo 121).
///
/// ## Estado de verificación: VERIFICADA (demo 121)
/// El parcheo de bandas (fondo + actor con máscara de copas) se ejercita en hardware en
/// `demos/techniques/amiga/effects/121_proc_corner_layers`; test host `HOST-450`.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Palabras de una fila de la **ventana** de un patrón de `w` píxeles desplazado
/// hasta 15 px (`ceil((w+15)/16)`): lo que necesita `make_band_from_pixels`.
/// \param w ancho del patrón en píxeles. \return palabras por fila (1..).
[[nodiscard]] constexpr eng::u16 band_window_words(eng::u16 w) {
	return eng::u16((eng::u32(w) + 30u) >> 4u);
}

/// Palabras de una banda `w × h` de `planes` planos: `w·h·planes`
/// (layout `[plano][fila][palabra]`).
/// \param w,h palabras y filas. \param planes planos. \return total de palabras.
[[nodiscard]] constexpr eng::u32 band_words(eng::u16 w, eng::u16 h, eng::u8 planes) {
	return eng::u32(w) * h * planes;
}

/// **Extrae** la banda `(wx, y, w, h)` de los `planes` bitplanes contiguos de `base`
/// (separación entre planos `plane_stride`; `row_bytes` por fila de plano; `wx` en
/// palabras) a `dst` con el layout `[plano][fila][palabra]`.
/// \param base inicio del plano 0 (bytes, big-endian de palabra en Amiga). \param plane_stride
///        bytes entre planos. \param row_bytes bytes por fila de un plano. \param wx palabra
///        inicial (x/16). \param y fila inicial. \param w,h tamaño en palabras/filas.
/// \param planes planos. \param dst banda destino (`w·h·planes` palabras).
inline void get_band(const eng::u8* base, eng::u32 plane_stride, eng::u16 row_bytes, eng::u16 wx,
		     eng::u16 y, eng::u16 w, eng::u16 h, eng::u8 planes, eng::u16* dst) {
	for (eng::u8 p = 0u; p < planes; ++p) {
		for (eng::u16 r = 0u; r < h; ++r) {
			const eng::u8* src =
				base + eng::u32(p) * plane_stride + eng::u32(y + r) * row_bytes +
				eng::u32(wx) * 2u;
			eng::u16* out = dst + (eng::u32(p) * h + r) * w;
			for (eng::u16 c = 0u; c < w; ++c) {
				out[c] = reinterpret_cast<const eng::u16*>(src)[c];
			}
		}
	}
}

/// **Escribe** la banda `src` (layout `[plano][fila][palabra]`) en los bitplanes de
/// `base` en `(wx, y)` (mismo contrato de geometría que `get_band`).
/// \param base plano 0. \param plane_stride separación entre planos. \param row_bytes fila.
/// \param wx,y destino en palabras/filas. \param w,h tamaño. \param planes planos.
/// \param src banda origen.
inline void put_band(eng::u8* base, eng::u32 plane_stride, eng::u16 row_bytes, eng::u16 wx,
		     eng::u16 y, eng::u16 w, eng::u16 h, eng::u8 planes, const eng::u16* src) {
	for (eng::u8 p = 0u; p < planes; ++p) {
		for (eng::u16 r = 0u; r < h; ++r) {
			eng::u8* dst = base + eng::u32(p) * plane_stride + eng::u32(y + r) * row_bytes +
				       eng::u32(wx) * 2u;
			const eng::u16* in = src + (eng::u32(p) * h + r) * w;
			for (eng::u16 c = 0u; c < w; ++c) {
				reinterpret_cast<eng::u16*>(dst)[c] = in[c];
			}
		}
	}
}

/// **Parchea con máscara** (cookie-cut planar): `dst = (dst & ~mask) | (src & mask)`.
/// La máscara es **una banda de 1 plano** (1 palabra por fila × `w`); `mask = 0` deja el
/// destino intacto (fondo), `mask = 1` escribe el píxel de `src`. Es el mecanismo para
/// dibujar un actor que respeta la capa de overhead (los bits de la copa lo tapan).
/// \param base plano 0. \param plane_stride separación entre planos. \param row_bytes fila.
/// \param wx,y destino en palabras/filas. \param w,h tamaño. \param planes planos.
/// \param src banda del patrón. \param mask banda de máscara (`w·h` palabras).
inline void put_band_masked(eng::u8* base, eng::u32 plane_stride, eng::u16 row_bytes, eng::u16 wx,
			    eng::u16 y, eng::u16 w, eng::u16 h, eng::u8 planes,
			    const eng::u16* src, const eng::u16* mask) {
	for (eng::u8 p = 0u; p < planes; ++p) {
		for (eng::u16 r = 0u; r < h; ++r) {
			eng::u8* dst = base + eng::u32(p) * plane_stride + eng::u32(y + r) * row_bytes +
				       eng::u32(wx) * 2u;
			const eng::u16* in = src + (eng::u32(p) * h + r) * w;
			const eng::u16* m = mask + eng::u32(r) * w;
			for (eng::u16 c = 0u; c < w; ++c) {
				eng::u16* const d = reinterpret_cast<eng::u16*>(dst) + c;
				*d = eng::u16((*d & eng::u16(~m[c])) | (in[c] & m[c]));
			}
		}
	}
}

/// **Construye una banda desde un patrón de píxeles** (compilación): `pixels[y*W+x]`
/// (índices de color; bit `p` del índice → plano `p`, `0` = transparente) se coloca en
/// la ventana desplazado `shift` px (0..15) — los 16 desplazamientos se precomputan y en
/// el frame solo se indexa. `mask` (1 palabra/fila) marca `1` en los píxeles con color
/// distinto de 0; es la máscara para `put_band_masked` (el llamador la combina con la de
/// la capa de overhead si procede).
/// \tparam W,H tamaño del patrón (px). \tparam Planes planos del destino (bits de color).
/// \param pixels patrón fila-major. \param shift desplazamiento horizontal (0..15).
/// \param dst banda destino (`band_window_words(W)·H·Planes` palabras).
/// \param mask máscara destino (`band_window_words(W)·H` palabras).
template <eng::u16 W, eng::u16 H, eng::u8 Planes>
constexpr void make_band_from_pixels(const eng::u8 (&pixels)[W * H], eng::u16 shift, eng::u16* dst,
				     eng::u16* mask) {
	constexpr eng::u16 wpr = band_window_words(W);
	static_assert(W > 0u && H > 0u && Planes >= 1u && Planes <= 8u, "patron de banda invalido");
	const eng::u32 total = eng::u32(wpr) * H * Planes;
	for (eng::u32 i = 0u; i < total; ++i) dst[i] = 0u;
	for (eng::u32 i = 0u; i < eng::u32(wpr) * H; ++i) mask[i] = 0u;
	for (eng::u16 y = 0u; y < H; ++y) {
		for (eng::u16 x = 0u; x < W; ++x) {
			const eng::u8 color = pixels[eng::u32(y) * W + x];
			if (color == 0u) continue;
			const eng::u16 bx = eng::u16(x + shift);
			const eng::u16 c = eng::u16(bx >> 4u);
			const eng::u16 bit = eng::u16(0x8000u >> (bx & 15u));
			eng::u16& mcell = mask[eng::u32(y) * wpr + c];
			mcell = eng::u16(mcell | bit);
			for (eng::u8 p = 0u; p < Planes; ++p) {
				if (((color >> p) & 1u) != 0u) {
					eng::u16& cell = dst[(eng::u32(p) * H + y) * wpr + c];
					cell = eng::u16(cell | bit);
				}
			}
		}
	}
}

} // namespace eng::graphics
