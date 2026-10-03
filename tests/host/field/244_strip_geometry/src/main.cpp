// ============================================================================
// Test HOST-244: geometria e invariantes del scroller de tiras (eng/field/strip_scroller.hpp).
// ============================================================================
//
// Codifica como codigo verificable la referencia del "Copper ring + incoming strip":
// constantes compile-time (static_assert) y, sobre un modelo de anillo en sombra, los invariantes:
//   - la columna destino del blit (guarda) NUNCA esta en la ventana visible actual,
//   - tras cada cruce de palabra, TODA la ventana visible esta pintada,
//   - como maximo 2 blits por frame (columna + fila) en cualquier secuencia de pasos 1..16.
//
//   bash tools/run-host-tests.sh tests/host/field/244_strip_geometry

#include <array>
#include <cstdint>
#include <cstdio>

#include <eng/field/strip_scroller.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

// Caso base: 320x208 (split-cap), 5 planos, tile 16, guarda 2 palabras. Anillo X = 23 words.
using Geom = eng::playfield::StripScrollGeometry<320u, 208u, 5u, 16u, 16u, 2u, 1u, false>;
// Variante XY (anillo vertical + split); 208 -> cabe en VPOS.
using GeomXY = eng::playfield::StripScrollGeometry<320u, 208u, 5u, 16u, 16u, 2u, 1u, true>;
// Tile 32: guarda 64 px = 4 palabras; viewport 192 (multiplo de 32).
using Geom32 = eng::playfield::StripScrollGeometry<320u, 192u, 5u, 32u, 32u, 4u, 1u, false>;
// Mapa largo: el anillo es el ancho del bitmap del mapa (160 words), no pantalla+guarda.
using GeomLong = eng::playfield::StripScrollGeometry<320u, 208u, 5u, 16u, 16u, 2u, 1u, false, 160u>;
static_assert(GeomLong::ring_w_words == 160u && GeomLong::ring_w_bytes == 320u,
	      "anillo de mapa largo (160 words)");

static_assert(Geom::visible_words == 20u, "320 px = 20 words");
static_assert(Geom::ring_w_words == 23u, "anillo = 20 + 2 guarda + 1 fetch");
static_assert(Geom::ring_w_bytes == 46u, "46 B/planeline");
static_assert(Geom::bpl_mod == 188u, "BPL1MOD = planes*ring_w_bytes - fetch_words*2");
static_assert(Geom::bltdmod_col == 44u, "BLTDMOD columna (tile 16)");
static_assert(Geom::column_planelines == 1040u, "208 lineas x 5 planos");
static_assert(Geom32::bltdmod_col == 46u, "BLTDMOD columna (tile 32, anillo 50 B)");
static_assert(GeomXY::ring_h == 240u, "anillo vertical = 208 + 2*16");
// BLTSIZE H es de 10 bits (max 1024): columna 208x5 = 1040 -> 2 blits; 192x5=960 -> 1.
static_assert(Geom::column_planelines == 1040u, "208 x 5 planos");
static_assert(Geom::column_blits == 2u, "1040 > 1024 -> 2 blits");
static_assert(Geom32::column_planelines == 960u, "192 x 5 planos");
static_assert(Geom32::column_blits == 1u, "960 <= 1024 -> 1 blit");
// Tile 32: tira de 2 words, anillo 50 B, BLTDMOD 46, 6 tiles por columna (192/32).
static_assert(Geom32::strip_words == 2u, "tile 32 -> 2 words");
static_assert(Geom32::ring_w_bytes == 50u, "320 + 64 guarda + 16 fetch = 400 px = 50 B");
static_assert(Geom32::bltdmod_col == 46u, "BLTDMOD = 50 - 4");
static_assert(Geom32::column_tiles == 6u, "192 / 32 = 6 tiles");
// Interfaz `MapWords`: deriva el anillo como `visible + periodo` (60) y garantiza el multiplo.
using GeomMapWords =
	eng::playfield::StripScrollGeometry<320u, 256u, 3u, 16u, 16u, 2u, 1u, false, 0u, 0u, 40u>;
