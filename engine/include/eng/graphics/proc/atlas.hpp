#pragma once

/// \file atlas.hpp
/// **Atlas de materiales procedurales** (`TextureAtlas`): genera una vez, en carga, los
/// tiles de material que consume el pintor de terreno (`terrain.hpp`) — hierba, piedra,
/// agua, madera, muralla, tejado, nieve… — con **3 variantes** por material para que el
/// mapa no se vea repetitivo, una **paleta compartida de 32 colores** (RGB444 de `eng::
/// Palette32`, la de los registros físicos `COLOR00..31`; el display usa 5 planos) y las
/// texturas tileables de `textures.hpp`.
///
/// También define `MaterialLayer`: varias bandas de material con umbrales y fundido por
/// dither, para transiciones dentro del mismo tile (hierba → piedra → nieve).
///
/// ```text
///   TextureAtlas<32,32,MiniFloat16>
///     palette: eng::Palette32 (32 colores semánticos + jitter por seed)
///     textures[22][3]  ← fill_texture(Clouds/Stone/Marble/…) + remap a base de paleta
///          │                  └─ paso de halftone: 2·levels−1 tonos aparentes (tableros)
///          get(id, variant).at(x,y)          variant_at(tile_x,tile_y,id)  (hash estable)
///          │
///          ▼
///   terrain::paint_terrain_tile(...)         compone cada tile de terreno
/// ```
///
/// ## Halftone (más colores aparentes)
///
/// La paleta tiene 32 colores reales (5 planos). Para que las familias de tono no se vean
/// escalonadas, la generación añade un **paso de halftone**: cada textura se cuantiza a
/// `2·levels−1` pasos y los impares se pintan como **tablero 2×2** entre los dos colores
/// vecinos (`halftone_index`), de modo que el ojo ve el intermedio — una familia de 4
/// colores muestra 7 tonos. El patrón es espacial y determinista (sin costura entre
/// tiles); ver `dither.hpp`.
///
/// ## Índices de paleta por material
///
/// `MaterialDesc::base` + `levels−1 ≤ 31` (la tabla está construida con esa cota; el test
/// host la verifica). Los tramos están agrupados **por tono** (8 familias × 4 niveles) para
/// que un material no mezcle colores de otra familia (piedra con teja, etc.):
///
/// ```text
///    0-3 cueva (negro→gris cálido)      4-7 agua (abisal→espuma)
///    8-11 hierba/hoja/musgo             12-15 tierra/arena/camino
///   16-19 cultivo A/B (trigo)          20-23 piedra/granito/mármol/adoquín
///   24-27 madera/tejado/óxido          28-31 follaje/nieve (frío y luz)
/// ```
///
/// ## Escalar
///
/// El atlas es una plantilla sobre el escalar `S` del ruido: en Amiga `MiniFloat16`, en
/// host `float`. La generación es costosa (22 materiales × 3 variantes × `W·H` píxeles de
/// fBm + Worley): **exclusivamente de carga**, nunca por frame.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por tests host (`HOST-442`/`HOST-443`); la demo 119 usa el atlas
/// precocinado en host (la generación runtime en 68000 está medida y queda fuera del
/// presupuesto de carga: `PROCEDURAL_BITMAPS.md` §Coste).
/// Diseño y uso: `docs/engine/architecture/PROCEDURAL_BITMAPS.md`.

#include <eng/core/math/random.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/evaluators.hpp>
#include <eng/graphics/proc/tiles.hpp>
#include <eng/graphics/proc/textures.hpp>

