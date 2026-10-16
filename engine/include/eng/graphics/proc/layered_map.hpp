#pragma once

/// \file layered_map.hpp
/// **Mapa de tiles por capas + colisión por celdas** (`eng::gfx::proc`): el tilemap
/// que especifica sin ambigüedad **qué se dibuja en cada capa** y **qué bloquea**,
/// separando el dibujo de la física.
///
/// ```text
///   orden de dibujo (atrás → delante)         datos por celda (SoA)
///     0  Ground      (suelo, opaco)            ground  u16[W*H]   índice de tile
///     1  Decal       (flores, sombras)         decal   u16[W*H]   kEmptyTile = vacío
///     2  [actores]   (jugador, NPCs)           overhead u16[W*H]  kEmptyTile = vacío
///     3  Overhead    (copas, arcos, techos)    flags   u8[W*H]    CellFlags (colisión)
/// ```
///
/// - **Cada capa es una rejilla propia** (SoA): se envuelve como
///   `eng::playfield::TileLayerMap` **sin conversión** (`map.span(MapLayer::Ground)`),
///   y el scroll consume solo la capa que le toca.
/// - **Pixel 0 de un tile = transparente** en decal/overhead (convenio del engine,
///   `bitmap_buffer.hpp`): las copas se dibujan con `blit_tile(..., transparent=true)`
///   y el personaje queda **entre** el suelo y el overhead (callback `between` de
///   `render_layered_map`) → «pasa por debajo» de forma natural.
/// - **La colisión no mira el dibujo**: `flags` se hornea desde el índice del suelo
///   (`bake_flags`) y el overhead **nunca** contribuye; `move_and_slide` resuelve el
///   AABB del actor **eje a eje** (primero X, luego Y: deslizamiento en esquinas).
/// - Máscara de colisión = `eng::util::Aabb` en espacio de mundo (bordes exclusivos);
///   la hitbox del actor es **más pequeña que el sprite** (pies), no el sprite entero.
///
/// ## Coste
///
/// Todo es de **carga/generación** (composición one-shot, como `blit_corner_map`);
/// la consulta por frame es un OR de `u8` por celda cubierta (≤ 4 para una hitbox
/// de un tile) y el movimiento no asigna ni divide (shifts por `N` potencia de dos).
/// La física completa por perfiles (dynamic bodies, plataformas móviles…) es otro
/// nivel (`PIXEL_ART_2D_ISOMETRIC.md`); aquí solo el mínimo del tilemap.
///
/// ## Estado de verificación: VERIFICADA (demo 121)
/// En hardware por `demos/techniques/amiga/effects/121_proc_corner_layers` (mapa
/// ground/decal/overhead compuesto al framebuffer de runtime y actor entre capas que se
/// oculta bajo las copas); tests host `HOST-448`.
/// Diseño y uso: `docs/engine/architecture/PROCEDURAL_BITMAPS.md` §Mapas por capas.

#include <eng/core/math/fast_div.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>

