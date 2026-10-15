#pragma once

/// \file terrain_biome.hpp
/// **Mundo de biomas por máscaras** (`eng::gfx::proc::terrain`): la evolución del
/// colocador por catálogo a un **generador con conocimiento explícito de compatibilidad**.
/// Aquí los «lados» de un tile son **materiales de campo** (agua, arena, hierba, tierra,
/// roca) más la red de camino; la compatibilidad es la **igualdad de material** y la
/// gramática `biome_adjacent` (el agua no toca directamente tierra ni roca: el generador
/// interpone arena). El set ya **no enumera piezas**: pinta la pieza de CUALQUIER máscara
/// que el mapa necesite, memoizada y deduplicada por rotaciones — el catálogo pasa de 45
/// piezas fijas a «todas las que el mundo use» (~decenas por mapa).
///
/// ```text
///   campos (ruido entero mulu.w + río + camino)     →  MaterialGrid<W,H>
///   gramática de adyacencia (sand entre agua y X)   →  reparación local
///   por celda: 4 lados = material del vecino        →  BiomeTileSet::tile_for (memo + giro)
///   = mapa coherente por construcción (todo lado casa con su vecino)
/// ```
///
/// El pintor reutiliza el del catálogo (base + franjas de transición + red de camino +
/// rasgos: árboles, casa, cueva, montaña, muralla…): las **franjas** funden el material
/// del vecino con la rampa y el dither (idénticas en ambos lados de la junta), el **río**
/// entra por el centro del borde (dos tramos casan) y los rasgos escalan con `TW`.
///
/// ## Las cuatro piezas del sistema
///
/// - `Biome`/`biome_adjacent`: el **alfabeto** de materiales y la gramática de contactos.
/// - `BiomeSpec`: qué pide una celda (material central, material de cada lado, rasgos).
/// - `BiomeTileSet::tile_for`: **memoiza** la pieza de una máscara (canónica por rotación)
///   y devuelve el índice en la orientación pedida (variante girada barata).
/// - `generate_biome_world`: el **generador** (campos → gramática → camino → castillo →
///   dispersión → máscaras → layout) con la comprobación final `biome_world_connected`.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-445`); la demo 119 lo estrena en hardware.

#include <eng/core/math/random.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/noise_tile.hpp>
#include <eng/graphics/proc/terrain_paint.hpp>
#include <eng/graphics/proc/terrain_place.hpp>
#include <eng/graphics/proc/terrain_types.hpp>
#include <eng/graphics/proc/tiles.hpp>

namespace eng::gfx::proc::terrain {

/// **Materiales de campo** del mundo (los ids de los cuatro lados). El camino no es un
/// material: es una red que se superpone (valor `kBiomePath` en los lados).
enum class Biome : eng::u8 {
	Water = 0,
	Sand,
	Grass,
	Dirt,
	Rock,
	Count,
};

/// Número de materiales de campo (índice reservado `Count`).
inline constexpr eng::u8 kBiomeCount = eng::u8(Biome::Count);

/// Valor de lado «camino» (red sobre el material; `kBiomeCount` = primer id libre).
inline constexpr eng::u8 kBiomePath = kBiomeCount;

/// Material del atlas que representa un bioma (mapping explícito: el orden de `Biome`
/// —campo del mundo— no coincide con el de `MaterialId` —catálogo del atlas—; `Grass` es
/// 0 en `MaterialId`).
/// \param b bioma. \return `MaterialId` homónimo.
[[nodiscard]] constexpr MaterialId biome_material(Biome b) {
	switch (b) {
	case Biome::Water: return MaterialId::Water;
	case Biome::Sand: return MaterialId::Sand;
	case Biome::Grass: return MaterialId::Grass;
	case Biome::Dirt: return MaterialId::Dirt;
	case Biome::Rock: return MaterialId::Rock;
	default: return MaterialId::Grass;
	}
}

/// **Gramática de adyacencia**: ¿los materiales `a` y `b` pueden tocarse por un lado?
/// Es el «conocimiento de compatibilidad» del generador: el agua solo toca arena (playa)
/// o hierba (orilla directa); el contacto agua–tierra y agua–roca se repara interponiendo
/// arena. El resto de pares son compatibles.
/// \param a,b materiales. \return true si pueden compartir lado.
[[nodiscard]] constexpr bool biome_adjacent(Biome a, Biome b) {
	if (a == b) return true;
	const bool water = a == Biome::Water || b == Biome::Water;
	const bool earthy = a == Biome::Dirt || a == Biome::Rock || b == Biome::Dirt || b == Biome::Rock;
	return !(water && earthy);
}

/// Especificación de una pieza de bioma: material central (`base`), material de cada lado
/// en orden N/E/S/W (valores `Biome` o `kBiomePath`), rasgos (`Feature`) y, para las
/// piezas de orientación (muralla/puerta), los bordes `Edge` que el pintor clásico usa
/// para decidir la orientación (`feat_edges`).
struct BiomeSpec {
	eng::u8 base = 0u;
	/// Valor de lado que esta celda **expone** a sus vecinos (`Biome` o `kBiomePath`): el
	/// vecino guarda este valor en su lado correspondiente — la juntura casa por igualdad.
	eng::u8 id = 0u;
	eng::u8 sides[4] {0u, 0u, 0u, 0u};
	Feature features = Feature::None;
	EdgeSides<Edge> feat_edges {};
	eng::u32 seed = 0u;
};

/// Índice canónico de las rotaciones (Rot90 gira los lados N→E→S→W, como `transform_edges`).
/// \param spec especificación a girar. \param rot nº de Rot90 (0..3). \return copia girada.
[[nodiscard]] constexpr BiomeSpec rotate_biome_spec(const BiomeSpec& spec, eng::u8 rot) {
	BiomeSpec out = spec;
	for (eng::u8 r = 0; r < (rot & 3u); ++r) {
		const eng::u8 n = out.sides[0];
		out.sides[0] = out.sides[3];
		out.sides[3] = out.sides[2];
		out.sides[2] = out.sides[1];
		out.sides[1] = n;
		out.feat_edges = transform_edges(out.feat_edges, Transform::Rot90);
	}
	return out;
}

/// ¿Dos especificaciones describen la misma pieza (material, lados, id expuesto y rasgos)?
/// \param a,b especificaciones. \return true si son idénticas en lo pintable y verificable.
[[nodiscard]] constexpr bool biome_spec_equal(const BiomeSpec& a, const BiomeSpec& b) {
	if (a.base != b.base || a.features != b.features || a.id != b.id) return false;
	for (eng::u8 i = 0u; i < 4u; ++i) {
		if (a.sides[i] != b.sides[i]) return false;
	}
	return true;
}

/// **Set de piezas de bioma**: pinta y memoiza la pieza de una máscara la primera vez que
/// se pide (canónica por rotación: hasta 4 máscaras equivalentes comparten pintado) y
/// devuelve el índice en la orientación pedida. Sustituye al catálogo enumerado: el mapa
/// solo contiene las máscaras que usa, y **cualquier** máscara es representable.
/// \tparam TW,TH geometría del tile (16×16 típico). \tparam MaxTiles capacidad fija.
template <eng::u16 TW, eng::u16 TH, eng::u16 MaxTiles>
struct BiomeTileSet {
	/// Buffers de las piezas instanciadas (índices de la paleta del atlas).
	BitmapBuffer<TW, TH> tiles[MaxTiles] {};
	/// Especificación de cada pieza (los índices del layout pueden venir girados).
	BiomeSpec specs[MaxTiles] {};
	/// Variantes giradas memoizadas por pieza **canónica**: `variant[c][k]` = índice de la
	/// pieza girada `k·90°` (0 = aún no creada).
	eng::u16 variant[MaxTiles][4] {};
	/// Pieza canónica de cada índice (un índice de variante apunta a su canónica).
	eng::u16 canon_of[MaxTiles] {};
	/// Giro acumulado de cada índice respecto a su canónica (0..3).
	eng::u8 rot_of[MaxTiles] {};
	/// Piezas instanciadas (`<= MaxTiles`).
	eng::u16 count = 0u;
	/// Peticiones truncadas por falta de capacidad (fallo de forma visible).
	eng::u16 overflow = 0u;