namespace eng::gfx::proc {

/// Identificadores de material del atlas (el índice del array `textures`).
enum class MaterialId : eng::u8 {
	Grass = 0,
	Dirt,
	Stone,
	Granite,
	Water,
	Foam,
	Sand,
	Wood,
	Marble,
	Wall,
	Roof,
	CropA,
	CropB,
	Foliage,
	Trunk,
	Rock,
	Cave,
	Rust,
	Moss,
	Snow,
	Path,
	Cobble,
	Count,
};

/// Número de materiales del atlas (índice reservado `Count`).
inline constexpr eng::u8 kMaterialCount = eng::u8(MaterialId::Count);

/// Variantes por material (seeds distintos; `variant_at` elige de forma estable por
/// coordenadas de tile).
inline constexpr eng::u8 kVariantsPerMaterial = 3;

/// Descripción de un material: nombre, tramo de paleta y frecuencia del ruido.
struct MaterialDesc {
	/// Nombre corto (depuración y documentación del atlas).
	const char* name;
	/// Niveles de color que usa (índices `base..base+levels-1`, con `base+levels-1 ≤ 31`).
	eng::u8 levels;
	/// Primer índice de la paleta compartida.
	eng::u8 base;
	/// Ciclos de ruido por tile (`k`): la escala es `tile_scale<S>(TW, k)` y el periodo `k`.
	eng::u8 freq;
};

/// Tabla `constexpr` de materiales (niveles/base/frecuencia por `MaterialId`; fuente única
/// que el atlas y el pintor comparten sin nombrar el escalar del atlas). Las bases caen
/// dentro de la familia de tono (8 familias de 4 niveles; ver el diagrama del fichero) y
/// los tramos de una misma familia pueden solaparse (agua/espuma) para reutilizar steps.
/// \param id material consultado. \return descripción del material.
[[nodiscard]] constexpr MaterialDesc material_desc(MaterialId id) {
	switch (id) {
	case MaterialId::Grass: return {"grass", 4, 8, 1};
	case MaterialId::Dirt: return {"dirt", 3, 12, 2};
	case MaterialId::Stone: return {"stone", 3, 20, 2};
	case MaterialId::Granite: return {"granite", 3, 21, 2};
	case MaterialId::Water: return {"water", 3, 4, 1};
	case MaterialId::Foam: return {"foam", 2, 6, 4};
	case MaterialId::Sand: return {"sand", 2, 13, 1};
	case MaterialId::Wood: return {"wood", 3, 24, 2};
	case MaterialId::Marble: return {"marble", 2, 22, 1};
	case MaterialId::Wall: return {"wall", 3, 20, 2};
	case MaterialId::Roof: return {"roof", 3, 25, 2};
	case MaterialId::CropA: return {"crop_a", 3, 16, 2};
	case MaterialId::CropB: return {"crop_b", 3, 17, 2};
	// Follaje en su propio tono (verde profundo, plano): sin él las copas no se
	// distinguirían de la hierba en un vistazo.
	case MaterialId::Foliage: return {"foliage", 2, 28, 4};
	case MaterialId::Trunk: return {"trunk", 1, 24, 3};
	case MaterialId::Rock: return {"rock", 3, 20, 2};
	case MaterialId::Cave: return {"cave", 3, 0, 1};
	case MaterialId::Rust: return {"rust", 2, 26, 2};
	case MaterialId::Moss: return {"moss", 2, 8, 4};
	case MaterialId::Snow: return {"snow", 2, 30, 1};
	case MaterialId::Path: return {"path", 3, 12, 2};
	case MaterialId::Cobble: return {"cobble", 3, 21, 4};
	default: return {"grass", 4, 8, 1}; // `Count` no es un material
	}
}

/// **Atlas de materiales**: los `kMaterialCount × kVariantsPerMaterial` tiles de
/// material + la paleta compartida de 32 colores.
/// \tparam TW,TH geometría del tile (cuadrados de 32×32 típicos; el pintor usa TW).
/// \tparam S     escalar del ruido de generación (`MiniFloat16` en Amiga, `float` en host).
template <eng::u16 TW, eng::u16 TH, class S>
struct TextureAtlas {
	/// Ancho del tile de material en píxeles.
	static constexpr eng::u16 tile_w = TW;
	/// Alto del tile de material en píxeles.
	static constexpr eng::u16 tile_h = TH;

