#pragma once

/// \file terrain.hpp
/// **Generador de terreno modular** (`eng::gfx::proc::terrain`) — cabecera paraguas: los
/// tipos (`terrain_types.hpp`), el pintor (`terrain_paint.hpp`), los catálogos de piezas
/// y el contenedor `TerrainTileSet`. Piezas tipo Carcassonne/Wang — cada una declara un
/// **borde tipado** por lado (`Edge`) y unos **rasgos** de interior (`Feature`) — que el
/// pintor compone muestreando el `TextureAtlas` de materiales. Uniendo piezas se forman
/// ríos continuos, lagos, sierras, bosques, aldeas, murallas y castillos completos.
///
/// ```text
///   TileSpec { edges = {n,e,s,w: Edge}, features = Feature mask, seed }
///        │
///        ▼   paint_terrain_tile (muestrea el atlas + sellos de shapes.hpp)
///   ┌─────────────────────────┐
///   │ base (hierba/cultivo/…) │  1. material base por rasgo dominante
///   │ + crop rows             │  2. rasgos en orden de pintor:
///   │ + lake (+Foam)          │     cultivo → lago → río(+puente) → árboles → rocas →
///   │ + river (+Foam/Bridge)  │     montaña → casa → cueva → muralla → torre → puerta →
///   │ + trees/rocks/mountain  │     musgo del patio
///   │ + house/cave/wall/tower │
///   │ + yard moss             │
///   │ + transición de bordes  │  3. franjas con rampa+dither por los Edge no estructurales
///   └─────────────────────────┘
/// ```
///
/// La **continuidad** entre vecinos está garantizada por construcción: el agua entra y
/// sale por el centro exacto del borde (dos tramos consecutivos casan), la muralla se
/// centra en el borde, los ids casan (`edges_match`) y el borde suave usa una rampa
/// idéntica en ambos lados (el dither Bayer es espacial y determinista).
///
/// ## Escalar
///
/// El pintor es una plantilla sobre `S` (el escalar del atlas): en Amiga `MiniFloat16`;
/// en host, `float`. Todo el coste es de **carga** (generación de tiles), nunca por frame.
///
/// ## Estado de verificación: VERIFICADA (demo 119)
/// `TerrainTileSet`/`add_variant`/`can_place` se ejercitan en hardware (colocación del
/// mundo en carga); tests host `HOST-442`/`HOST-443`.
/// Diseño y uso: `docs/engine/architecture/PROCEDURAL_BITMAPS.md`.

#include <eng/core/math/random.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/terrain_paint.hpp>
#include <eng/graphics/proc/terrain_types.hpp>

