// ============================================================================
// Test HOST-244: geometria e invariantes del scroller de tiras (referencia).
// ============================================================================
//
// Codifica como codigo verificable la referencia del "Copper ring + incoming
// strip": constantes compile-time (static_assert) y, sobre un modelo de anillo en
// sombra, los invariantes:
//   - la columna destino del blit (guarda) NUNCA esta en la ventana visible,
//   - tras cada cruce de palabra, TODA la ventana visible esta pintada,
//   - como maximo 1 blit por frame (X-only) en cualquier secuencia de pasos 1..16.
//
//   bash tools/run-host-tests.sh tests/host/field/244_strip_geometry

#include <array>
#include <cstdint>
#include <cstdio>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

// --- Constantes de la referencia (5 planos, 320x256, tile 16x16) --------------
constexpr unsigned VIEWPORT_W = 320u, VIEWPORT_H = 256u, PLANES = 5u;
constexpr unsigned TILE_W = 16u, TILE_H = 16u;
constexpr unsigned GUARD_WORDS = 2u, FETCH_EXTRA = 1u;
constexpr unsigned RING_W_WORDS = VIEWPORT_W / TILE_W + GUARD_WORDS + FETCH_EXTRA; // 23
constexpr unsigned RING_W_BYTES = RING_W_WORDS * 2u;                              // 46
constexpr unsigned VISIBLE_WORDS = VIEWPORT_W / TILE_W;                           // 20
constexpr unsigned BPL_MOD = (PLANES - 1u) * RING_W_BYTES;                        // 184
constexpr unsigned BLTDMOD_COL = RING_W_BYTES - 2u;                               // 44
constexpr unsigned COLUMN_PLANELINES = (VIEWPORT_H / TILE_H) * TILE_H * PLANES;   // 1280
constexpr unsigned COLUMN_BYTES = RING_W_WORDS * 2u * (VIEWPORT_H / TILE_H) * PLANES; // guard strip bytes

static_assert(RING_W_WORDS == 23u, "ring 368 px = 23 words");
static_assert(RING_W_BYTES == 46u, "ring = 46 bytes/planeline");
static_assert(VISIBLE_WORDS == 20u, "320 px = 20 words");
static_assert(BPL_MOD == 184u, "BPL1MOD/BPL2MOD interleaved");
static_assert(BLTDMOD_COL == 44u, "BLTDMOD columna alta");
static_assert(COLUMN_PLANELINES == 1280u, "columna = 16 tiles x 80 planelines");
static_assert(GUARD_WORDS * 16u >= 16u + 16u, "guarda >= ceil(max_step/16)+1");
// Split XY invalido en OCS: SPLIT_LINE = 0x2c + 256 = 300 > 255 (VPOS 8 bits).
static_assert(0x2cu + VIEWPORT_H > 255u, "OCS: el split a 0x2c+256 no cabe en VPOS (8 bits)");

struct Lcg {
	std::uint32_t s;
	std::uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
	std::uint32_t range(std::uint32_t n) { return next() % n; }
};

// Simula una secuencia aleatoria de pasos 1..16 (adelante/atras) sobre el anillo en
// sombra y comprueba los invariantes.
bool simulate(std::uint32_t seed) {
	Lcg rng {seed * 2654435761u + 12345u};
	std::array<bool, RING_W_WORDS> painted {};
	for (unsigned w = 0; w < VISIBLE_WORDS; ++w) painted[w] = true; // pantalla inicial pintada
	const long period = static_cast<long>(RING_W_WORDS) * 16;
	long scroll = 0;
	for (int f = 0; f < 20000; ++f) {
		const int step = 1 + static_cast<int>(rng.range(16u));
		const bool fwd = (rng.next() & 1u) != 0u;
		long nscroll = scroll + (fwd ? step : -step);
		nscroll = ((nscroll % period) + period) % period;
		const unsigned w_old = static_cast<unsigned>(scroll / 16);
		const unsigned w_new = static_cast<unsigned>(nscroll / 16);
		if (w_new != w_old) {
			// La columna entrante: adelante = borde derecho (w_old+VISIBLE_WORDS), atras = borde
			// izquierdo (w_old-1). Debe caer FUERA de la ventana visible actual.
			const unsigned dest = fwd ? (w_old + VISIBLE_WORDS) % RING_W_WORDS
						  : (w_old + RING_W_WORDS - 1u) % RING_W_WORDS;
			for (unsigned k = 0; k < VISIBLE_WORDS; ++k) {
				if (dest == (w_old + k) % RING_W_WORDS) return false; // destino dentro de la ventana
			}
			painted[dest] = true;
		}
		scroll = nscroll;
		// Tras el cruce, TODA la ventana visible debe estar pintada.
		const unsigned w = static_cast<unsigned>(scroll / 16);
		for (unsigned k = 0; k < VISIBLE_WORDS; ++k) {
			if (!painted[(w + k) % RING_W_WORDS]) return false;
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

	// Limite de hardware documentado: el split XY en OCS no cabe (VPOS 8 bits).
	check(0x2cu + VIEWPORT_H > 255u, "split XY (5 planes) excede VPOS de 8 bits -> usar mirror/linear");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: geometria de tiras (anillo, guarda, cobertura) e invariantes validados.\n");
	return 0;
}