	/// Tiles de material: `textures[material][variante]` (índices de paleta ya remapeados).
	BitmapBuffer<TW, TH> textures[kMaterialCount][kVariantsPerMaterial] {};
	/// Paleta compartida de 32 colores RGB444 (la base de la escena que muestre el mapa).
	eng::Palette32 palette {};
	/// Colores usados de la paleta (`<= 32`; el display necesita 5 planos).
	eng::u8 total_colors = 0;
	/// Semilla base del atlas (la usa `variant_at` para el hash de variante).
	eng::u32 base_seed = 0;

	/// Descripción de un material (niveles/base/frecuencia): delega en la tabla libre
	/// `material_desc` (fuente única que no exige nombrar el escalar del atlas).
	/// \param id material consultado. \return descripción del material.
	[[nodiscard]] static constexpr MaterialDesc desc(MaterialId id) { return material_desc(id); }

	/// Genera el atlas completo: paleta (32 colores semánticos + jitter por seed) y las
	/// `kMaterialCount × kVariantsPerMaterial` texturas. Costoso (de carga): 66 tiles de
	/// fBm/Worley de `octaves` octavas. El atlas ocupa los 32 colores de `Palette32`
	/// (5 planos de display; ver `PROCEDURAL_BITMAPS.md`).
	/// \param seed semilla base (mismo seed = mismo atlas, host y Amiga).
	constexpr void generate(eng::u32 seed) {
		base_seed = seed;
		total_colors = 32u;
		build_palette(seed);
		eng::Xoroshiro64pp rng {seed, seed ^ 0xA71A5u};
		render_all(rng);
	}

	/// Tile de material de una variante concreta (0..`kVariantsPerMaterial`−1, con módulo).
	/// \param id material. \param variant variante pedida (se envuelve con módulo).
	/// \return buffer de índices del tile.
	[[nodiscard]] constexpr const BitmapBuffer<TW, TH>& get(MaterialId id, eng::u8 variant = 0u) const {
		return textures[eng::util::to_underlying(id)][variant % kVariantsPerMaterial];
	}