namespace eng::gfx::proc {

/// **Banderas de gameplay por celda** (máscara combinable), independientes del
/// dibujo: la misma variante visual puede ser sólida o no según la celda. Los bits
/// 5..7 quedan libres para el juego (plataformas, hielo, trampas…).
enum class CellFlags : eng::u8 {
	/// Sin propiedades: transitable a pie.
	None = 0u,
	/// Bloquea el movimiento (muro, roca, torre…).
	Solid = 1u << 0u,
	/// Agua: el juego decide (nadar, frenar, daño…); no bloquea por sí sola.
	Water = 1u << 1u,
	/// Terreno lento (barro, cultivos): multiplica la velocidad por un factor.
	Slow = 1u << 2u,
	/// Zona de evento al entrar (puerta, diálogo): no bloquea.
	Trigger = 1u << 3u,
	/// Escalera: movimiento vertical con reglas propias (si el juego tiene altura).
	Ladder = 1u << 4u,
};

/// Unión de banderas (OR de bits).
/// \param a,b banderas. \return máscara con ambos conjuntos.
[[nodiscard]] constexpr CellFlags operator|(CellFlags a, CellFlags b) {
	return CellFlags(eng::util::to_underlying(a) | eng::util::to_underlying(b));
}
/// Intersección de banderas (AND de bits).
/// \param a,b banderas. \return banderas presentes en ambas.
[[nodiscard]] constexpr CellFlags operator&(CellFlags a, CellFlags b) {
	return CellFlags(eng::util::to_underlying(a) & eng::util::to_underlying(b));
}
/// ¿La máscara `f` contiene la bandera `bit`?
/// \param f máscara. \param bit bandera a comprobar. \return true si está activa.
[[nodiscard]] constexpr bool has(CellFlags f, CellFlags bit) {
	return (eng::util::to_underlying(f) & eng::util::to_underlying(bit)) != 0u;
}

/// Capa del mapa (orden de composición; `Count` no es una capa válida).
enum class MapLayer : eng::u8 { Ground = 0, Decal = 1, Overhead = 2, Count = 3 };

/// Valor de celda «sin tile» en decal/overhead (`0xFFFF`; el índice 0 es un tile
/// válido en los sets de `proc`).
inline constexpr eng::u16 kEmptyTile = 0xFFFFu;

/// **Mapa en capas de `W×H` celdas** con tiles de `N×N` píxeles: rejillas SoA
/// (`ground`/`decal`/`overhead`) + las banderas de colisión de cada celda.
/// \tparam W,H celdas del mapa. \tparam N lado del tile en píxeles (potencia de dos ≥ 8).
template <eng::u16 W, eng::u16 H, eng::u16 N>
struct LayeredTileMap {
	static_assert(W > 0u && H > 0u, "el mapa en capas no puede estar vacío");
	static_assert(N >= 8u && eng::is_pow2(N), "tiles de lado potencia de dos >= 8");
	static_assert(eng::u32(W) * N <= 0xFFFFu && eng::u32(H) * N <= 0xFFFFu,
		      "el mapa en píxeles debe caber en u16 (geometría de pantalla)");
	/// Rejilla del suelo (índice de tile; toda celda tiene suelo).
	eng::u16 ground[eng::u32(W) * H] {};
	/// Rejilla de decal (kEmptyTile = sin decal).
	eng::u16 decal[eng::u32(W) * H] {};
	/// Rejilla de overhead (kEmptyTile = sin copa/arco).
	eng::u16 overhead[eng::u32(W) * H] {};
	/// Banderas de colisión/gameplay por celda (independientes del dibujo).
	eng::u8 flags[eng::u32(W) * H] {};
	/// Banderas de una coordenada fuera del mapa (borde del mundo).
	CellFlags outside = CellFlags::Solid;
	/// Ancho del mapa en celdas.
	static constexpr eng::u16 width = W;
	/// Alto del mapa en celdas.
	static constexpr eng::u16 height = H;
	/// Ancho del mapa en píxeles (`W·N`).
	static constexpr eng::u16 pixel_width = W * N;
	/// Alto del mapa en píxeles (`H·N`).
	static constexpr eng::u16 pixel_height = H * N;

	/// Mapa vacío: suelo = tile 0, decal/overhead = `kEmptyTile`, sin banderas.
	constexpr LayeredTileMap() {
		for (eng::u32 i = 0u; i < eng::u32(W) * H; ++i) {
			ground[i] = 0u;
			decal[i] = kEmptyTile;
			overhead[i] = kEmptyTile;
			flags[i] = 0u;
		}
	}

