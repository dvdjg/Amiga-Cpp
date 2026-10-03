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
using Geom = eng::field::StripScrollGeometry<320u, 208u, 5u, 16u, 16u, 2u, 1u, false>;
// Variante XY (anillo vertical + split); 208 -> cabe en VPOS.
using GeomXY = eng::field::StripScrollGeometry<320u, 208u, 5u, 16u, 16u, 2u, 1u, true>;
// Tile 32: guarda 64 px = 4 palabras; viewport 192 (multiplo de 32).
using Geom32 = eng::field::StripScrollGeometry<320u, 192u, 5u, 32u, 32u, 4u, 1u, false>;

static_assert(Geom::visible_words == 20u, "320 px = 20 words");
static_assert(Geom::ring_w_words == 23u, "anillo = 20 + 2 guarda + 1 fetch");
static_assert(Geom::ring_w_bytes == 46u, "46 B/planeline");
static_assert(Geom::bpl_mod == 184u, "BPL1MOD/BPL2MOD interleaved");
static_assert(Geom::bltdmod_col == 44u, "BLTDMOD columna (tile 16)");
static_assert(Geom::column_planelines == 1040u, "208 lineas x 5 planos");
static_assert(Geom32::bltdmod_col == 46u, "BLTDMOD columna (tile 32, anillo 50 B)");
static_assert(GeomXY::ring_h == 240u, "anillo vertical = 208 + 2*16");
// BLTSIZE H es de 10 bits (max 1024): columna 208x5 = 1040 -> 2 blits; 192x5=960 -> 1.
static_assert(Geom::column_planelines == 1040u, "208 x 5 planos");
static_assert(Geom::column_blits == 2u, "1040 > 1024 -> 2 blits");
static_assert(Geom32::column_planelines == 960u, "192 x 5 planos");
static_assert(Geom32::column_blits == 1u, "960 <= 1024 -> 1 blit");
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
		const auto fr = eng::field::plan_strip_frame<Geom>(
			static_cast<eng::s32>(ns), 0, static_cast<eng::s32>(scroll), 0);
		check(fr.blits <= 2u, "<= 2 blits/frame");
		const eng::u16 old_window = static_cast<eng::u16>((static_cast<eng::u32>(scroll) / 16u) %
								  Geom::ring_w_words);
		if (fr.column_crossed) {
			// La columna destino debe caer FUERA de la ventana visible ACTUAL (invisible).
			if (!eng::field::strip_dest_is_guard(fr.col_dest_word, old_window,
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

} // namespace

int main() {
	std::printf("== HOST-244 strip_geometry ==\n");
	bool ok = true;
	for (std::uint32_t seed = 1; seed <= 40 && ok; ++seed) ok = simulate(seed);
	check(ok, "guarda y cobertura invariantes en 20000 pasos x 40 semillas");

	// Limite de hardware: los modos con Copper split asumen viewport <= 208 px
	// (0x2c + viewport_h <= 255) para no duplicar el buffer (espejo/lineal).
	check(0x2cu + 208u <= 255u, "split XY con viewport 208 px cabe en VPOS (8 bits)");

	// Descriptor del blit de tira: A->D copia, ASH en BLTCON0, BLTDMOD del anillo, BLTSIZE con
	// H de 10 bits (columna partida en 2 para 208x5).
	const auto b0 = eng::field::strip_blit_desc<Geom>(5u, 0u);
	check(((b0.bltcon0 >> 12u) & 15u) == 5u, "BLTCON0 ASH = fine 5");
	check(b0.bltdmod == 44, "BLTDMOD = ring_w_bytes - 2");
	check(b0.bltsize == static_cast<eng::u16>((1024u << 6u) | 1u), "chunk 0: 1024 planelines x 1 word");
	const auto b1 = eng::field::strip_blit_desc<Geom>(5u, 1u);
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
		const eng::u16 n = eng::field::compose_column<Geom>(out, bank, ids, 80u);
		check(n == Geom::column_planelines, "compose_column: 13 tiles x 80 = 1040 palabras");
		bool okc = true;
		for (eng::u16 t = 0u; t < 13u && okc; ++t) {
			for (eng::u16 i = 0u; i < 80u; ++i) {
				if (out[t * 80u + i] != bank[ids[t] * 80u + i]) { okc = false; break; }
			}
		}
		check(okc, "compose_column: concatenacion correcta (BLTAMOD=0)");
	}

	// Valores de Copper: BPLCON1 = fine; BPLxPT por plano = p*ring_w_bytes + window_word*2.
	{
		const auto fr = eng::field::plan_strip_frame<Geom>(5, 0, 0, 0);
		const auto c = eng::field::strip_copper_values<Geom>(fr);
		check(c.bplcon1 == 5u, "BPLCON1 = fine scroll");
		check(c.pt_byte[0] == 0u && c.pt_byte[1] == 46u && c.pt_byte[4] == 184u,
		      "BPLxPT por plano = p*ring_w_bytes + window*2");
	}
	// Split con two-WAIT: 0x2c + 256 = 300 > 255 -> hay que cruzar la 255 con dos WAITs.
	using Geom256 = eng::field::StripScrollGeometry<320u, 256u, 5u, 16u, 16u, 2u, 1u, true>;
	check(Geom256::split_crosses_255, "split a 300 usa two-WAIT (cruza la linea 255)");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: geometria de tiras (anillo, guarda, cobertura) e invariantes validados.\n");
	return 0;
}