	/// Variante determinista para la posición `(tile_x, tile_y)` del mapa: hash entero
	/// estable (mismo tile → misma variante en cualquier máquina) que rompe la repetición.
	/// \param tile_x,tile_y columna/fila del tile en el mapa. \param id material.
	/// \return índice de variante 0..2.
	[[nodiscard]] constexpr eng::u8 variant_at(eng::u16 tile_x, eng::u16 tile_y, MaterialId id) const {
		eng::u32 h = base_seed ^ (eng::u32(tile_x) * 374761393u) ^
			     (eng::u32(tile_y) * 668265263u) ^
			     (eng::u32(eng::util::to_underlying(id)) * 2246822519u);
		h = (h ^ (h >> 13u)) * 1274126177u;
		return eng::u8((h >> 16u) % kVariantsPerMaterial);
	}

private:
	/// Paleta semántica (32 colores RGB444 en 8 familias de 4 niveles) + variación ligera
	/// ±1 por canal con el RNG (dos atlas con seeds distintos no son idénticos, pero
	/// mantienen roles: agua azul, hierba verde, piedra gris…). Se reservan 0 = negro/
	/// transparente y 31 = nieve/luz (puros); el jitter afecta a 1..30.
	/// \param seed semilla del jitter.
	constexpr void build_palette(eng::u32 seed) {
		// Valores RGB444 con el canal dominante del rol; dentro de cada familia los cuatro
		// niveles van de oscuro a claro (los tramos de material los reutilizan).
		eng::Palette32 p {};
		// Familia 1: cueva (negro → gris cálido).
		p.color[0] = 0x0000u;
		p.color[1] = 0x0211u;
		p.color[2] = 0x0321u;
		p.color[3] = 0x0432u;
		// Familia 2: agua (abisal → espuma).
		p.color[4] = 0x0026u;
		p.color[5] = 0x0038u;
		p.color[6] = 0x004Cu;
		p.color[7] = 0x006Au;
		// Familia 3: hierba / hoja / musgo.
		p.color[8] = 0x0480u;
		p.color[9] = 0x04A0u;
		p.color[10] = 0x05B0u;
		p.color[11] = 0x06C0u;
		// Familia 4: tierra / arena / camino.
		p.color[12] = 0x0540u;
		p.color[13] = 0x0640u;
		p.color[14] = 0x0982u;
		p.color[15] = 0x0B83u;
		// Familia 5: cultivo A/B (trigo).
		p.color[16] = 0x0990u;
		p.color[17] = 0x0A90u;
		p.color[18] = 0x0BB0u;
		p.color[19] = 0x0CC0u;
		// Familia 6: piedra / granito / mármol / adoquín.
		p.color[20] = 0x0666u;
		p.color[21] = 0x0777u;
		p.color[22] = 0x0999u;
		p.color[23] = 0x0AAAu;
		// Familia 7: madera / tejado / óxido.
		p.color[24] = 0x0843u;
		p.color[25] = 0x0A64u;
		p.color[26] = 0x0B52u;
		p.color[27] = 0x0C75u;
		// Familia 8: follaje profundo / nieve.
		p.color[28] = 0x0180u;
		p.color[29] = 0x02A0u;
		p.color[30] = 0x0CCCu;
		p.color[31] = 0x0EEEu;
		// Solo el 0 (transparente) y el 31 (nieve/luz) quedan fijos; el resto varía ±1 por
		// canal (12 bits) para que dos seeds den atlas distinguibles.
		eng::Xoroshiro64pp rng {seed ^ 0x5A5A5A5Au, seed | 1u};
		const eng::u8 last_jitter = eng::u8(total_colors - 2u);
		for (eng::u8 i = 1; i <= last_jitter; ++i) {
			const eng::u16 c = p.color[i];
			const eng::u8 r = (c >> 8u) & 0xFu;
			const eng::u8 g = (c >> 4u) & 0xFu;
			const eng::u8 b = c & 0xFu;
			const eng::s8 dr = eng::s32(rng.next_mod(3u)) - 1;
			const eng::s8 dg = eng::s32(rng.next_mod(3u)) - 1;
			const eng::s8 db = eng::s32(rng.next_mod(3u)) - 1;
			const eng::u8 nr = clamp4(r + dr);
			const eng::u8 ng = clamp4(g + dg);
			const eng::u8 nb = clamp4(b + db);
			p.color[i] = eng::u16((eng::u16(nr) << 8u) | (eng::u16(ng) << 4u) | nb);
		}
		palette = p;
	}

	/// Recorta un canal RGB444 con signo a 0..15 (el jitter no puede salir del nibble).
	/// \param v valor del canal tras el jitter (−1..16). \return canal 0..15.
	[[nodiscard]] static constexpr eng::u8 clamp4(eng::s32 v) {
		return eng::u8(v < 0 ? 0 : (v > 15 ? 15 : v));
	}

	/// Renderiza las 3 variantes de un material con su textura y aplica el **paso de
	/// halftone**: la textura se cuantiza a `2·levels−1` pasos locales y `remap_halftone`
	/// convierte los impares en tableros 2×2 del par vecino → la familia de `levels`
	/// colores reales muestra `2·levels−1` tonos APARENTES (intermedios por checkerboard,
	/// la petición de «más colores» sin tocar la paleta). Cada variante usa un seed del
	/// RNG (determinista en el orden de la tabla).
	/// \tparam TexFn constructor de la textura `(S seed) -> S(px,py)`. \param id material.
	/// \param rng RNG compartido (avanza por variante). \param make_tex fábrica de la textura.
	template <class TexFn>
	constexpr void fill_variants(MaterialId id, eng::Xoroshiro64pp& rng, TexFn&& make_tex) {
		const MaterialDesc d = desc(id);
		const eng::u8 mi = eng::util::to_underlying(id);
		for (eng::u8 v = 0; v < kVariantsPerMaterial; ++v) {
			const eng::u32 vseed = rng.next();
			// La mezcla la hace el tablero (no Bayer): cuantización plana a la escala
			// aparente y reparto par/impar en `remap_halftone`.
			fill_texture(textures[mi][v], make_tex(vseed), eng::u8(2u * d.levels - 1u), false);
			remap_halftone(textures[mi][v], d.base, d.levels);
		}
	}