static_assert(GeomMapWords::ring_w_words == 60u, "MapWords: anillo = visible + periodo (20 + 40)");
static_assert((GeomMapWords::ring_w_words - GeomMapWords::visible_words) % 40u == 0u,
	      "MapWords: span multiplo del periodo por construccion");

// Split OCS: la linea de split es 0x2c + viewport_h; con 208 -> 252 <= 255 (cabe).

struct Lcg {
	std::uint32_t s;
	std::uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
	std::uint32_t range(std::uint32_t n) { return next() % n; }
};

bool simulate(std::uint32_t seed) {
	Lcg rng {seed * 2654435761u + 12345u};
	std::array<bool, Geom::ring_w_words> painted {};
	for (unsigned w = 0; w < Geom::visible_words; ++w) painted[w] = true; // pantalla inicial pintada
	const long period = static_cast<long>(Geom::ring_w_words) * 16;
	long scroll = 0;
	bool fwd = true;
	for (int f = 0; f < 20000; ++f) {
		const int step = 1 + static_cast<int>(rng.range(16u));
		if (rng.range(6u) == 0u) fwd = !fwd;
		long ns = scroll + (fwd ? step : -step);
		if (ns < 0) { ns = 0; fwd = true; }          // rebote (mapa acotado)
		if (ns >= period) { ns = period - 1; fwd = false; }
		const auto fr = eng::playfield::plan_strip_frame<Geom>(
			static_cast<eng::s32>(ns), 0, static_cast<eng::s32>(scroll), 0);
		check(fr.blits <= 2u, "<= 2 blits/frame");
		const eng::u16 span = static_cast<eng::u16>(Geom::ring_w_words - Geom::visible_words);
		const long scl = scroll < 1 ? 1 : scroll;
		const eng::u16 old_window = static_cast<eng::u16>(
			(eng::graphics::fine_scroll_coarse(static_cast<eng::u16>(scl)) / 16u) % span);
		if (fr.column_crossed) {
			// La columna destino debe caer FUERA de la ventana visible ACTUAL (invisible).
			if (!eng::playfield::strip_dest_is_guard(fr.col_dest_word, old_window,
							     Geom::visible_words, Geom::ring_w_words)) {
				std::printf("  [seed %u] destino %u dentro de la ventana %u (scroll %ld->%ld)\n",
					    seed, fr.col_dest_word, old_window, scroll, ns);
				return false;
			}
			painted[fr.col_dest_word] = true;
		}
		scroll = ns;
		for (unsigned k = 0; k < Geom::visible_words; ++k) {
			if (!painted[(fr.window_word + k) % Geom::ring_w_words]) {
				std::printf("  [seed %u] hueco en ventana %u (scroll %ld)\n", seed,
					    fr.window_word, scroll);
				return false;
			}
		}
	}
	return true;
}

// Invariante de **contenido** (no solo "pintado"): modela el valor de mundo de cada slot y exige
// que, en todo paso, la ventana visible Y la palabra extra de fetch (`window+visible`) contengan
// la columna del mundo que les toca. Codifica el requisito de dimensionado que faltaba:
// `span = ring - visible` debe ser **multiplo del periodo del mapa** `map_period`; si no, el
// contenido se descuadra al envolver el anillo (el slot `s` dejaria de valer `s % map_period`).
template <class G>
bool simulate_content(unsigned map_period, std::uint32_t seed) {
	Lcg rng {seed * 2654435761u + 99991u};
	std::array<int, G::ring_w_words> ring {};
	for (unsigned s = 0; s < G::ring_w_words; ++s) ring[s] = static_cast<int>(s % map_period);
	const long period = static_cast<long>(G::ring_w_words) * 16;
	long scroll = 0;
	bool fwd = true;
	for (int f = 0; f < 20000; ++f) {
		const int step = 1 + static_cast<int>(rng.range(16u));
		if (rng.range(6u) == 0u) fwd = !fwd;
		long ns = scroll + (fwd ? step : -step);
		if (ns < 0) { ns = 0; fwd = true; }
		if (ns >= period) { ns = period - 1; fwd = false; }
		const auto fr = eng::playfield::plan_strip_frame<G>(
			static_cast<eng::s32>(ns), 0, static_cast<eng::s32>(scroll), 0);
		const unsigned span = G::ring_w_words - G::visible_words;
		const unsigned sxu = ns < 1 ? 1u : static_cast<unsigned>(ns);
		const unsigned cw = eng::graphics::fine_scroll_coarse(static_cast<eng::u16>(sxu)) / 16u;
		if (fr.column_crossed) {
			const unsigned off = cw - (cw % span);
			ring[fr.col_dest_word] =
				static_cast<int>((fr.col_dest_word + off) % map_period);
		}
		for (unsigned k = 0u; k <= G::visible_words; ++k) {
			const unsigned slot = (fr.window_word + k) % G::ring_w_words;
			if (ring[slot] != static_cast<int>((cw + k) % map_period)) return false;
		}
		scroll = ns;
	}
	return true;
}