namespace eng::gfx::proc::terrain {

/// Catálogo de piezas de **castillo / muralla** (rectos, esquinas, torres, puerta, patio
/// y muralla con camino de ronda). Los bordes `Wall` casan entre piezas: colocándolas
/// según `can_place` se cierra un castillo con torres en las esquinas.
inline constexpr TileSpec kCastlePieces[] = {
	// Rectos: muralla vertical (N/S) y horizontal (E/W).
	{.edges = {Edge::Wall, Edge::Grass, Edge::Wall, Edge::Grass}, .features = Feature::WallSeg},
	{.edges = {Edge::Grass, Edge::Wall, Edge::Grass, Edge::Wall}, .features = Feature::WallSeg},
	// Esquinas (dos lados adyacentes de muro).
	{.edges = {Edge::Wall, Edge::Wall, Edge::Grass, Edge::Grass}, .features = Feature::WallSeg},
	{.edges = {Edge::Wall, Edge::Grass, Edge::Grass, Edge::Wall}, .features = Feature::WallSeg},
	{.edges = {Edge::Grass, Edge::Wall, Edge::Wall, Edge::Grass}, .features = Feature::WallSeg},
	{.edges = {Edge::Grass, Edge::Grass, Edge::Wall, Edge::Wall}, .features = Feature::WallSeg},
	// Torre de esquina (cuatro muros) y torre intermedia (recta con torre).
	{.edges = {Edge::Wall, Edge::Wall, Edge::Wall, Edge::Wall},
	 .features = Feature::Tower | Feature::WallSeg},
	{.edges = {Edge::Wall, Edge::Grass, Edge::Wall, Edge::Grass}, .features = Feature::Tower},
	// Puerta (muro horizontal con hueco).
	{.edges = {Edge::Grass, Edge::Wall, Edge::Grass, Edge::Wall},
	 .features = Feature::Gate | Feature::WallSeg},
	// Patio interior (rodeado de muralla).
	{.edges = {Edge::Wall, Edge::Wall, Edge::Wall, Edge::Wall}, .features = Feature::CastleYard},
	// Muralla con camino de ronda (muro N/S, camino E/W).
	{.edges = {Edge::Wall, Edge::Path, Edge::Wall, Edge::Path}, .features = Feature::WallSeg},
};

/// Catálogo de piezas de **río**: rectos, curvas, cruce/ensanche y puente (agua N/S con
/// camino E/W). El cauce entra/sale por el centro del borde, así que encadena entre
/// piezas (y con las de lago de `kNaturePieces`).
inline constexpr TileSpec kRiverPieces[] = {
	// Recto vertical (N-S) y horizontal (E-W).
	{.edges = {Edge::Water, Edge::Grass, Edge::Water, Edge::Grass}, .features = Feature::River},
	{.edges = {Edge::Grass, Edge::Water, Edge::Grass, Edge::Water}, .features = Feature::River},
	// Curvas (dos lados adyacentes de agua).
	{.edges = {Edge::Water, Edge::Water, Edge::Grass, Edge::Grass}, .features = Feature::River},
	{.edges = {Edge::Water, Edge::Grass, Edge::Grass, Edge::Water}, .features = Feature::River},
	// Cruce / ensanche (cuatro bordes de agua).
	{.edges = {Edge::Water, Edge::Water, Edge::Water, Edge::Water},
	 .features = Feature::River | Feature::Lake},
	// Puente (agua N/S, camino E/W).
	{.edges = {Edge::Water, Edge::Path, Edge::Water, Edge::Path},
	 .features = Feature::River | Feature::Bridge},
};

/// Catálogo de piezas de **naturaleza y aldea**: hierba llana, camino, bosques,
/// peñascos, sierras, lago, cultivos, casas y cueva.
inline constexpr TileSpec kNaturePieces[] = {
	// Hierba llana (borde todo `Grass`): el relleno base del mapa.
	{.features = Feature::None},
	// Camino horizontal (borde E/W `Path`; la franja de transición lo dibuja): la
	// versión vertical es un `apply_transform(Rot90)` que deriva el colocador.
	{.edges = {Edge::Grass, Edge::Path, Edge::Grass, Edge::Path}, .features = Feature::None},
	{.features = Feature::Trees},
	{.features = Feature::Trees | Feature::Rocks},
	{.edges = {Edge::Forest, Edge::Forest, Edge::Grass, Edge::Grass}, .features = Feature::Trees},
	{.features = Feature::Mountain},
	{.edges = {Edge::Ridge, Edge::Grass, Edge::Ridge, Edge::Grass}, .features = Feature::Mountain},
	{.features = Feature::Lake},
	{.features = Feature::Crops},
	{.edges = {Edge::Crop, Edge::Crop, Edge::Grass, Edge::Grass}, .features = Feature::Crops},
	{.features = Feature::House | Feature::Crops},
	{.features = Feature::House | Feature::Trees},
	{.edges = {Edge::Cave, Edge::Grass, Edge::Grass, Edge::Grass},
	 .features = Feature::CaveMouth | Feature::Rocks},
};

/// **Set de piezas de terreno**: los catálogos pintados + variaciones extra, con
/// `can_place` para colocarlas por casación de bordes. Los tiles se generan una vez (en
/// carga); el mapa solo guarda índices a este set (sin heap: capacidad fija).
/// \tparam TW,TH geometría del tile. \tparam MaxTiles capacidad fija del set.
template <eng::u16 TW, eng::u16 TH, eng::u16 MaxTiles>
struct TerrainTileSet {
	/// Buffers de las piezas disponibles (índices de la paleta del atlas).
	BitmapBuffer<TW, TH> tiles[MaxTiles] {};
	/// Especificación de cada pieza (bordes, rasgos y seed asignado).
	TileSpec specs[MaxTiles] {};
	/// Piezas generadas (`<= MaxTiles`).
	eng::u16 count = 0;