	/// Remapea los índices locales `0..2·levels−2` al tramo `base..base+levels-1` con el
	/// paso de halftone: paso par = color sólido, impar = `halftone2x2` del par vecino
	/// (intermedio aparente). La fase del tablero es la del píxel `(x, y)` del tile, así
	/// que tiles contiguos siguen la misma retícula.
	/// \param buf tile con pasos locales. \param base primer índice del tramo.
	/// \param levels colores reales del tramo.
	static constexpr void remap_halftone(BitmapBuffer<TW, TH>& buf, eng::u8 base, eng::u8 levels) {
		const eng::u8 span = eng::u8(2u * levels - 1u);
		for (eng::u16 y = 0; y < TH; ++y) {
			for (eng::u16 x = 0; x < TW; ++x) {
				const eng::u8 step = eng::u8(buf.at(x, y) % span);
				buf.at(x, y) = halftone_index(step, base, levels, x, y);
			}
		}
	}

	/// Genera todas las texturas: despacho por material a la textura de `textures.hpp` con
	/// la escala `tile_scale<S>(TW, freq)` y el periodo `freq` (tileable por construcción;
	/// ver contrato de periodicidad en `textures.hpp`).
	/// \param rng RNG del atlas (cada material y variante consume seeds en orden fijo).
	constexpr void render_all(eng::Xoroshiro64pp& rng) {
		namespace tx = tex;
		using eng::math::div_norm;
		using eng::math::scalar_traits;
		const S one = scalar_traits<S>::one();
		auto sc = [&](MaterialId id) { return tile_scale<S>(TW, desc(id).freq); };
		auto per = [&](MaterialId id) { return eng::s32(desc(id).freq); };

		// Hierba: fBm suave (nubes) a 1 ciclo por tile.
		fill_variants(MaterialId::Grass, rng, [&](eng::u32 s) {
			return tx::Clouds<S> {s, 3, sc(MaterialId::Grass), per(MaterialId::Grass)};
		});
		// Tierra: piedra suave (2 ciclos).
		fill_variants(MaterialId::Dirt, rng, [&](eng::u32 s) {
			return tx::Stone<S> {s, sc(MaterialId::Dirt), 3, 2, per(MaterialId::Dirt)};
		});
		// Piedra: roca con warp + ridged (4 octavas).
		fill_variants(MaterialId::Stone, rng, [&](eng::u32 s) {
			return tx::Stone<S> {s, sc(MaterialId::Stone), 4, 2, per(MaterialId::Stone)};
		});
		// Granito: fBm + cristales Worley a doble frecuencia.
		fill_variants(MaterialId::Granite, rng, [&](eng::u32 s) {
			return tx::Granite<S> {s, sc(MaterialId::Granite), 2, 4, per(MaterialId::Granite)};
		});
		// Agua: fBm de 4 octavas (olas).
		fill_variants(MaterialId::Water, rng, [&](eng::u32 s) {
			return tx::Clouds<S> {s, 4, sc(MaterialId::Water), per(MaterialId::Water)};
		});
		// Espuma: burbujas Worley (1/8 de umbral de relleno).
		fill_variants(MaterialId::Foam, rng, [&](eng::u32 s) {
			return tx::Foam<S> {s, sc(MaterialId::Foam), eighth(one), per(MaterialId::Foam)};
		});
		// Arena: fBm suave de 2 octavas.
		fill_variants(MaterialId::Sand, rng, [&](eng::u32 s) {
			return tx::Clouds<S> {s, 2, sc(MaterialId::Sand), per(MaterialId::Sand)};
		});
		// Madera: veta direccional (10 anillos).
		fill_variants(MaterialId::Wood, rng, [&](eng::u32 s) {
			return tx::Wood<S> {s, sc(MaterialId::Wood), half(one), 10, per(MaterialId::Wood)};
		});
		// Mármol: venas de turbulencia plegadas (1 ciclo).
		fill_variants(MaterialId::Marble, rng, [&](eng::u32 s) {
			return tx::Marble<S> {s, sc(MaterialId::Marble), scalar_traits<S>::from_int(6), 5,
					      per(MaterialId::Marble)};
		});
		// Muralla: piedra trabajada de 3 octavas.
		fill_variants(MaterialId::Wall, rng, [&](eng::u32 s) {
			return tx::Stone<S> {s, sc(MaterialId::Wall), 3, 2, per(MaterialId::Wall)};
		});
		// Tejado: veta de madera con 6 anillos.
		fill_variants(MaterialId::Roof, rng, [&](eng::u32 s) {
			return tx::Wood<S> {s, sc(MaterialId::Roof), ninth(one), 6, per(MaterialId::Roof)};
		});
		// Cultivo A: fBm de 2 octavas.
		fill_variants(MaterialId::CropA, rng, [&](eng::u32 s) {
			return tx::Clouds<S> {s, 2, sc(MaterialId::CropA), per(MaterialId::CropA)};
		});
		// Cultivo B: fBm de 2 octavas (tono distinto del A).
		fill_variants(MaterialId::CropB, rng, [&](eng::u32 s) {
			return tx::Clouds<S> {s, 2, sc(MaterialId::CropB), per(MaterialId::CropB)};
		});
		// Follaje: musgo frondoso (parches Worley a media frecuencia).
		fill_variants(MaterialId::Foliage, rng, [&](eng::u32 s) {
			return tx::Moss<S> {s, sc(MaterialId::Foliage), 2, per(MaterialId::Foliage)};
		});
		// Tronco: veta estrecha de 4 anillos.
		fill_variants(MaterialId::Trunk, rng, [&](eng::u32 s) {
			return tx::Wood<S> {s, sc(MaterialId::Trunk), eighth(one), 4, per(MaterialId::Trunk)};
		});
		// Roca: granito de 3 octavas (peñascos).
		fill_variants(MaterialId::Rock, rng, [&](eng::u32 s) {
			return tx::Granite<S> {s, sc(MaterialId::Rock), 2, 3, per(MaterialId::Rock)};
		});
		// Cueva: piedra oscura de 2 octavas.
		fill_variants(MaterialId::Cave, rng, [&](eng::u32 s) {
			return tx::Stone<S> {s, sc(MaterialId::Cave), 2, 2, per(MaterialId::Cave)};
		});
		// Óxido: corrosión (turbulencia + crestas).
		fill_variants(MaterialId::Rust, rng, [&](eng::u32 s) {
			return tx::Rust<S> {s, sc(MaterialId::Rust), 3, per(MaterialId::Rust)};
		});
		// Musgo: fBm enmascarado por parches.
		fill_variants(MaterialId::Moss, rng, [&](eng::u32 s) {
			return tx::Moss<S> {s, sc(MaterialId::Moss), 2, per(MaterialId::Moss)};
		});
		// Nieve: fBm fino de 2 octavas.
		fill_variants(MaterialId::Snow, rng, [&](eng::u32 s) {
			return tx::Clouds<S> {s, 2, sc(MaterialId::Snow), per(MaterialId::Snow)};
		});
		// Camino: tierra apisonada de 2 octavas.
		fill_variants(MaterialId::Path, rng, [&](eng::u32 s) {
			return tx::Stone<S> {s, sc(MaterialId::Path), 2, 2, per(MaterialId::Path)};
		});
		// Adoquín: burbujas Worley con umbral alto (piedras juntas).
		fill_variants(MaterialId::Cobble, rng, [&](eng::u32 s) {
			return tx::Foam<S> {s, sc(MaterialId::Cobble), half(one), per(MaterialId::Cobble)};
		});
	}