	/// Rejilla de la capa `l` (mutable). `MapLayer::Count` no es válida: devuelve el suelo.
	/// \param l capa. \return puntero a `W*H` índices.
	[[nodiscard]] constexpr eng::u16* data(MapLayer l) {
		switch (l) {
		case MapLayer::Decal: return decal;
		case MapLayer::Overhead: return overhead;
		default: return ground;
		}
	}
	/// Rejilla (solo lectura) de la capa `l`.
	/// \param l capa. \return puntero a `W*H` índices.
	[[nodiscard]] constexpr const eng::u16* data(MapLayer l) const {
		switch (l) {
		case MapLayer::Decal: return decal;
		case MapLayer::Overhead: return overhead;
		default: return ground;
		}
	}
	/// Vista de la capa `l` para envolverla como `eng::playfield::TileLayerMap`
	/// (`map.cells = layered.span(MapLayer::Ground)`), sin conversión.
	/// \param l capa. \return span de `W*H` índices.
	[[nodiscard]] constexpr eng::Span<const eng::u16> span(MapLayer l) const {
		return {data(l), eng::usize(W) * H};
	}
	/// Índice de tile de la celda `(x, y)` en la capa `l`.
	/// \param l capa. \param x,y celda (0..W-1, 0..H-1). \return índice (`kEmptyTile` = vacío).
	[[nodiscard]] constexpr eng::u16 cell(MapLayer l, eng::u16 x, eng::u16 y) const {
		return data(l)[eng::u32(y) * W + eng::u32(x)];
	}
	/// Escribe el índice de tile de la celda `(x, y)` en la capa `l`.
	/// \param l capa. \param x,y celda. \param index índice (`kEmptyTile` = vacío).
	constexpr void set_cell(MapLayer l, eng::u16 x, eng::u16 y, eng::u16 index) {
		data(l)[eng::u32(y) * W + eng::u32(x)] = index;
	}
	/// Rellena toda una capa con `index` (atajo del relleno base del generador).
	/// \param l capa. \param index índice de tile.
	constexpr void fill_layer(MapLayer l, eng::u16 index) {
		eng::u16* cells = data(l);
		for (eng::u32 i = 0u; i < eng::u32(W) * H; ++i) cells[i] = index;
	}

	/// Banderas de la celda `(x, y)` (fuera del mapa: `outside`).
	/// \param x,y celda. \return máscara de banderas.
	[[nodiscard]] constexpr CellFlags flags_at(eng::u16 x, eng::u16 y) const {
		if (x >= W || y >= H) return outside;
		return CellFlags(flags[eng::u32(y) * W + eng::u32(x)]);
	}
	/// Banderas de la celda que contiene el **píxel** `(px, py)` (los pies del
	/// actor y los triggers se consultan por punto).
	/// \param px,py píxel de mundo. \return máscara de banderas (fuera: `outside`).
	[[nodiscard]] constexpr CellFlags flags_at_pixel(eng::s16 px, eng::s16 py) const {
		if (px < 0 || py < 0 || px >= eng::s16(pixel_width) || py >= eng::s16(pixel_height)) {
			return outside;
		}
		constexpr eng::s32 kLog = eng::s32(eng::ilog2(N));
		return CellFlags(flags[eng::u32(py >> kLog) * W + eng::u32(px >> kLog)]);
	}
	/// Escribe las banderas de la celda `(x, y)` (sobrescribe).
	/// \param x,y celda. \param f máscara nueva.
	constexpr void set_flags(eng::u16 x, eng::u16 y, CellFlags f) {
		flags[eng::u32(y) * W + eng::u32(x)] = eng::util::to_underlying(f);
	}
	/// Añade banderas a la celda `(x, y)` (OR). Precondición: dentro del mapa.
	/// \param x,y celda. \param f banderas a añadir.
	constexpr void add_flags(eng::u16 x, eng::u16 y, CellFlags f) {
		eng::u8& fl = flags[eng::u32(y) * W + eng::u32(x)];
		fl = eng::u8(fl | eng::util::to_underlying(f));
	}

	/// **Hornea las banderas desde el suelo**: `flags_of(ground_index)` decide la
	/// máscara de cada celda (p. ej. agua → `Water`, muralla → `Solid`, hierba →
	/// `None`). Es el único punto que traduce tile → física: el set visual puede
	/// tener N variantes del mismo muro y todas comparten sus banderas.
	/// \tparam Fn callable `CellFlags(eng::u16 ground_index)`.
	/// \param flags_of política de banderas por tile de suelo.
	template <class Fn>
	constexpr void bake_flags(Fn&& flags_of) {
		for (eng::u32 i = 0u; i < eng::u32(W) * H; ++i) {
			flags[i] = eng::util::to_underlying(flags_of(ground[i]));
		}
	}

