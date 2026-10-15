#pragma once

/// \file evaluators.hpp
/// **Evaluadores de píxel** (expresiones `u8(u16 x, u16 y)`) y su volcado a un
/// `BitmapBuffer` (`fill`, `fill_masked`). Un evaluador es cualquier callable que
/// devuelve un índice de color; se componen con `Layer` (máscara A/B), con las texturas
/// de `textures.hpp` o con las primitivas de `shapes.hpp`.
///
/// Genéricos sobre el escalar `S` (§1.11): el ruido (`fbm2`, `turbulence2`, `worley2_sq`
/// de `eng/core/math/noise.hpp`) exige un escalar con rango y división — en el Amiga el
/// válido es `MiniFloat16` (10 bits, `[2^-14, 65504]`; ver `noise.hpp` y demo
/// `083_fbm_noise`); en host, `float`/`double`. Un `Fixed` 4.12 **no** compila el ruido
/// (el diagnóstico de `require_grid_range` lo explica al instanciar).
///
/// ```text
///   evaluador:  u8 f(x, y)                      fill(buf, f)
///      ├─ FbmFill<S>     ruido fBm → cuantizado con dither Bayer
///      ├─ CircleFill     disco por distancia² (sin sqrt)
///      ├─ Hill2D<N>      perfil 1D separable (colina/valle)
///      └─ Layer<Fa,Fb,Fm>  mask(x,y) ? a(x,y) : b(x,y)
/// ```
///
/// ## Coste
///
/// `fill` recorre `W·H` píxeles una vez (generación/carga, nunca por frame). `FbmFill`
/// paga `octaves` × `value_noise2` por píxel (el ruido más caro del engine: ~2 `__mulsi3`
/// de hash por muestra de rejilla); `CircleFill`/`Hill2D` son unos pocos ciclos por píxel.
/// Precalcular tiles y reutilizarlos es la estrategia (ver `atlas.hpp`).
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-442`); no corre en hardware en la demo 119
/// (materiales precocinados; medida en `PROCEDURAL_BITMAPS.md` §Coste).

#include <eng/core/math/noise.hpp>
#include <eng/core/math/scalar_ops.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/dither.hpp>
#include <eng/graphics/proc/profile.hpp>

namespace eng::gfx::proc {

/// Escala de ruido para un tile de `tile_px` píxeles con `freq` ciclos por tile
/// (`freq/tile_px`): el cociente exacto que hace que `noise(x + period) == noise(x)` con
/// `period = freq` (ver contrato de periodicidad en `textures.hpp`).
/// \tparam S escalar del ruido (`MiniFloat16`, `float`…). \param tile_px ancho del tile en píxeles.
/// \param freq ciclos de ruido por tile (1..). \return factor `freq/tile_px` en `S`.
template <class S>
[[nodiscard]] constexpr S tile_scale(eng::u16 tile_px, eng::u8 freq) {
	using eng::math::div_norm;
	using eng::math::scalar_traits;
	return div_norm(scalar_traits<S>::from_int(freq), scalar_traits<S>::from_int(tile_px));
}

/// Relleno por **fBm**: ruido `fbm2` con dither Bayer a `levels` colores. Es el material
/// base de hierba/tierra/nubes y el interior de los tiles orgánicos.
/// \tparam S escalar del ruido (con rango de rejilla; ver `noise.hpp`).
template <class S>
struct FbmFill {
	/// Semilla del ruido (tiles distintos con seeds distintos = variantes).
	eng::u32 seed = 0;
	/// Niveles de color usados (2..32; salida 0..levels-1).
	eng::u8 levels = 16;
	/// Octavas de fBm (más octavas = más detalle y coste).
	eng::u8 octaves = 4;
	/// Dither ordenado al cuantizar (false = cuantización plana, útil para tests).
	bool dither = true;
	/// Periodo del ruido en unidades de `inv_size` (0 = no tileable; ver `tile_scale`).
	int period = 0;
	/// Frecuencia espacial: 1 píxel = `inv_size` unidades de ruido (`tile_scale<S>`).
	S inv_size {};

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return índice 0..levels-1.
	[[nodiscard]] eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		using eng::math::div_norm;
		using eng::math::fbm2;
		using eng::math::fbm2_cached;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		const S fx = mul_norm(scalar_traits<S>::from_int(x), inv_size);
		const S fy = mul_norm(scalar_traits<S>::from_int(y), inv_size);
		const S two = scalar_traits<S>::from_int(2);
		const S half = div_norm(scalar_traits<S>::one(), two);
		const S n = fbm2_cached(fx, fy, seed, octaves, two, half, period);
		const eng::u8 inten = scalar_traits<S>::to_int(mul_norm(n, scalar_traits<S>::from_int(255)));
		return dither ? quantize_dither(inten, x, y, levels) : quantize_plain(inten, levels);
	}
};

/// Disco/elipse barata por **distancia al cuadrado** (sin `sqrt`): el test `d² ≤ r²` es
/// exacto y una sola comparación por píxel. Base de lunas, cofres y siluetas redondas.
struct CircleFill {
	/// Centro del disco en píxeles del tile.
	eng::u16 cx = 16;
	/// Centro del disco en píxeles del tile.
	eng::u16 cy = 16;
	/// Radio al cuadrado (`r·r`; así el test interior es una comparación).
	eng::u16 r2 = 100;
	/// Color interior (índice de paleta).
	eng::u8 color_in = 1;
	/// Color exterior (índice de paleta).
	eng::u8 color_out = 0;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `color_in` si `(x,y)` cae en el disco, si no `color_out`.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		const eng::s32 dx = x - cx;
		const eng::s32 dy = y - cy;
		const eng::s32 d2 = dx * dx + dy * dy;
		return (d2 <= r2) ? color_in : color_out;
	}
};

/// Colina/valle 2D por **producto separable de perfiles 1D** (`(px[y]+py[y])/2`): la
/// silueta de montaña y las dunas se resuelven con lookups, sin ruido por píxel.
/// \tparam N  tramos del perfil (tablas de `N+1` muestras, escala 0..255).
template <eng::u16 N>
struct Hill2D {
	/// Perfil horizontal (altura por columna, extendido con `x % (N+1)`).
	Profile1D<N> px {};
	/// Perfil vertical (altura por fila, extendido con `y % (N+1)`).
	Profile1D<N> py {};
	/// Niveles de color de salida (2..32).
	eng::u8 levels = 16;

	/// Evalúa el píxel `(x, y)` con la media de ambos perfiles.
	/// \param x,y píxel del tile. \return índice 0..levels-1 (altura cuantizada).
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		const eng::u16 hx = px.y[x % (N + 1u)];
		const eng::u16 hy = py.y[y % (N + 1u)];
		const eng::u8 h = (hx + hy) >> 1u;
		return quantize_plain(h, levels);
	}
};

/// Combinador de capas: dos evaluadores y una máscara (`mask(x,y) != 0` elige `a`).
/// Permite componer expresiones sin clases nuevas: `Layer{CircleFill{…}, FbmFill<S>{…}, mask}`.
/// \tparam Fa evaluador de la capa superior. \tparam Fb evaluador de la capa inferior. \tparam Fm máscara (0/1).
template <class Fa, class Fb, class Fm>
struct Layer {
	/// Capa superior (se elige donde la máscara es distinta de cero).
	Fa a {};
	/// Capa inferior (se elige donde la máscara es cero).
	Fb b {};
	/// Máscara de selección (cualquier callable que devuelva 0/1).
	Fm mask {};

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return índice de `a` o `b`.
	[[nodiscard]] eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		return mask(x, y) ? a(x, y) : b(x, y);
	}
};

/// Rellena `buf` evaluando `fn(x, y)` fila a fila por píxel. Vale en `constexpr` (con
/// evaluadores `constexpr`) y en runtime (generadores con ruido): la firma es la misma.
/// \param buf buffer destino (su geometría fija el bucle). \param fn evaluador de píxel.
template <eng::u16 W, eng::u16 H, class PixelFn>
constexpr void fill(BitmapBuffer<W, H>& buf, PixelFn&& fn) {
	for (eng::u16 y = 0; y < H; ++y) {
		for (eng::u16 x = 0; x < W; ++x) {
			buf.at(x, y) = fn(x, y);
		}
	}
}

/// Rellena `buf` con `mask`: donde la máscara es cero el píxel se deja **transparente**
/// (índice 0), el convenio del motor para siluetas (sprites, bocas de cueva).
/// \param buf  buffer destino. \param fn evaluador de píxel (solo se paga donde hay máscara).
/// \param mask máscara 0/1 por píxel.
template <eng::u16 W, eng::u16 H, class PixelFn, class MaskFn>
constexpr void fill_masked(BitmapBuffer<W, H>& buf, PixelFn&& fn, MaskFn&& mask) {
	for (eng::u16 y = 0; y < H; ++y) {
		for (eng::u16 x = 0; x < W; ++x) {
			buf.at(x, y) = mask(x, y) ? fn(x, y) : 0u;
		}
	}
}

} // namespace eng::gfx::proc