	/// Constante `1/den` en `S` (parámetro de turbulencia de las texturas).
	/// \param one el `1` del escalar. \param den denominador (p. ej. 2, 8, 9).
	/// \return `1/den` en `S`.
	[[nodiscard]] static constexpr S frac(S one, int den) {
		return eng::math::div_norm(one, eng::math::scalar_traits<S>::from_int(den));
	}
	/// `1/2` en `S` (pastillas de espuma y umbral de adoquín). \param one el `1` del escalar. \return `1/2`.
	[[nodiscard]] static constexpr S half(S one) { return frac(one, 2); }
	/// `1/8` en `S` (turbulencia de la veta de madera). \param one el `1` del escalar. \return `1/8`.
	[[nodiscard]] static constexpr S eighth(S one) { return frac(one, 8); }
	/// `1/9` en `S` (turbulencia del tejado). \param one el `1` del escalar. \return `1/9`.
	[[nodiscard]] static constexpr S ninth(S one) { return frac(one, 9); }
};

/// **Banda de material** para `MaterialLayer`: rango de "altura" `[lo, hi]` (en `[0,1]`)
/// con una zona de fundido `soft` a cada lado (el peso baja a 0 con `smoothstep`).
/// \tparam S escalar de los umbrales.
template <class S>
struct MaterialBand {
	/// Material de la banda.
	MaterialId id = MaterialId::Grass;
	/// Variante del material (0..2).
	eng::u8 variant = 0;
	/// Altura mínima (inicio de la banda).
	S lo {};
	/// Altura máxima (fin de la banda).
	S hi {};
	/// Ancho del fundido a cada lado (0 = transición dura).
	S soft {};
};

/// **Capa de materiales por altura**: mezcla hasta 6 bandas según un campo de altura
/// (fBm o un perfil externo), fundiendo los bordes con dither espacial (sin interpolar
/// color). Ejemplos típicos (se configuran con `add`, no hay presets de un caso):
///
/// ```cpp
/// // Hierba → piedra → nieve (montaña):
/// layer.add(MaterialId::Grass, v0, S{},       S{35}/100, S{8}/100);
/// layer.add(MaterialId::Stone, v1, S{30}/100, S{70}/100, S{10}/100);
/// layer.add(MaterialId::Snow,  v2, S{65}/100, S{1},      S{8}/100);
/// // Tierra → hierba → musgo (bosque): Dirt/Grass/Moss con umbrales 25/70/60…
/// ```
/// \tparam TW,TH geometría del tile. \tparam S escalar del campo de altura.
template <eng::u16 TW, eng::u16 TH, class S>
struct MaterialLayer {
	/// Máximo de bandas de la capa.
	static constexpr eng::u8 kMaxBands = 6;