	/// Devuelve el índice de la pieza para `(base, sides, features)` en la orientación
	/// pedida. Si la máscara (canónica por rotación, solo sin rasgos) ya está pintada,
	/// reutiliza su pintado con una variante girada; si no, la pinta con un seed derivado
	/// de la propia máscara (determinista, independiente del orden de petición).
	/// \tparam S escalar del atlas. \param base material central.
	/// \param sides material de cada lado (N/E/S/W; `Biome` o `kBiomePath`).
	/// \param features rasgos de la pieza (0 = tile de campo). \param feat_edges bordes
	///        para la orientación de los rasgos (muralla/puerta). \param self_id valor que
	///        la celda expone a sus vecinos (`Biome` o `kBiomePath`). \param atlas atlas.
	/// \param border_w ancho de franja de transición (4/6/8). \return índice de la pieza
	/// (memoizada o recién pintada; `0` truncada si el set se llenó).
	template <class S>
	constexpr eng::u16 tile_for(eng::u8 base, const eng::u8 sides[4], Feature features,
			  EdgeSides<Edge> feat_edges, eng::u8 self_id,
			  const TextureAtlas<TW, TH, S>& atlas, eng::u8 border_w = 4u) {
		BiomeSpec want {};
		want.base = base;
		want.id = self_id;
		for (eng::u8 i = 0u; i < 4u; ++i) want.sides[i] = sides[i];
		want.features = features;
		want.feat_edges = feat_edges;
		// Búsqueda sobre las piezas CANÓNICAS (una entrada por máscara pintada): si la
		// máscara pedida es un giro de una existente, se resuelve a su canónica y se pide
		// la variante girada (el giro se compone con el de la variante, sin cadenas).
		for (eng::u16 c = 0u; c < count; ++c) {
			if (canon_of[c] != c) continue; // solo canónicas
			if (specs[c].base != base || specs[c].features != features) continue;
			for (eng::u8 k = 0u; k < 4u; ++k) {
				const BiomeSpec rot = rotate_biome_spec(want, k);
				if (!biome_spec_equal(specs[c], rot)) continue;
				const eng::u8 back = eng::u8((4u - k) & 3u);
				if (back == 0u) return c;
				return variant_of(c, back, atlas);
			}
		}
		if (count >= MaxTiles) {
			++overflow;
			return 0u;
		}
		const eng::u16 idx = count;
		specs[idx] = want;
		specs[idx].seed = biome_seed(want);
		specs[idx].base = base;
		canon_of[idx] = idx;
		rot_of[idx] = 0u;
		paint_biome_tile(tiles[idx], specs[idx], atlas, border_w);
		++count;
		return idx;
	}

private:
	/// Instancia (una vez) la variante girada `rot·90°` de la pieza canónica `c` (píxeles
	/// con `apply_transform`, lados y bordes girados en la especificación). El índice de la
	/// canónica y el giro quedan anotados (`canon_of`/`rot_of`) para que una petición de
	/// otra orientación no encadene variantes.
	/// \tparam S escalar del atlas. \param c pieza canónica. \param rot 1..3 giros.
	/// \param atlas atlas (no se usa al girar; firma por simetría con `tile_for`).
	/// \return índice de la variante (o `c` si no hay capacidad).
	template <class S>
	constexpr eng::u16 variant_of(eng::u16 c, eng::u8 rot, const TextureAtlas<TW, TH, S>& atlas) {
		(void)atlas;
		if (variant[c][rot] != 0u) return eng::u16(variant[c][rot] - 1u);
		if (count >= MaxTiles) {
			++overflow;
			return c;
		}
		const eng::u16 idx = count;
		apply_transform(tiles[c], tiles[idx], transform_for(rot));
		specs[idx] = rotate_biome_spec(specs[c], rot);
		canon_of[idx] = c;
		rot_of[idx] = rot;
		variant[c][rot] = eng::u16(idx + 1u);
		++count;
		return idx;
	}