	/// Añade una pieza: asigna su seed desde `rng`, la pinta y la guarda. Sin efecto si el
	/// set está lleno. La posición `(tile_x, tile_y)` de variantes se reparte por índice
	/// (`count % 8`, `count / 8`) para que cada pieza use variantes distintas del atlas.
	/// \tparam S escalar del atlas. \param s especificación (su `seed` se reasigna).
	/// \param atlas atlas de materiales. \param border_w ancho de la franja de borde.
	/// \param rng RNG del set (determinista).
	template <class S>
	void add_piece(TileSpec s, const TextureAtlas<TW, TH, S>& atlas, eng::u8 border_w,
		       eng::Xoroshiro64pp& rng) {
		if (count >= MaxTiles) return;
		s.seed = rng.next();
		const eng::u16 tx = eng::u16(count % 8u);
		const eng::u16 ty = eng::u16(count / 8u);
		specs[count] = s;
		paint_terrain_tile(tiles[count], s, atlas, tx, ty, border_w);
		++count;
	}

	/// Añade una **variante transformada** de una pieza base (rotada/reflejada) reutilizando
	/// su pintado: el píxel va con `apply_transform` y los bordes con `transform_edges`, así
	/// que las conexiones se conservan sin volver a generar ruido (memoria y CPU ÷8 por
	/// variante). Es lo que usa el colocador para orientar curvas de río y caminos.
	/// \param base índice de la pieza base (`count` si no hay variante que crear).
	/// \param t transformación a aplicar (`Identity` devuelve `base` sin añadir nada).
	/// \return índice de la variante (nuevo) o `base` si no se creó.
	[[nodiscard]] eng::u16 add_variant(eng::u16 base, Transform t) {
		static_assert(TW == TH, "las variantes rotadas/reflejadas exigen tiles cuadrados");
		if (t == Transform::Identity || base >= count || count >= MaxTiles) return base;
		BitmapBuffer<TW, TH> out {};
		apply_transform(tiles[base], out, t);
		tiles[count] = out;
		specs[count] = specs[base];
		specs[count].edges = transform_edges(specs[base].edges, t);
		++count;
		return eng::u16(count - 1u);
	}

	/// Genera el set completo: catálogos de castillo, río y naturaleza + 12 variaciones
	/// extra de naturaleza con seeds distintos (más riqueza sin piezas nuevas).
	/// \tparam S escalar del atlas. \param seed semilla del set (mismo seed = mismo set).
	/// \param atlas atlas ya generado. \param border_w ancho de la franja de borde (4/6/8).
	template <class S>
	void generate(eng::u32 seed, const TextureAtlas<TW, TH, S>& atlas, eng::u8 border_w = 6u) {
		eng::Xoroshiro64pp rng {seed, seed ^ 0xA11A5u};
		count = 0;
		for (const TileSpec& s : kCastlePieces) add_piece(s, atlas, border_w, rng);
		for (const TileSpec& s : kRiverPieces) add_piece(s, atlas, border_w, rng);
		for (const TileSpec& s : kNaturePieces) add_piece(s, atlas, border_w, rng);
		constexpr eng::u32 nature_n = sizeof(kNaturePieces) / sizeof(kNaturePieces[0]);
		for (eng::u8 i = 0; i < 12u && count < MaxTiles; ++i) {
			add_piece(kNaturePieces[rng.next_mod(nature_n)], atlas, border_w, rng);
		}
	}

	/// ¿La pieza `b` puede colocarse al lado `side` de la pieza `a`? (casación de ids).
	/// \param a índice de la pieza de referencia. \param b índice de la candidata.
	/// \param side lado de `a` por el que tocaría `b`. \return true si los ids casan.
	[[nodiscard]] bool can_place(eng::u16 a, eng::u16 b, Side side) const {
		return a < count && b < count && edges_match(specs[a].edges, specs[b].edges, side);
	}

	/// Índice de la primera pieza que cumple `pred(spec)` (búsqueda lineal; apoyo para el
	/// colocador de mapas y los tests). \tparam Pred callable `bool(const TileSpec&)`.
	/// \param pred predicado. \return índice o `count` si ninguna cumple.
	template <class Pred>
	[[nodiscard]] eng::u16 find(Pred&& pred) const {
		for (eng::u16 i = 0; i < count; ++i) {
			if (pred(specs[i])) return i;
		}
		return count;
	}
};

} // namespace eng::gfx::proc::terrain