	/// Bandas configuradas (orden de inserción; el peso resuelve la dominante).
	MaterialBand<S> bands[kMaxBands] {};
	/// Número de bandas configuradas.
	eng::u8 band_count = 0;
	/// Semilla del campo de altura (ruido fBm).
	eng::u32 height_seed = 0;
	/// Escala del campo de altura (`tile_scale<S>` para tileables).
	S height_scale {};
	/// Periodo del campo de altura (0 = no tileable).
	int height_period = 0;
	/// Octavas del campo de altura.
	eng::u8 height_octaves = 3;

	/// Añade una banda (sin ordenar: el peso se evalúa por banda). No hace nada si ya hay
	/// `kMaxBands` bandas (la capa no reserva memoria).
	/// \param id material. \param variant variante 0..2. \param lo,hi rango de altura en `[0,1]`.
	/// \param soft ancho del fundido en los bordes (0 = corte duro).
	void add(MaterialId id, eng::u8 variant, S lo, S hi, S soft) {
		if (band_count >= kMaxBands) return;
		bands[band_count] = MaterialBand<S> {id, variant, lo, hi, soft};
		++band_count;
	}

	/// Rellena `buf` mezclando las bandas según el campo de altura fBm interno (tileable
	/// con `height_period`).
	/// \param buf buffer destino. \param atlas atlas de materiales (debe ser de la misma `S`).
	void render(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas) const {
		using eng::math::fbm2;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		const S two = scalar_traits<S>::from_int(2);
		const S half = eng::math::div_norm(scalar_traits<S>::one(), two);
		for (eng::u16 y = 0; y < TH; ++y) {
			for (eng::u16 x = 0; x < TW; ++x) {
				const S hx = mul_norm(scalar_traits<S>::from_int(x), height_scale);
				const S hy = mul_norm(scalar_traits<S>::from_int(y), height_scale);
				const S h = fbm2(hx, hy, height_seed, height_octaves, two, half, height_period);
				buf.at(x, y) = sample_bands(h, x, y, atlas);
			}
		}
	}

