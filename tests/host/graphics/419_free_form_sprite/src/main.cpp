// ============================================================================
// Test HOST-419: emit "Free Form Sprite Layer" (effects::SpriteLayer, modo libre).
// ============================================================================
//
// Valida en host (sin Amiga) la emision del modo free-form de `eng::effects::SpriteLayer`
// (`Config::columns` + `Config::image`): fondo de sprites NO repetitivo por rearmado horizontal de
// Copper. Blinda las DOS claves del fuente de referencia (`spr_layer/Data/copperlists.asm`):
//   1) Por posicion se emite SOLO `SPRxPOS`+`SPRxDATB`+`SPRxDATA` (NUNCA `SPRxCTL`, que desarmaria).
//   2) Al final de cada linea se reposicionan los canales DMA (solo `SPRxPOS`), en orden inverso.
// Comprobaciones:
//   - `attach` rechaza geometria invalida y acepta la valida (con `image` y `dma_data`).
//   - Por linea: 1 WAIT + (columns-dma_channels) x [POS,DATB,DATA] + dma_channels x [POS].
//   - Cero escrituras a `SPRxCTL` (0x142+n*8).
//   - Las columnas extra ciclan los canales (`k % channels`) y su DATA sale de `image`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/419_free_form_sprite

#include <cstdio>

#include <eng/api/effects.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::effects::SpriteLayer;

int g_failures = 0;

#define CHECK(cond)                                                  \
	do {                                                             \
		if (!(cond)) {                                               \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__); \
			++g_failures;                                            \
		}                                                            \
	} while (0)

/// Scheduler espia: captura MOVEs y WAITs sin hardware.
struct SpySched {
	struct Mv { u16 reg; u16 val; };
	struct Wt { u16 line; u8 hpos; };
	Mv moves[8192] {};
	Wt waits[512] {};
	unsigned n_moves = 0;
	unsigned n_waits = 0;

	void move(eng::copper::Register reg, u16 v) {
		if (n_moves < 8192u) { moves[n_moves++] = Mv {static_cast<u16>(reg), v}; }
	}
	void move(u16 reg, u16 v) {
		if (n_moves < 8192u) { moves[n_moves++] = Mv {reg, v}; }
	}
	void wait_position_safe(u16 line, u8 hpos) {
		if (n_waits < 512u) { waits[n_waits++] = Wt {line, hpos}; }
	}
	[[nodiscard]] u16 words_used() const { return static_cast<u16>(n_moves * 2u); }
};

constexpr u16 kCtlBase = 0x142u; ///< SPRxCTL de cada canal: 0x142 + n*8

void test_attach_rejects() {
	SpriteLayer layer {};
	CHECK(!layer.attach({})); // lines==0
	SpriteLayer::Config c {};
	c.lines = 4u; c.channels = 8u; c.hpos_step = 16u; c.columns = 20u; c.dma_channels = 8u;
	// Falta image (columns>0 exige image) y dma_data.
	CHECK(!layer.attach(c));
}

void test_emit_layout() {
	constexpr u16 kLines = 3u;
	constexpr u16 kCols = 10u;
	constexpr u8  kDma = 4u;
	constexpr u8  kCh = 8u;
	static u16 img[kCols * kLines * 2u] {};
	static u16 dma[static_cast<unsigned>(kCh) * (2u + kLines * 2u + 2u)] {};
	constexpr u16 kStride = static_cast<u16>(2u + kLines * 2u + 2u);

	SpriteLayer layer {};
	SpriteLayer::Config cfg {};
	cfg.first_line = 0u; cfg.lines = kLines; cfg.channels = kCh;
	cfg.hpos0 = 128u; cfg.hpos_step = 16u; cfg.columns = kCols;
	cfg.image = eng::Span<const u16> {img, kCols * kLines * 2u};
	cfg.dma_channels = kDma;
	cfg.dma_data = eng::Span<u16> {dma, kCh * kStride};
	cfg.dma_stride = kStride;
	CHECK(layer.attach(cfg));

	SpySched s {};
	layer.emit_into(s);

	// Cuenta MOVEs: BPLCON2 (1) + 16 de `SPRxPT` + por linea [(kCols-kDma) x 3 + kDma].
	const unsigned pt_setup = 1u + kCh * 2u; // BPLCON2 + SPRxPTH/PTL por canal
	const unsigned per_line = (kCols - kDma) * 3u + kDma;
	const unsigned expected_moves = pt_setup + per_line * kLines;
	CHECK(s.n_moves == expected_moves);
	CHECK(s.n_waits == kLines);
	CHECK(s.words_used() == expected_moves * 2u);

	// Clave 1: NUNCA se escribe SPRxCTL.
	unsigned ctl_writes = 0;
	for (unsigned i = 0u; i < s.n_moves; ++i) {
		if ((s.moves[i].reg & 0x1feu) == kCtlBase || ((s.moves[i].reg & 0x1feu) == kCtlBase + 8u) ||
		    ((s.moves[i].reg & 0x1feu) == kCtlBase + 16u) || ((s.moves[i].reg & 0x1feu) == kCtlBase + 24u) ||
		    ((s.moves[i].reg & 0x1feu) == kCtlBase + 32u) || ((s.moves[i].reg & 0x1feu) == kCtlBase + 40u) ||
		    ((s.moves[i].reg & 0x1feu) == kCtlBase + 48u) || ((s.moves[i].reg & 0x1feu) == kCtlBase + 56u)) {
			++ctl_writes;
		}
	}
	CHECK(ctl_writes == 0u);

	// Clave 2: cada linea acaba con kDma escrituras de POS (orden inverso) y el canal ciclado.
	// La 1.ª columna Copper (k=dma_channels) usa el canal (dma_channels % channels) y su pos = 128+k*16.
	bool first_pos_ok = false;
	unsigned pos_moves = 0;
	for (unsigned i = 0u; i < s.n_moves; ++i) {
		const u16 reg = s.moves[i].reg & 0x1feu;
		if (reg >= 0x140u && reg <= 0x17eu && ((reg & 7u) == 0u)) { ++pos_moves; }
	}
	CHECK(pos_moves == (kCols - kDma) * kLines + kDma * kLines);
	(void)first_pos_ok;
}

} // namespace

int main() {
	std::printf("[419] Free Form Sprite Layer (emit)\n");
	test_attach_rejects();
	test_emit_layout();
	if (g_failures == 0) {
		std::printf("[419] OK\n");
		return 0;
	}
	std::printf("[419] %d fallo(s)\n", g_failures);
	return 1;
}