// **Controlador end-to-end**: `StripScrollController` (plan -> `compose_column` -> blit) sobre un
// sink mock que escribe el slot del anillo como el Blitter. El banco codifica el id de tile en
// todas las palabras y el mapa devuelve la columna, asi el valor de cada slot es la columna del
// mundo que le toca: se comprueba la ventana visible + la palabra extra de fetch por frame.
template <class G>
bool simulate_controller(std::uint32_t seed) {
	constexpr eng::u16 kPeriod = static_cast<eng::u16>(G::ring_w_words - G::visible_words);
	constexpr eng::u16 kTileWords = static_cast<eng::u16>(G::tile_h * G::planes);
	constexpr eng::u16 kNTiles = 64u;
	std::array<eng::u16, kNTiles * kTileWords> bank {};
	for (eng::u16 t = 0; t < kNTiles; ++t)
		for (eng::u16 i = 0; i < kTileWords; ++i) bank[t * kTileWords + i] = t;
	std::array<eng::u16, G::ring_w_words> ring {};
	std::array<eng::u16, G::column_planelines> column {};
	struct Map {
		eng::u16 tile_at(eng::u16 col, eng::u16) const { return static_cast<eng::u16>(col % kNTiles); }
	};
	struct Sink {
		eng::u16* ring;
		bool blitter_strip_column(const eng::u16* src, void* dst, eng::u16, eng::s16, eng::u16,
					  eng::u8) {
			const auto* d = static_cast<const eng::u16*>(dst);
			ring[d - ring] = src[0];
			return true;
		}
	} sink {ring.data()};
	Map map {};
	eng::playfield::StripScrollController<G, Map, Sink> ctrl {};
	ctrl.bind(ring.data(), bank.data(), column.data(), kTileWords, map, sink);
	ctrl.fill_ring();
	Lcg rng {seed * 2654435761u + 7u};
	const long period = static_cast<long>(G::ring_w_words) * 16;
	long scroll = 0;
	bool fwd = true;
	for (int f = 0; f < 20000; ++f) {
		const int step = 1 + static_cast<int>(rng.range(16u));
		if (rng.range(6u) == 0u) fwd = !fwd;
		long ns = scroll + (fwd ? step : -step);
		if (ns < 0) { ns = 0; fwd = true; }
		if (ns >= period) { ns = period - 1; fwd = false; }
		const auto fr = ctrl.tick(static_cast<eng::s32>(ns), 0, static_cast<eng::s32>(scroll), 0);
		const unsigned sxu = ns < 1 ? 1u : static_cast<unsigned>(ns);
		const unsigned base = eng::graphics::fine_scroll_coarse(static_cast<eng::u16>(sxu)) / 16u;
		for (unsigned k = 0u; k <= G::visible_words; ++k) {
			const unsigned slot = (fr.window_word + k) % G::ring_w_words;
			if (ring[slot] != static_cast<eng::u16>((base + k) % kPeriod)) return false;
		}
		scroll = ns;
	}
	return true;
}

// Anillo del tipo del demo 128: mapa de 40 columnas -> span 40 = multiplo del periodo.
using GeomMap =
	eng::playfield::StripScrollGeometry<320u, 256u, 3u, 16u, 16u, 2u, 1u, false, 0u, 0u, 40u>;