	/// Igual que `render` pero con un campo de altura externo (p. ej. un perfil 1D de
	/// montaña normalizado); `height_fn(x,y)` debe devolver `[0,1]`.
	/// \tparam HeightFn callable `S(u16 x, u16 y)`. \param buf destino. \param atlas atlas.
	/// \param height_fn campo de altura externo.
	template <class HeightFn>
	void render_with_height(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas,
				HeightFn&& height_fn) const {
		for (eng::u16 y = 0; y < TH; ++y) {
			for (eng::u16 x = 0; x < TW; ++x) {
				buf.at(x, y) = sample_bands(height_fn(x, y), x, y, atlas);
			}
		}
	}

private:
	/// Peso de la banda `b` para una altura `h`: 1 dentro de `[lo, hi]`, `smoothstep`
	/// decreciente en la zona `soft` y 0 fuera. Sin divisiones fuera de `soft` (una
	/// `div_norm` por banda en la zona de fundido, generación).
	/// \param h altura en `[0,1]`. \param b banda. \return peso 0..1.
	[[nodiscard]] static S band_weight(S h, const MaterialBand<S>& b) {
		using eng::math::div_norm;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::smoothstep;
		const S zero = scalar_traits<S>::zero();
		const S one = scalar_traits<S>::one();
		const S three = scalar_traits<S>::from_int(3);
		const S two = scalar_traits<S>::from_int(2);
		if (h >= b.lo && h <= b.hi) return one;
		if (b.soft <= zero) return zero;
		if (h < b.lo) {
			const S t = div_norm(h - (b.lo - b.soft), b.soft);
			if (t < zero) return zero;
			const S u = t * t * (three - mul_norm(two, t));
			return u; // smoothstep suave creciente hacia la banda
		}
		const S t = div_norm((b.hi + b.soft) - h, b.soft);
		if (t < zero) return zero;
		return t * t * (three - mul_norm(two, t));
	}

	/// Elige la banda dominante y, si hay segunda, funde con dither espacial (el peso
	/// `best_w` decide el porcentaje de píxeles del material dominante).
	/// \param h altura. \param x,y píxel (módulo 8 en el dither). \param atlas atlas.
	/// \return índice de color en la paleta del atlas.
	[[nodiscard]] eng::u8 sample_bands(S h, eng::u16 x, eng::u16 y,
					   const TextureAtlas<TW, TH, S>& atlas) const {
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		eng::u8 best = 0;
		S best_w = scalar_traits<S>::zero();
		eng::u8 second = 0;
		S second_w = scalar_traits<S>::zero();
		for (eng::u8 i = 0; i < band_count; ++i) {
			const S w = band_weight(h, bands[i]);
			if (w > best_w) {
				second = best;
				second_w = best_w;
				best = i;
				best_w = w;
			} else if (w > second_w) {
				second = i;
				second_w = w;
			}
		}
		if (band_count == 0) return 0u;
		const MaterialBand<S>& b0 = bands[best];
		const eng::u8 c0 = atlas.get(b0.id, b0.variant).at(x, y);
		if (second_w <= scalar_traits<S>::zero() || best_w >= scalar_traits<S>::one()) return c0;
		const MaterialBand<S>& b1 = bands[second];
		const eng::u8 c1 = atlas.get(b1.id, b1.variant).at(x, y);
		const eng::u8 alpha =
			scalar_traits<S>::to_int(mul_norm(best_w, scalar_traits<S>::from_int(255)));
		return blend_index(c1, c0, alpha, x, y);
	}
};

} // namespace eng::gfx::proc