	/// OR de banderas de las celdas que cubre el AABB `[x, x+w) × [y, y+h)` en
	/// píxeles de mundo (el rectángulo de la hitbox del actor). Si parte del AABB
	/// cae fuera del mapa, la contribución de esa parte es `outside`.
	/// \param x,y esquina superior izquierda (píxeles). \param w,h tamaño (píxeles).
	/// \return máscara acumulada (consulta `has(f, CellFlags::Solid)`).
	[[nodiscard]] constexpr CellFlags flags_aabb(eng::s16 x, eng::s16 y, eng::u16 w,
						     eng::u16 h) const {
		if (w == 0u || h == 0u) return CellFlags::None;
		constexpr eng::s32 kLog = eng::s32(eng::ilog2(N));
		const eng::s32 cx0 = x >> kLog;
		const eng::s32 cy0 = y >> kLog;
		const eng::s32 cx1 = (eng::s32(x) + w - 1) >> kLog;
		const eng::s32 cy1 = (eng::s32(y) + h - 1) >> kLog;
		CellFlags acc = CellFlags::None;
		if (cx0 < 0 || cy0 < 0 || cx1 >= eng::s32(W) || cy1 >= eng::s32(H)) acc = outside;
		for (eng::s32 cy = cy0 < 0 ? 0 : cy0; cy <= cy1 && cy < eng::s32(H); ++cy) {
			for (eng::s32 cx = cx0 < 0 ? 0 : cx0; cx <= cx1 && cx < eng::s32(W); ++cx) {
				acc = acc | CellFlags(flags[eng::u32(cy) * W + eng::u32(cx)]);
			}
		}
		return acc;
	}
	/// ¿El AABB `[x, x+w) × [y, y+h)` toca alguna celda `Solid`? Es la consulta
	/// que resuelve `move_and_slide`.
	/// \param x,y esquina en píxeles. \param w,h tamaño en píxeles. \return true si bloquea.
	[[nodiscard]] constexpr bool blocked(eng::s16 x, eng::s16 y, eng::u16 w, eng::u16 h) const {
		return has(flags_aabb(x, y, w, h), CellFlags::Solid);
	}
};

/// **Cuerpo de gameplay mínimo**: AABB en píxeles de mundo (bordes exclusivos).
/// La hitbox es la **de los pies** (más pequeña que el sprite): el actor «pasa»
/// por huecos en los que su dibujo no cabría.
struct MapBody {
	eng::s16 x = 0; ///< esquina superior izquierda (x).
	eng::s16 y = 0; ///< esquina superior izquierda (y).
	eng::u16 w = 0; ///< ancho de la hitbox.
	eng::u16 h = 0; ///< alto de la hitbox.
};

/// **Mueve un cuerpo resolviendo la colisión por ejes** (top-down): primero X,
/// luego Y; si un eje choca contra `Solid`, ese eje **no se aplica** (el otro
/// sigue → deslizamiento en esquinas). Precondiciones: `|dx|,|dy| ≤ N` (no se
/// atraviesa un tile en un paso) y el cuerpo trabaja dentro del mapa.
/// \tparam W,H,N geometría del mapa. \param map mapa (banderas ya horneadas).
/// \param body cuerpo a mover (se actualiza). \param dx,dy desplazamiento del paso.
template <eng::u16 W, eng::u16 H, eng::u16 N>
constexpr void move_and_slide(const LayeredTileMap<W, H, N>& map, MapBody& body, eng::s16 dx,
			      eng::s16 dy) {
	if (dx != 0) {
		const eng::s16 nx = eng::s16(body.x + dx);
		if (!map.blocked(nx, body.y, body.w, body.h)) body.x = nx;
	}
	if (dy != 0) {
		const eng::s16 ny = eng::s16(body.y + dy);
		if (!map.blocked(body.x, ny, body.w, body.h)) body.y = ny;
	}
}

/// Copia un tile `N×N` en `dest` en `(dx, dy)`; con `transparent`, los píxeles de
/// índice 0 **no se escriben** (cookie-cut del engine: copas, arcos, actores).
/// Todo el rectángulo debe caber en `dest` (lo garantizan los bucles del compositor).
/// \tparam N lado del tile. \tparam W,H tamaño del destino.
/// \param dest buffer destino. \param tile tile a copiar. \param dx,dy esquina en `dest`.
/// \param transparent true = no escribir el índice 0 (decal/overhead).
template <eng::u16 N, eng::u16 W, eng::u16 H>
constexpr void blit_tile(BitmapBuffer<W, H>& dest, const BitmapBuffer<N, N>& tile, eng::u16 dx,
			 eng::u16 dy, bool transparent) {
	for (eng::u16 y = 0u; y < N; ++y) {
		for (eng::u16 x = 0u; x < N; ++x) {
			const eng::u8 p = tile.at(x, y);
			if (!transparent || p != 0u) dest.at(eng::u16(dx + x), eng::u16(dy + y)) = p;
		}
	}
}

/// Igual que la variante `BitmapBuffer`, pero sobre un **framebuffer de runtime**
/// (`dest` con `stride` en píxeles: el buffer chunky del display, p. ej. el de
/// `eng::IndexedDisplay::framebuffer()`), sin copia intermedia a un buffer de
/// geometría fija.
/// \tparam N lado del tile. \param dest buffer destino (filas con `stride`).
/// \param stride píxeles por fila del destino. \param tile tile a copiar.
/// \param dx,dy esquina. \param transparent true = no escribir el índice 0.
template <eng::u16 N>
constexpr void blit_tile(eng::Span<eng::u8> dest, eng::u16 stride, const BitmapBuffer<N, N>& tile,
			 eng::u16 dx, eng::u16 dy, bool transparent) {
	for (eng::u16 y = 0u; y < N; ++y) {
		eng::u8* const row = dest.data() + eng::u32(dy + y) * stride + dx;
		for (eng::u16 x = 0u; x < N; ++x) {
			const eng::u8 p = tile.at(x, y);
			if (!transparent || p != 0u) row[x] = p;
		}
	}
}

/// Callback «entre capas» por defecto (sin actores): evita una lambda local en la
/// cabecera (el compilador m68k a `-O0` da ICE de CFI con lambdas locales).
struct NoBetweenRender {
	/// No hace nada (el compositor puede llamarlo con el buffer).
	/// \tparam Buf buffer (ignorado).
	template <class Buf>
	constexpr void operator()(Buf&) const {}
};

namespace layered_detail {

/// Adapta el callback «entre capas» de la variante `BitmapBuffer` (recibe el buffer
/// tipado) al de la variante `Span` (recibe la vista del framebuffer): el llamador de
/// `BitmapBuffer` sigue escribiendo su callback contra el buffer y la implementación
/// única es la de `Span`.
/// \tparam Buf tipo del buffer (p. ej. `BitmapBuffer<W,H>`). \tparam Cb callback original.
template <class Buf, class CbRef>
struct BetweenOverSpan {
	/// Tipo del callback sin la referencia de la deducción (`BF&&` deduce `Cb&`).
	using Cb = eng::util::remove_reference_t<CbRef>;
	Buf* buf = nullptr;     ///< buffer tipado (mismo almacenamiento que el span).
	Cb* callback = nullptr; ///< callback original `void(Buf&)`.
	/// Ignora el span (es el mismo almacenamiento) y llama al callback tipado.
	/// \param vista vista del framebuffer (no usada).
	constexpr void operator()(eng::Span<eng::u8> vista) const {
		(void)vista;
		(*callback)(*buf);
	}
};

} // namespace layered_detail

/// **Compone el mapa por capas** en `dest` (`W·N × H·N`): suelo (opaco) → decal
/// (transparente) → `between(dest)` (los actores del juego se dibujan aquí:
/// quedan sobre el suelo y **bajo** el overhead) → overhead (transparente).
/// Las celdas `kEmptyTile` se saltan; los proveedores traducen índice → bitmap:
/// `render_layered_map(map, dest, [&set](u16 i) { return set.get(i); }, …)`.
/// \tparam W,H,N geometría. \tparam GroundFn,DecalFn,OverFn callables
///         `const BitmapBuffer<N,N>&(eng::u16 index)`. \tparam BetweenFn `void(BitmapBuffer&)`.
/// \param map mapa (capas ya rellenas). \param dest destino (se sobrescribe).
/// \param ground_of,decal_of,over_of proveedores por capa. \param between actores.
template <eng::u16 W, eng::u16 H, eng::u16 N, class GroundFn, class DecalFn, class OverFn,
	  class BetweenFn>
constexpr void render_layered_map(const LayeredTileMap<W, H, N>& map,
				  BitmapBuffer<W * N, H * N>& dest, GroundFn&& ground_of,
				  DecalFn&& decal_of, OverFn&& over_of, BetweenFn&& between) {
	render_layered_map(
		map, eng::Span<eng::u8> {dest.pixels, eng::usize(W * N) * (H * N)}, eng::u16(W * N),
		ground_of, decal_of, over_of,
		layered_detail::BetweenOverSpan<BitmapBuffer<W * N, H * N>, BetweenFn> {&dest,
										      &between});
}

/// \copydoc render_layered_map
/// Atajo sin actores (no hay callback «entre capas»).
/// \tparam W,H,N geometría. \tparam GroundFn,DecalFn,OverFn proveedores por capa.
/// \param map mapa. \param dest destino. \param ground_of,decal_of,over_of proveedores.
template <eng::u16 W, eng::u16 H, eng::u16 N, class GroundFn, class DecalFn, class OverFn>
constexpr void render_layered_map(const LayeredTileMap<W, H, N>& map,
				  BitmapBuffer<W * N, H * N>& dest, GroundFn&& ground_of,
				  DecalFn&& decal_of, OverFn&& over_of) {
	render_layered_map(map, dest, ground_of, decal_of, over_of, NoBetweenRender {});
}

// ---------------------------------------------------------------------------
//  Variante para framebuffers de runtime (`Span` + `stride`, sin geometría fija)
// ---------------------------------------------------------------------------

/// **Compone el mapa por capas** en un framebuffer de runtime: `dest` es la vista del
/// buffer chunky (filas de `stride` píxeles; el de `eng::IndexedDisplay::framebuffer()`).
/// Mismo contrato de capas y orden que la variante `BitmapBuffer`
/// (suelo → decal → `between` → overhead); `between` recibe la vista completa.
/// \tparam W,H,N geometría. \tparam GroundFn,DecalFn,OverFn proveedores por capa.
/// \tparam BetweenFn `void(eng::Span<eng::u8>)`. \param map mapa. \param dest framebuffer.
/// \param stride píxeles por fila. \param ground_of,decal_of,over_of proveedores.
/// \param between actores.
template <eng::u16 W, eng::u16 H, eng::u16 N, class GroundFn, class DecalFn, class OverFn,
	  class BetweenFn>
constexpr void render_layered_map(const LayeredTileMap<W, H, N>& map, eng::Span<eng::u8> dest,
				  eng::u16 stride, GroundFn&& ground_of, DecalFn&& decal_of,
				  OverFn&& over_of, BetweenFn&& between) {
	for (eng::u16 cy = 0u; cy < H; ++cy) {
		for (eng::u16 cx = 0u; cx < W; ++cx) {
			const eng::u16 g = map.cell(MapLayer::Ground, cx, cy);
			if (g != kEmptyTile) {
				blit_tile(dest, stride, ground_of(g), eng::u16(cx * N), eng::u16(cy * N),
					  false);
			}
		}
	}
	for (eng::u16 cy = 0u; cy < H; ++cy) {
		for (eng::u16 cx = 0u; cx < W; ++cx) {
			const eng::u16 d = map.cell(MapLayer::Decal, cx, cy);
			if (d != kEmptyTile) {
				blit_tile(dest, stride, decal_of(d), eng::u16(cx * N), eng::u16(cy * N),
					  true);
			}
		}
	}
	between(dest);
	for (eng::u16 cy = 0u; cy < H; ++cy) {
		for (eng::u16 cx = 0u; cx < W; ++cx) {
			const eng::u16 o = map.cell(MapLayer::Overhead, cx, cy);
			if (o != kEmptyTile) {
				blit_tile(dest, stride, over_of(o), eng::u16(cx * N), eng::u16(cy * N),
					  true);
			}
		}
	}
}

/// \copydoc render_layered_map
/// Atajo sin actores de la variante `Span`.
/// \tparam W,H,N geometría. \tparam GroundFn,DecalFn,OverFn proveedores por capa.
/// \param map mapa. \param dest framebuffer. \param stride píxeles por fila.
/// \param ground_of,decal_of,over_of proveedores.
template <eng::u16 W, eng::u16 H, eng::u16 N, class GroundFn, class DecalFn, class OverFn>
constexpr void render_layered_map(const LayeredTileMap<W, H, N>& map, eng::Span<eng::u8> dest,
				  eng::u16 stride, GroundFn&& ground_of, DecalFn&& decal_of,
				  OverFn&& over_of) {
	render_layered_map(map, dest, stride, ground_of, decal_of, over_of, NoBetweenRender {});
}

} // namespace eng::gfx::proc