	/// Transformación de `rot·90°` (1 = `Rot90`…).
	/// \param rot 1..3. \return transformación de `apply_transform`.
	[[nodiscard]] static constexpr Transform transform_for(eng::u8 rot) {
		return rot == 1u ? Transform::Rot90
				 : (rot == 2u ? Transform::Rot180 : Transform::Rot270);
	}

	/// Seed determinista de una máscara (FNV-1a de 32 bits sobre base+lados+rasgos): la
	/// misma máscara pinta igual aunque cambie el orden de petición.
	/// \param spec especificación. \return semilla.
	[[nodiscard]] static constexpr eng::u32 biome_seed(const BiomeSpec& spec) {
		eng::u32 h = 0x811C9DC5u ^ (eng::u32(spec.base) * 2654435761u);
		for (eng::u8 i = 0u; i < 4u; ++i) {
			h = (h ^ spec.sides[i]) * 16777619u;
		}
		return h ^ (eng::u32(eng::util::to_underlying(spec.features)) * 2246822519u);
	}
};

// Declaraciones anticipadas de los auxiliares del pintor (se definen más abajo, tras el
// pintor principal, para leer el fichero en el orden del algoritmo).
template <eng::u16 TW, eng::u16 TH>
constexpr void paint_biome_path(BitmapBuffer<TW, TH>& buf, const TileSpec& spec);
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_shore(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, Biome land,
		 eng::u8 side, eng::u32 seed);
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_bank(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, Biome land,
		eng::u8 side, eng::Xoroshiro64pp& rng, eng::u32 seed);
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void blend_side(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, MaterialId m,
		eng::u8 side, eng::u8 border_w, eng::u32 seed);
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_biome_features(BitmapBuffer<TW, TH>& buf, const BiomeSpec& spec,
			  const TextureAtlas<TW, TH, S>& atlas, eng::Xoroshiro64pp& rng);

/// **Pinta una pieza de bioma** (ver el diagrama del fichero): base del material central,
/// franjas de transición por lado con el material del vecino (arena/hierba/tierra/roca),
/// orilla+espuma cuando el lado es agua (o el centro es agua), red de camino sobre lo
/// anterior (plana, como el catálogo) y rasgos (árboles, roca, montaña, casa, cueva,
/// muralla, torre, puerta, patio, cultivo, puente) con las geometrías proporcionales.
/// Todo el azar sale de `spec.seed` (misma máscara = mismo tile en host y Amiga).
/// \tparam TW,TH geometría. \tparam S escalar del atlas. \param buf destino.
/// \param spec pieza (material central + lados + rasgos). \param atlas atlas de materiales.
/// \param border_w ancho de la franja de transición (4/6/8).
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_biome_tile(BitmapBuffer<TW, TH>& buf, const BiomeSpec& spec,
		      const TextureAtlas<TW, TH, S>& atlas, eng::u8 border_w = 4u) {
	eng::Xoroshiro64pp rng {spec.seed, spec.seed ^ 0xB10Bu};
	const Biome base = Biome(spec.base);
	const eng::u8 base_variant =
		atlas.variant_at(eng::u16(spec.seed), eng::u16(spec.seed >> 16u), biome_material(base));
	copy_material(buf, atlas, biome_material(base), base_variant);
	// Franjas de transición de campo↔campo (rampa+dither, idéntica en ambos lados).
	for (eng::u8 i = 0u; i < 4u; ++i) {
		const eng::u8 s = spec.sides[i];
		if (s == spec.base || s == kBiomePath) continue;
		const Biome b = Biome(s);
		if (base == Biome::Water) {
			paint_shore(buf, atlas, b, i, spec.seed);
		} else if (b == Biome::Water) {
			paint_bank(buf, atlas, base, i, rng, spec.seed);
		} else {
			blend_side(buf, atlas, biome_material(b), i, border_w, spec.seed);
		}
	}
	// Red de camino: se apoya en `paint_path` con un `TileSpec` temporal (los lados con
	// `kBiomePath` se marcan como `Edge::Path`; los demás no pintan camino) y después se
	// recortan los **topes** al borde: la cápsula de `paint_path` termina en media luna
	// (bulto redondeado) que a 16×16 se lee como un arco; el tope recto hace que la franja
	// salga del tile como una banda continua hacia el vecino.
	{
		TileSpec ts {};
		ts.edges.n = spec.sides[0] == kBiomePath ? Edge::Path : Edge::Grass;
		ts.edges.e = spec.sides[1] == kBiomePath ? Edge::Path : Edge::Grass;
		ts.edges.s = spec.sides[2] == kBiomePath ? Edge::Path : Edge::Grass;
		ts.edges.w = spec.sides[3] == kBiomePath ? Edge::Path : Edge::Grass;
		// Solo las celdas QUE SON camino pintan la red (las franjas del camino viven dentro
		// del propio camino; el vecino de hierba no lleva tope, como en los juegos).
		const bool any = spec.id == kBiomePath && (ts.edges.n == Edge::Path ||
							   ts.edges.e == Edge::Path ||
							   ts.edges.s == Edge::Path ||
							   ts.edges.w == Edge::Path);
		if (any) {
			paint_biome_path(buf, ts);
		}
	}
	paint_biome_features(buf, spec, atlas, rng);
}

/// Pinta la **red de camino de bioma**: por cada lado `Path`, una banda **rectangular**
/// del color plano del tramo `Path` (borde oscuro y núcleo claro) desde ese borde hasta el
/// centro del tile. Rectangular a propósito: la cápsula con final redondeado del camino
/// clásico se lee como un «arco» a 16×16; la banda recta encadena con el vecino como una
/// calle. Los pares opuestos forman la recta, dos adyacentes el codo (las bandas se
/// encuentran en el centro) y tres/cuatro la T/cruce.
/// \tparam TW,TH geometría. \param buf destino. \param spec pieza temporal con lados
/// `Edge::Path` en los contactos (los demás no pintan).
template <eng::u16 TW, eng::u16 TH>
constexpr void paint_biome_path(BitmapBuffer<TW, TH>& buf, const TileSpec& spec) {
	const MaterialDesc pd = material_desc(MaterialId::Path);
	const eng::u8 core = eng::u8(pd.base + pd.levels - 1u);
	const eng::u8 edge = pd.base;
	const eng::u16 e4 = eng::u16(TW * 4u >> 5u);
	const eng::u16 se = e4 < 3u ? 3u : e4; // semi-ancho del borde oscuro
	const eng::u16 sc = eng::u16(se - 1u); // semi-ancho del núcleo claro
	const eng::u16 cx = eng::u16(TW / 2u);
	const eng::u16 cy = eng::u16(TH / 2u);
	// Rectángulo (borde→centro) relleno de `col`.
	auto band = [&](eng::u16 xa, eng::u16 xb, eng::u16 ya, eng::u16 yb, eng::u8 col) {
		for (eng::u16 y = ya; y <= yb; ++y) {
			for (eng::u16 x = xa; x <= xb; ++x) buf.at(x, y) = col;
		}
	};
	// Borde oscuro por contacto (hasta el centro), después el núcleo claro encima.
	if (spec.edges.n == Edge::Path) band(eng::u16(cx - se), eng::u16(cx + se), 0u, cy, edge);
	if (spec.edges.s == Edge::Path) {
		band(eng::u16(cx - se), eng::u16(cx + se), cy, eng::u16(TH - 1u), edge);
	}
	if (spec.edges.w == Edge::Path) band(0u, cx, eng::u16(cy - se), eng::u16(cy + se), edge);
	if (spec.edges.e == Edge::Path) {
		band(cx, eng::u16(TW - 1u), eng::u16(cy - se), eng::u16(cy + se), edge);
	}
	if (spec.edges.n == Edge::Path) band(eng::u16(cx - sc), eng::u16(cx + sc), 1u, cy, core);
	if (spec.edges.s == Edge::Path) {
		band(eng::u16(cx - sc), eng::u16(cx + sc), cy, eng::u16(TH - 2u), core);
	}
	if (spec.edges.w == Edge::Path) band(1u, cx, eng::u16(cy - sc), eng::u16(cy + sc), core);
	if (spec.edges.e == Edge::Path) {
		band(cx, eng::u16(TW - 2u), eng::u16(cy - sc), eng::u16(cy + sc), core);
	}
}

/// Estampa la **orilla**: el centro es agua y el lado `i` es tierra → franja del material
/// de tierra pegada al borde (rampa) con una línea de espuma entre agua y orilla.
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param atlas atlas.
/// \param land material de tierra del lado. \param side 0..3 (N/E/S/W). \param seed seed.
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_shore(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, Biome land,
		 eng::u8 side, eng::u32 seed) {
	const eng::u16 sh = eng::u16(TW * 5u >> 5u) < 3u ? 3u : eng::u16(TW * 5u >> 5u);
	const MaterialId lm = biome_material(land);
	const eng::u8 lv = atlas.variant_at(eng::u16(seed), eng::u16(seed >> 8u), lm);
	const eng::u8 fv = atlas.variant_at(eng::u16(seed ^ 1u), eng::u16(seed >> 8u), MaterialId::Foam);
	const BitmapBuffer<TW, TH>& ltex = atlas.get(lm, lv);
	const BitmapBuffer<TW, TH>& foam = atlas.get(MaterialId::Foam, fv);
	for (eng::u16 j = 0u; j < sh + 2u; ++j) {
		const eng::u8 alpha = j < sh ? 255u : 220u; // tierra sólida + espuma casi sólida
		for (eng::u16 k = 0u; k < eng::u16(side < 2u ? TW : TH); ++k) {
			const eng::u16 x = side < 2u ? k : (side == 3u ? j : eng::u16(TW - 1u - j));
			const eng::u16 y = side == 0u ? j : (side == 2u ? eng::u16(TH - 1u - j) : k);
			const eng::u8 tex = j < sh ? ltex.at(x, y) : foam.at(x, y);
			buf.at(x, y) = blend_index(buf.at(x, y), tex, alpha, x, y);
		}
	}
}

/// Estampa la **ribera**: el centro es tierra y el lado `i` es agua → cauce (agua+espuma)
/// que entra por el centro del borde (continúa el cauce del vecino por construcción).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param atlas atlas.
/// \param land material de tierra del centro (elige la variante del atlas). \param side 0..3.
/// \param rng RNG de la pieza (ancho y ondulación). \param seed semilla del cauce.
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_bank(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, Biome land,
		eng::u8 side, eng::Xoroshiro64pp& rng, eng::u32 seed) {
	(void)land;
	eng::u16 wx[1] {};
	eng::u16 wy[1] {};
	if (side == 0u) {
		wx[0] = eng::u16(TW / 2u);
		wy[0] = 0u;
	} else if (side == 1u) {
		wx[0] = eng::u16(TW - 1u);
		wy[0] = eng::u16(TH / 2u);
	} else if (side == 2u) {
		wx[0] = eng::u16(TW / 2u);
		wy[0] = eng::u16(TH - 1u);
	} else {
		wx[0] = 0u;
		wy[0] = eng::u16(TH / 2u);
	}
	const eng::u16 w = eng::u16(1u + (TW * 2u >> 5u) + rng.next_mod(2u)); // semi-ancho 2..3
	stamp_course(buf, atlas, MaterialId::Foam, 0u, wx, wy, 1u, eng::u16(w + 1u), seed, 2u);
	stamp_course(buf, atlas, MaterialId::Water, 0u, wx, wy, 1u, w, seed, 2u);
}

/// Mezcla el material `m` en la franja del lado `side` con la rampa y el dither
/// (equivalente por material a `apply_edge_transition`, que va por `Edge`).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param atlas atlas.
/// \param m material del vecino. \param side 0..3 (N/E/S/W). \param border_w ancho.
/// \param seed semilla (elige variante del material).
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void blend_side(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, MaterialId m,
		eng::u8 side, eng::u8 border_w, eng::u32 seed) {
	const eng::u8* ramp = kRamp6.t;
	eng::u8 bw = 6u;
	if (border_w <= 4u) {
		ramp = kRamp4.t;
		bw = 4u;
	} else if (border_w > 6u) {
		ramp = kRamp8.t;
		bw = 8u;
	}
	if (eng::u16(bw) * 2u > TW || eng::u16(bw) * 2u > TH) return;
	const eng::u8 v = atlas.variant_at(eng::u16(seed), eng::u16(seed >> 8u), m);
	const BitmapBuffer<TW, TH>& tex = atlas.get(m, v);
	for (eng::u16 j = 0u; j < bw; ++j) {
		const eng::u8 alpha = side < 2u ? ramp[eng::u8(bw - 1u - j)] : ramp[eng::u8(j)];
		for (eng::u16 k = 0u; k < eng::u16(side < 2u ? TW : TH); ++k) {
			const eng::u16 x = side < 2u ? k : (side == 3u ? j : eng::u16(TW - 1u - j));
			const eng::u16 y = side == 0u ? j : (side == 2u ? eng::u16(TH - 1u - j) : k);
			buf.at(x, y) = blend_index(buf.at(x, y), tex.at(x, y), alpha, x, y);
		}
	}
}

/// Estampa los rasgos de una pieza de bioma (mismo orden de pintor que el catálogo):
/// cultivo, puente/cauce, árboles, rocas, montaña, casa, cueva, muralla, torre, puerta y
/// patio. Los rasgos de orientación (muralla/puerta) leen `spec.feat_edges`.
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param spec pieza.
/// \param atlas atlas. \param rng RNG de la pieza.
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_biome_features(BitmapBuffer<TW, TH>& buf, const BiomeSpec& spec,
			  const TextureAtlas<TW, TH, S>& atlas, eng::Xoroshiro64pp& rng) {
	TileSpec ts {};
	ts.edges = spec.feat_edges;
	ts.features = spec.features;
	ts.seed = spec.seed;
	const eng::u16 tx = eng::u16(spec.seed);
	const eng::u16 ty = eng::u16(spec.seed >> 16u);
	if (has(spec.features, Feature::Bridge)) {
		// Puente: cauce de agua N/S (semi-ancho 2) + tablones y barandillas sobre el agua.
		eng::u16 wx[2] {eng::u16(TW / 2u), eng::u16(TW / 2u)};
		eng::u16 wy[2] {0u, eng::u16(TH - 1u)};
		stamp_course(buf, atlas, MaterialId::Foam, 0u, wx, wy, 2u, 4u, spec.seed, 1u);
		stamp_course(buf, atlas, MaterialId::Water, 0u, wx, wy, 2u, 3u, spec.seed, 1u);
		const BitmapBuffer<TW, TH>& wood = atlas.get(
			MaterialId::Wood, atlas.variant_at(tx, ty, MaterialId::Wood));
		const BitmapBuffer<TW, TH>& rail = atlas.get(
			MaterialId::Cave, atlas.variant_at(tx, ty, MaterialId::Cave));
		const eng::u16 y0 = eng::u16(TH / 2u >= 2u ? TH / 2u - 2u : 0u);
		const eng::u16 y1b = TH / 2u + 2u < TH ? eng::u16(TH / 2u + 2u)
						       : eng::u16(TH - 1u);
		// Tablero ancho (±4): a 16×16 el puente debe cubrir el cauce y leerse como paso.
		const eng::u16 bx = eng::u16(TW / 2u >= 4u ? TW / 2u - 4u : 0u);
		const eng::u16 ex2 = eng::u16(TW / 2u + 4u < TW ? TW / 2u + 4u : TW - 1u);
		for (eng::u16 y = y0; y <= y1b; ++y) {
			for (eng::u16 x = bx; x <= ex2; ++x) buf.at(x, y) = wood.at(x, y);
		}
		for (eng::u16 x = bx; x <= ex2; ++x) {
			buf.at(x, eng::u16(y0 - 1u)) = rail.at(x, eng::u16(y0 - 1u));
			buf.at(x, eng::u16(y1b + 1u)) = rail.at(x, eng::u16(y1b + 1u));
		}
	}
	if (has(spec.features, Feature::Crops)) paint_crops(buf, atlas, rng, tx, ty);
	if (has(spec.features, Feature::Trees)) paint_trees(buf, atlas, rng, tx, ty);
	if (has(spec.features, Feature::Rocks)) paint_rocks(buf, atlas, rng, tx, ty);
	if (has(spec.features, Feature::Mountain)) paint_mountain(buf, atlas, tx, ty);
	if (has(spec.features, Feature::House)) paint_house(buf, atlas, rng, tx, ty);
	if (has(spec.features, Feature::CaveMouth)) paint_cave_mouth(buf, atlas, tx, ty);
	if (has(spec.features, Feature::WallSeg)) paint_wall(buf, ts, atlas, tx, ty);
	if (has(spec.features, Feature::Tower)) paint_tower(buf, atlas, tx, ty);
	if (has(spec.features, Feature::Gate)) paint_gate(buf, ts, atlas, tx, ty);
	if (has(spec.features, Feature::CastleYard)) paint_yard_moss(buf, ts, atlas, tx, ty);
}

/// **Genera un mundo de biomas** `W×H` con conocimiento de compatibilidad: campos
/// (ruido entero `mulu.w` para parches de tierra y montaña), río de dos celdas con
/// meandros, lago, **playa** por dilatación, gramática `biome_adjacent` (reparación),
/// camino en L hasta la puerta del castillo (puente donde cruza el agua), castillo 3×3,
/// cueva/sierra, aldea, bosques y rocas; por celda, los lados salen del material del
/// vecino y la pieza se instancia en el `BiomeTileSet` (memoria por máscara y giro).
/// \tparam W,H celdas del mapa. \tparam TW,TH,MaxTiles geometría/capacidad del set.
/// \tparam S escalar del atlas. \param out layout a rellenar (índices de pieza).
/// \param set set de biomas (se le añaden las piezas del mundo). \param atlas atlas.
/// \param seed semilla (misma semilla = mismo mundo). \param castle_c0 columna del castillo
///        (0 = automática). \param castle_r0 fila del castillo. \return true si el mundo
///        quedó conectado por construcción (`biome_world_connected`) y no truncó piezas.
template <eng::u16 W, eng::u16 H, eng::u16 TW, eng::u16 TH, eng::u16 MaxTiles, class S>
[[nodiscard]] constexpr bool generate_biome_world(TerrainLayout<W, H>& out,
					BiomeTileSet<TW, TH, MaxTiles>& set,
					const TextureAtlas<TW, TH, S>& atlas, eng::u32 seed,
					eng::u16 castle_c0 = 0u, eng::u16 castle_r0 = 1u) {
	static_assert(W >= 10u && H >= 8u, "el mundo de biomas minimo es 10x8");
	using eng::Xoroshiro64pp;
	Xoroshiro64pp rng {seed, seed ^ 0x81E5u};

	eng::u8 mat[W * H] {};
	for (eng::u16 i = 0u; i < W * H; ++i) mat[i] = eng::u8(Biome::Grass);

	// 1) Río de dos celdas con meandros (sin meandro en la fila del puente ni encima).
	const eng::u16 road_row = eng::u16(H - 2u);
	const eng::u16 cc = castle_c0 != 0u ? castle_c0 : eng::u16(W - 4u);
	eng::u16 row_col[H] {};
	eng::u16 rc = eng::u16(W / 4u);
	eng::u16 next_shift_row = 1u;
	for (eng::u16 r = 0u; r < H; ++r) {
		mat[r * W + rc] = eng::u8(Biome::Water);
		if (rc + 1u < W) mat[r * W + rc + 1u] = eng::u8(Biome::Water);
		row_col[r] = rc;
		const bool shift_ok = r + 1u < H && r != road_row && r + 1u != road_row;
		if (shift_ok && r >= next_shift_row && eng::chance(rng, 1u, 2u)) {
			const bool right = (rng.next() & 1u) != 0u;
			const eng::u16 nc = right ? eng::u16(rc + 1u) : eng::u16(rc - 1u);
			if (nc >= 1u && nc + 1u <= eng::u16(cc - 2u)) {
				mat[r * W + nc] = eng::u8(Biome::Water);
				mat[r * W + nc + 1u] = eng::u8(Biome::Water);
				rc = nc;
				next_shift_row = eng::u16(r + 2u);
			}
		}
	}
	// 2) Lago: charca de radio ~1.5 celdas con contorno ruidoso junto al cauce (fila 4).
	const eng::u16 lx = eng::u16(row_col[4] + 3u < W - 2u ? row_col[4] + 3u : eng::u16(W - 3u));
	const eng::u16 ly = 4u;
	for (eng::u16 y = 0u; y < H; ++y) {
		for (eng::u16 x = 0u; x < W; ++x) {
			const eng::s32 dx = eng::s32(x) - eng::s32(lx);
			const eng::s32 dy = eng::s32(y) - eng::s32(ly);
			const eng::u8 n = tile_noise2(eng::u16(x * TW + TW / 2u),
						      eng::u16(y * TH + TH / 2u), 3u, 8u, eng::u16(seed ^ 7u));
			// r² ≈ 1.7-2.7 con borde ruidoso: charca de 3-4 celdas de diámetro.
			if (dx * dx * 2 + dy * dy * 3 <= 5 + (n >> 6u)) {
				mat[y * W + x] = eng::u8(Biome::Water);
			}
		}
	}
	// 3) Playa: toda tierra junto al agua pasa a arena (dilatación de 1 celda).
	{
		eng::u8 copy[W * H] {};
		for (eng::u16 i = 0u; i < W * H; ++i) copy[i] = mat[i];
		for (eng::u16 y = 0u; y < H; ++y) {
			for (eng::u16 x = 0u; x < W; ++x) {
				if (copy[y * W + x] == eng::u8(Biome::Water)) continue;
				const bool near_water =
					(x > 0u && copy[y * W + x - 1u] == eng::u8(Biome::Water)) ||
					(x + 1u < W && copy[y * W + x + 1u] == eng::u8(Biome::Water)) ||
					(y > 0u && copy[(y - 1u) * W + x] == eng::u8(Biome::Water)) ||
					(y + 1u < H && copy[(y + 1u) * W + x] == eng::u8(Biome::Water));
				if (near_water) mat[y * W + x] = eng::u8(Biome::Sand);
			}
		}
	}
	// 4) Parches de tierra (ruido entero) y sierra (blobs de roca arriba a la izquierda).
	for (eng::u16 y = 0u; y < H; ++y) {
		for (eng::u16 x = 0u; x < W; ++x) {
			if (mat[y * W + x] != eng::u8(Biome::Grass)) continue;
			const eng::u8 n = tile_fbm2(eng::u16(x * TW + TW / 2u),
						    eng::u16(y * TH + TH / 2u), 3u, 8u, 3u,
						    eng::u16(seed ^ 0x33u));
			if (n > 168u && n < 196u) mat[y * W + x] = eng::u8(Biome::Dirt);
			const eng::u8 m = tile_fbm2(eng::u16(x * TW + TW / 2u),
						    eng::u16(y * TH + TH / 2u), 2u, 4u, 2u,
						    eng::u16(seed ^ 0x55u));
			if (x < 6u && y < 4u && m > 150u) mat[y * W + x] = eng::u8(Biome::Rock);
		}
	}
	// 5) Gramática: repara contactos prohibidos (agua–tierra/roca) con arena.
	for (eng::u8 pass = 0u; pass < 2u; ++pass) {
		eng::u8 copy[W * H] {};
		for (eng::u16 i = 0u; i < W * H; ++i) copy[i] = mat[i];
		for (eng::u16 y = 0u; y < H; ++y) {
			for (eng::u16 x = 0u; x < W; ++x) {
				const Biome b = Biome(copy[y * W + x]);
				if (b == Biome::Water) continue;
				const bool bad =
					(x > 0u && !biome_adjacent(b, Biome(copy[y * W + x - 1u]))) ||
					(x + 1u < W &&
					 !biome_adjacent(b, Biome(copy[y * W + x + 1u]))) ||
					(y > 0u && !biome_adjacent(b, Biome(copy[(y - 1u) * W + x]))) ||
					(y + 1u < H &&
					 !biome_adjacent(b, Biome(copy[(y + 1u) * W + x])));
				if (bad) mat[y * W + x] = eng::u8(Biome::Sand);
			}
		}
	}
	// 6) Camino en L: fila `road_row` de oeste a la puerta y ramal vertical hasta ella.
	eng::u8 path[W * H] {};
	const eng::u16 gate_x = eng::u16(cc + 1u);
	for (eng::u16 x = 0u; x < W; ++x) path[road_row * W + x] = 1u;
	for (eng::u16 y = eng::u16(castle_r0 + 3u); y < road_row; ++y) path[y * W + gate_x] = 1u;
	// 7) Estructuras: castillo (anillo 3×3), cueva, sierra, 2 casas, bosques, rocas, cultivo.
	Feature feat[W * H] {};
	struct CastlePiece {
		eng::u16 dx, dy;
		Feature features;
		EdgeSides<Edge> edges;
	};
	const CastlePiece ring[9] = {
		{0u, 0u, Feature::WallSeg, {Edge::Grass, Edge::Wall, Edge::Wall, Edge::Grass}},
		{1u, 0u, Feature::WallSeg, {Edge::Grass, Edge::Wall, Edge::Grass, Edge::Wall}},
		{2u, 0u, Feature::WallSeg, {Edge::Grass, Edge::Grass, Edge::Wall, Edge::Wall}},
		{0u, 1u, Feature::Tower, {Edge::Wall, Edge::Grass, Edge::Wall, Edge::Grass}},
		{2u, 1u, Feature::WallSeg, {Edge::Wall, Edge::Grass, Edge::Wall, Edge::Grass}},
		{0u, 2u, Feature::WallSeg, {Edge::Wall, Edge::Wall, Edge::Grass, Edge::Grass}},
		{1u, 2u, Feature::Gate, {Edge::Grass, Edge::Wall, Edge::Grass, Edge::Wall}},
		{2u, 2u, Feature::WallSeg, {Edge::Wall, Edge::Grass, Edge::Grass, Edge::Wall}},
		{1u, 1u, Feature::None, {Edge::Grass, Edge::Grass, Edge::Grass, Edge::Grass}}, // patio
	};
	for (const CastlePiece& p : ring) {
		if (p.features == Feature::None) continue; // el patio queda con hierba
		feat[(castle_r0 + p.dy) * W + (cc + p.dx)] = p.features;
		mat[(castle_r0 + p.dy) * W + (cc + p.dx)] = eng::u8(Biome::Grass);
	}
	feat[0u] = Feature::CaveMouth | Feature::Rocks;
	feat[W] = Feature::Mountain; // (0,1)
	eng::u8 houses = 2u;
	for (eng::u16 tries = 0u; tries < 64u && houses > 0u; ++tries) {
		const eng::u16 x = eng::u16(2u + rng.next_mod(W - 4u));
		const eng::u16 y = eng::u16(4u + rng.next_mod(eng::u16(H - 6u)));
		if (mat[y * W + x] != eng::u8(Biome::Grass) || feat[y * W + x] != Feature::None) continue;
		if (x >= eng::u16(cc - 1u) || (y == road_row && x < eng::u16(gate_x + 1u))) continue;
		feat[y * W + x] = Feature::House;
		--houses;
	}
	for (eng::u8 c = 0u; c < 5u; ++c) { // bosques: 5 núcleos con 2-4 árboles
		const eng::u16 x = eng::u16(1u + rng.next_mod(W - 2u));
		const eng::u16 y = eng::u16(1u + rng.next_mod(H - 2u));
		const eng::u8 n = eng::u8(2u + rng.next_mod(3u));
		for (eng::u8 k = 0u; k < n; ++k) {
			const eng::u16 tx = eng::u16(x + rng.next_mod(3u) - (rng.next_mod(3u) & 1u));
			const eng::u16 ty = eng::u16(y + rng.next_mod(3u) - (rng.next_mod(3u) & 1u));
			if (tx >= W || ty >= H || tx < 2u || ty < 2u) continue;
			if (mat[ty * W + tx] != eng::u8(Biome::Grass) ||
			    feat[ty * W + tx] != Feature::None) {
				continue;
			}
			if (path[ty * W + tx] != 0u) continue;
			feat[ty * W + tx] = Feature::Trees;
		}
	}
	for (eng::u8 c = 0u; c < 4u; ++c) { // rocas sueltas
		const eng::u16 x = eng::u16(2u + rng.next_mod(W - 4u));
		const eng::u16 y = eng::u16(4u + rng.next_mod(H - 6u));
		if (mat[y * W + x] == eng::u8(Biome::Grass) && feat[y * W + x] == Feature::None &&
		    path[y * W + x] == 0u) {
			feat[y * W + x] = Feature::Rocks;
		}
	}
	{ // campo de cultivo 2×2
		const eng::u16 x = eng::u16(3u + rng.next_mod(W / 2u));
		const eng::u16 y = eng::u16(4u + rng.next_mod(H - 6u));
		for (eng::u16 dy = 0u; dy < 2u; ++dy) {
			for (eng::u16 dx = 0u; dx < 2u; ++dx) {
				const eng::u16 cx = eng::u16(x + dx);
				const eng::u16 cy = eng::u16(y + dy);
				if (cx >= W || cy >= H) continue;
				if (mat[cy * W + cx] == eng::u8(Biome::Grass) &&
				    feat[cy * W + cx] == Feature::None && path[cy * W + cx] == 0u) {
					feat[cy * W + cx] = Feature::Crops;
				}
			}
		}
	}
	// 8) Instancia las piezas: lados = material expuesto del vecino (camino = red).
	for (eng::u16 y = 0u; y < H; ++y) {
		for (eng::u16 x = 0u; x < W; ++x) {
			const eng::u16 i = eng::u16(y * W + x);
			const eng::u8 exp = path[i] != 0u ? kBiomePath : mat[i];
			eng::u8 sides[4] {exp, exp, exp, exp};
			if (y > 0u) sides[0] = path[i - W] != 0u ? kBiomePath : mat[i - W];
			if (x + 1u < W) sides[1] = path[i + 1u] != 0u ? kBiomePath : mat[i + 1u];
			if (y + 1u < H) sides[2] = path[i + W] != 0u ? kBiomePath : mat[i + W];
			if (x > 0u) sides[3] = path[i - 1u] != 0u ? kBiomePath : mat[i - 1u];
			Feature features = feat[i];
			EdgeSides<Edge> fedges {Edge::Grass, Edge::Grass, Edge::Grass, Edge::Grass};
			for (const CastlePiece& p : ring) {
				if (p.features != Feature::None && x == eng::u16(cc + p.dx) &&
				    y == eng::u16(castle_r0 + p.dy)) {
					fedges = p.edges;
				}
			}
			const bool is_bridge = path[i] != 0u && mat[i] == eng::u8(Biome::Water);
			if (is_bridge) {
				features = Feature::Bridge;
				fedges = {Edge::Water, Edge::Path, Edge::Water, Edge::Path};
			}
			out.at(x, y) = set.tile_for(mat[i], sides, features, fedges, exp, atlas);
		}
	}
	return set.overflow == 0u && biome_world_connected(out, set);
}

/// **Contrato del mundo de biomas**: para cada par de celdas contiguas, el lado que la
/// pieza de una guarda hacia la otra coincide con el `id` expuesto de la pieza de la otra
/// (el generador escribe el id del vecino en cada lado; las piezas giradas conservan sus
/// lados rotados, así que esto verifica de verdad las máscaras instanciadas — detecta
/// truncados por capacidad o fallos de deduplicación). Las piezas con rasgos cuentan su
/// id expuesto normal (la muralla o el árbol no cambian la juntura).
/// \tparam W,H celdas. \tparam TW,TH,MaxTiles geometría/capacidad. \param out layout.
/// \param set set con las piezas del layout. \return true si todas las juntas casan.
template <eng::u16 W, eng::u16 H, eng::u16 TW, eng::u16 TH, eng::u16 MaxTiles>
[[nodiscard]] constexpr bool biome_world_connected(const TerrainLayout<W, H>& out,
						   const BiomeTileSet<TW, TH, MaxTiles>& set) {
	for (eng::u16 y = 0u; y < H; ++y) {
		for (eng::u16 x = 0u; x < W; ++x) {
			const BiomeSpec& a = set.specs[out.at(x, y)];
			if (x + 1u < W) {
				const BiomeSpec& b = set.specs[out.at(eng::u16(x + 1u), y)];
				if (a.sides[1] != b.id || b.sides[3] != a.id) return false;
			}
			if (y + 1u < H) {
				const BiomeSpec& b = set.specs[out.at(x, eng::u16(y + 1u))];
				if (a.sides[2] != b.id || b.sides[0] != a.id) return false;
			}
		}
	}
	return true;
}

} // namespace eng::gfx::proc::terrain