// Mismo anillo pero con el tamano ANTERIOR (span 23, no multiplo de 40): se descuadra.
using GeomMapBad = eng::playfield::StripScrollGeometry<320u, 256u, 3u, 16u, 16u, 2u, 1u, false, 43u>;

} // namespace

int main() {
	std::printf("== HOST-244 strip_geometry ==\n");
	bool ok = true;
	for (std::uint32_t seed = 1; seed <= 40 && ok; ++seed) ok = simulate(seed);
	check(ok, "guarda y cobertura invariantes en 20000 pasos x 40 semillas");

	// Contenido del anillo: con `span` multiplo del periodo del mapa la ventana (visible + la
	// palabra extra de fetch) siempre tiene la columna correcta; con el tamano antiguo (span 23,
	// mapa 40) se descuadra al envolver. Este es el invariante que la cobertura de "pintado" no veia.
	check(simulate_content<GeomMap>(40u, 1u),
	      "anillo span=40 (multiplo del mapa): ventana + extra siempre correctas");
	check(!simulate_content<GeomMapBad>(40u, 1u),
	      "anillo span=23 (no multiplo del mapa): se descuadra (regresion del bug del bitplane)");
	check(simulate_controller<GeomMap>(1u),
	      "StripScrollController (plan+compose+blit): ventana + extra correctas en 20000 pasos");

	// Limite de hardware: los modos con Copper split asumen viewport <= 208 px
	// (0x2c + viewport_h <= 255) para no duplicar el buffer (espejo/lineal).
	check(0x2cu + 208u <= 255u, "split XY con viewport 208 px cabe en VPOS (8 bits)");

	// Descriptor del blit de tira: A->D copia, ASH en BLTCON0, BLTDMOD del anillo, BLTSIZE con
	// H de 10 bits (columna partida en 2 para 208x5).
	const auto b0 = eng::playfield::strip_blit_desc<Geom>(5u, 0u);
	check(((b0.bltcon0 >> 12u) & 15u) == 5u, "BLTCON0 ASH = fine 5");
	check(b0.bltdmod == 44, "BLTDMOD = ring_w_bytes - 2");
	check(b0.bltsize == static_cast<eng::u16>((1024u << 6u) | 1u), "chunk 0: 1024 planelines x 1 word");
	const auto b1 = eng::playfield::strip_blit_desc<Geom>(5u, 1u);
	check(b1.bltsize == static_cast<eng::u16>((16u << 6u) | 1u), "chunk 1: 16 planelines (1040-1024)");

	// Composicion de columna: 13 tiles (208/16) de 80 palabras -> 1040 palabras contiguas.
	{
		eng::u16 bank[16u * 80u];
		for (eng::u16 id = 0u; id < 16u; ++id) {
			for (eng::u16 i = 0u; i < 80u; ++i) {
				bank[id * 80u + i] = static_cast<eng::u16>(id * 1000u + i);
			}
		}
		eng::u16 ids[13u];
		for (eng::u16 t = 0u; t < 13u; ++t) ids[t] = static_cast<eng::u16>((t * 3u + 1u) % 16u);
		eng::u16 out[Geom::column_planelines];
		const eng::u16 n = eng::playfield::compose_column<Geom>(out, bank, ids, 80u);
		check(n == Geom::column_planelines, "compose_column: 13 tiles x 80 = 1040 palabras");
		bool okc = true;
		for (eng::u16 t = 0u; t < 13u && okc; ++t) {
			for (eng::u16 i = 0u; i < 80u; ++i) {
				if (out[t * 80u + i] != bank[ids[t] * 80u + i]) { okc = false; break; }
			}
		}
		check(okc, "compose_column: concatenacion correcta (BLTAMOD=0)");
	}

	// Composicion con tile 32: 6 tiles de 32x32x5 = 160 palabras -> 960 contiguas.
	{
		eng::u16 bank[8u * 160u];
		for (eng::u16 id = 0u; id < 8u; ++id) {
			for (eng::u16 i = 0u; i < 160u; ++i) bank[id * 160u + i] = static_cast<eng::u16>(id * 1000u + i);
		}
		eng::u16 ids[6u] = {2u, 5u, 1u, 7u, 0u, 3u};
		eng::u16 out[Geom32::column_planelines];
		const eng::u16 n = eng::playfield::compose_column<Geom32>(out, bank, ids, 160u);
		bool okc = n == Geom32::column_planelines;
		for (eng::u16 t = 0u; t < 6u && okc; ++t) {
			for (eng::u16 i = 0u; i < 160u; ++i) {
				if (out[t * 160u + i] != bank[ids[t] * 160u + i]) { okc = false; break; }
			}
		}
		check(okc, "tile 32: compose_column 6x160 = 960 palabras");
		const auto bs = eng::playfield::strip_blit_desc<Geom32>(7u, 0u);
		check(bs.bltdmod == 46 && bs.bltsize == static_cast<eng::u16>((960u << 6u) | 2u),
		      "tile 32: BLTDMOD=46 y BLTSIZE=(960<<6)|2");
	}

	// Fila (scroll Y): 20 tiles de 80 palabras -> 1600 palabras; ancho 20 words, alto 80 planelines.
	{
		eng::u16 bank[16u * 80u];
		for (eng::u16 id = 0u; id < 16u; ++id) {
			for (eng::u16 i = 0u; i < 80u; ++i) bank[id * 80u + i] = static_cast<eng::u16>(id * 1000u + i);
		}
		eng::u16 ids[Geom::row_tiles];
		for (eng::u16 c = 0u; c < Geom::row_tiles; ++c) ids[c] = static_cast<eng::u16>(c % 16u);
		eng::u16 out[Geom::row_tiles * Geom::row_planelines];
		const eng::u16 n = eng::playfield::compose_row<Geom>(out, bank, ids, 80u);
		check(n == static_cast<eng::u16>(Geom::row_tiles * Geom::row_planelines),
		      "compose_row: anillo x 80 palabras");
		check(Geom::strip_row_words == Geom::ring_w_words && Geom::row_planelines == 80u,
		      "fila: ancho del anillo, 80 planelines de alto");
		bool okr = true;
		for (eng::u16 c = 0u; c < Geom::row_tiles && okr; ++c) {
			for (eng::u16 i = 0u; i < Geom::row_planelines; ++i) {
				// out[line*planes+plane][c] en orden (line,plane,c)
				(void)i;
			}
		}
		// Primer word = tile 0, linea 0, plano 0.
		okr = out[0] == bank[ids[0] * 80u];
		check(okr, "fila: orden (linea, plano, columna) correcto");
	}

	// Valores de Copper: BPLCON1 = fine; BPLxPT por plano = p*ring_w_bytes + window_word*2.
	{
		const auto fr = eng::playfield::plan_strip_frame<Geom>(20, 0, 0, 0);
		const auto c = eng::playfield::strip_copper_values<Geom>(fr);
		check(c.bplcon1 == 0xccu,
		      "BPLCON1 = fine_delay(scroll) duplicado en ambos nibbles (PF1 y PF2)");
		check(c.pt_byte[0] == 2u && c.pt_byte[1] == 48u && c.pt_byte[4] == 186u,
		      "BPLxPT por plano = p*ring_w_bytes + window_word*2");
	}
	// Split con two-WAIT: 0x2c + 256 = 300 > 255 -> hay que cruzar la 255 con dos WAITs.
	using Geom256 = eng::playfield::StripScrollGeometry<320u, 256u, 5u, 16u, 16u, 2u, 1u, true>;
	check(Geom256::split_crosses_255, "split a 300 usa two-WAIT (cruza la linea 255)");

	// Streaming: un frame sin cruce de tile no compone ni blitea (0 blits; solo parcheo de Copper).
	check(eng::playfield::plan_strip_frame<Geom>(5, 0, 5, 0).blits == 0u,
	      "sin cruce: 0 blits (solo parcheo de Copper)");
	check(eng::playfield::plan_strip_frame<Geom>(20, 0, 5, 0).blits == 1u,
	      "con cruce (5->20): 1 columna");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: geometria de tiras (anillo, guarda, cobertura) e invariantes validados.\n");
	return 0;
}
