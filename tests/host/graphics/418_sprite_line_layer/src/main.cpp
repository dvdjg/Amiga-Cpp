// ============================================================================
// Test HOST-418: capa/HUD de sprites por parcheo de POS+DATA por línea.
// ============================================================================
//
// Valida en host el módulo puro `eng/graphics/sprite_line_layer.hpp`
// (`SpriteLineLayer`): arma, en cada línea de un tramo, un `SpriteHorizontalRearm`
// por canal (X + DATA de ESA línea) y lo entrega a un scheduler espía. Es el patrón
// Parasol Stars (HUD de un sprite) / Brian the Lion (DATA distinta por línea).
//
// Comprobaciones:
//   1) `configure` rechaza rango/canales/hpos_step inválidos.
//   2) Emisión: un rearm por (línea, canal), orden línea→canal, `hpos` creciente
//      dentro de la línea, `vstart`/`vstop` = línea y línea+1.
//   3) Tabla de DATA por línea vs. par constante.
//   4) `scroll` desplaza la X y los canales fuera de pantalla se omiten.
//   5) `attach` se propaga; `rearm_count` cuenta rearmes.
//   6) Límites de hardware coherentes (`sprite_limits.hpp`).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/418_sprite_line_layer   (solo este)
//   bash tools/run-host-tests.sh                                            (todos)

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/sprite_line_layer.hpp>
#include <eng/graphics/sprite_limits.hpp>

namespace {

using eng::graphics::SpriteHorizontalRearm;
using eng::graphics::SpriteLineLayer;
using eng::graphics::SpriteLineLayerConfig;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

// Scheduler espía: guarda los MOVEs (armado POS+CTL+PT) y las reposiciones POS-only que
// `SpriteLineLayer` habría emitido.
struct SpyMove {
	eng::u16 reg;
	eng::u16 val;
};
struct SpyRepos {
	eng::u8 channel;
	eng::u8 vline;
	eng::u16 hpos;
};
struct SpyScheduler {
	static constexpr eng::u16 kMax = 256;
	SpyMove moves[kMax] {};
	eng::u16 move_count = 0;
	SpyRepos repos[kMax] {};
	eng::u16 repo_count = 0;
	eng::u16 waits = 0;

	void wait_line_safe(eng::u16) { ++waits; }
	void move(eng::copper::Register reg, eng::u16 v) {
		if (move_count < kMax) {
			moves[move_count++] = SpyMove {static_cast<eng::u16>(reg), v};
		}
	}
	void emit_sprite_horizontal_reposition(eng::u8 ch, eng::u8 vline, eng::u16 hpos) {
		if (repo_count < kMax) {
			repos[repo_count++] = SpyRepos {ch, vline, hpos};
		}
	}
};

void test_constants() {
	std::printf("sprite_limits: valores coherentes\n");
	CHECK(eng::graphics::kSpriteChannels == 8u);
	CHECK(eng::graphics::kSpriteMaxPerLine == eng::graphics::kSpriteChannels);
	CHECK(eng::graphics::kSpriteMinReusePx == 24u);
	CHECK(eng::graphics::kSpriteLineDataMaxBitplanes == 5u);
}

void test_configure_rejects() {
	std::printf("configure: rechaza configuraciones invalidas\n");

	// Tabla de DATA válida (la emisión la exige tipada: es la estructura de `SPRxPT`).
	alignas(16) static eng::u16 data[2 * 8 * 2] {};
	const eng::ChipView<eng::SpriteTag> data_view {
		eng::Address<eng::MemoryKind::Chip>::from_storage(data), sizeof(data)};

	SpriteLineLayer layer {};

	SpriteLineLayerConfig no_lines {};
	no_lines.channels = 2u;
	CHECK(!layer.configure(no_lines));

	SpriteLineLayerConfig no_channels {};
	no_channels.lines = 4u;
	CHECK(!layer.configure(no_channels));

	SpriteLineLayerConfig zero_step {};
	zero_step.lines = 4u;
	zero_step.channels = 1u;
	zero_step.hpos_step = 0u;
	CHECK(!layer.configure(zero_step));

	// Sin tabla de DATA (o corta): rechazado (la forma parcial no existe).
	SpriteLineLayerConfig no_data {};
	no_data.lines = 4u;
	no_data.channels = 2u;
	CHECK(!layer.configure(no_data));
	no_data.line_data = data_view.subview(0u, 2u * 4u * 2u - 2u); // una palabra menos
	CHECK(!layer.configure(no_data));

	SpriteLineLayerConfig bad_channels {};
	bad_channels.lines = 4u;
	bad_channels.channel_first = 6u;
	bad_channels.channels = 4u; // 6..9 se sale de 0..7
	CHECK(!layer.configure(bad_channels));

	SpriteLineLayerConfig bad_range {};
	bad_range.lines = 300u;
	bad_range.first_line = 300u; // 300+300 > 512
	bad_range.channels = 1u;
	CHECK(!layer.configure(bad_range));

	// Planos del display: el parcheo de DATA a media línea exige hueco de DMA (≤ 5 planos;
	// check_sprite_line_data, custom.cpp:10111/10130-10131).
	SpriteLineLayerConfig six_planes {};
	six_planes.lines = 4u;
	six_planes.channels = 1u;
	six_planes.display_planes = 6u;
	six_planes.line_data = data_view;
	CHECK(!layer.configure(six_planes));
	six_planes.display_planes = 5u;
	CHECK(layer.configure(six_planes));

	// Una válida sí entra.
	SpriteLineLayerConfig ok {};
	ok.first_line = 10u;
	ok.lines = 3u;
	ok.channel_first = 4u;
	ok.channels = 2u;
	ok.hpos0 = 100u;
	ok.hpos_step = 16u;
	ok.line_data = data_view;
	CHECK(layer.configure(ok));
	CHECK(layer.rearm_count() == 6u);
}

void test_emit_per_line_data() {
	std::printf("emit: armado unico por canal (POS+CTL+PT) + reposicion por linea\n");

	// 2 canales x 3 lineas; tabla `[channel][line][2]` (la estructura que recibe SPRxPT).
	alignas(16) static eng::u16 data[2 * 3 * 2] {};
	for (eng::u16 c = 0; c < 2u; ++c) {
		for (eng::u16 l = 0; l < 3u; ++l) {
			const eng::u32 i = (static_cast<eng::u32>(c) * 3u + l) * 2u;
			data[i] = static_cast<eng::u16>(0x1000u + l * 16u + c);     // DAT
			data[i + 1u] = static_cast<eng::u16>(0x2000u + l * 16u + c); // DATB
		}
	}

	SpriteLineLayer layer {};
	SpriteLineLayerConfig cfg {};
	cfg.first_line = 100u;
	cfg.lines = 3u;
	cfg.channel_first = 4u;
	cfg.channels = 2u;
	cfg.hpos0 = 50u;
	cfg.hpos_step = 16u;
	cfg.line_data = eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(data), sizeof(data)};
	CHECK(layer.configure(cfg));

	SpyScheduler spy {};
	layer.emit_into(spy);

	// Un armado por canal: WAIT + POS + CTL + PTH + PTL.
	CHECK(spy.waits == 2u);
	CHECK(spy.move_count == 8u);
	CHECK(spy.repo_count == 6u);

	const eng::u32 base = static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(data));
	for (eng::u16 c = 0u; c < 2u; ++c) {
		const eng::u16 ch = static_cast<eng::u16>(4u + c);
		const eng::u16 hpos = static_cast<eng::u16>(50u + c * 16u);
		const eng::u16 pos_expected =
			static_cast<eng::u16>((100u << 8u) | ((hpos >> 1u) & 0xffu));
		const eng::u16 ctl_expected = static_cast<eng::u16>(103u << 8u); // VSTOP=103
		const eng::u32 addr = base + static_cast<eng::u32>(c) * 3u * 4u;
		const SpyMove* m = &spy.moves[static_cast<eng::u32>(c) * 4u];
		CHECK(m[0].reg == 0x140u + ch * 8u && m[0].val == pos_expected);
		CHECK(m[1].reg == 0x142u + ch * 8u && m[1].val == ctl_expected);
		CHECK(m[2].reg == 0x120u + ch * 8u && m[2].val == (addr >> 16u));
		CHECK(m[3].reg == 0x122u + ch * 8u && m[3].val == (addr & 0xffffu));
	}
	// Reposiciones: por línea (100..102), canales 4 y 5 con X creciente (WAIT monotono).
	for (eng::u16 l = 0u; l < 3u; ++l) {
		const SpyRepos& r0 = spy.repos[l * 2u + 0u];
		const SpyRepos& r1 = spy.repos[l * 2u + 1u];
		CHECK(r0.channel == 4u && r1.channel == 5u);
		CHECK(r0.vline == 100u + l && r1.vline == 100u + l);
		CHECK(r0.hpos == 50u && r1.hpos == 66u);
		CHECK(r1.hpos > r0.hpos);
	}
}

void test_attach_and_scroll() {
	std::printf("emit: attach y scroll\n");

	alignas(16) static eng::u16 data[2 * 2] {0xaaaa, 0x5555, 0xbbbb, 0x6666};
	SpriteLineLayer layer {};
	SpriteLineLayerConfig cfg {};
	cfg.first_line = 0u;
	cfg.lines = 2u;
	cfg.channel_first = 0u;
	cfg.channels = 1u;
	cfg.hpos0 = 40u;
	cfg.hpos_step = 16u;
	cfg.attach = true;
	cfg.line_data = eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(data), sizeof(data)};
	CHECK(layer.configure(cfg));

	SpyScheduler spy {};
	layer.emit_into(spy);
	CHECK(spy.move_count == 4u);
	CHECK(spy.repo_count == 2u);
	// CTL con ATTACH (bit 7): VSTOP=2 -> 0x0280.
	CHECK(spy.moves[1].reg == 0x142u && spy.moves[1].val == 0x0280u);

	// Scroll: +10 px desplaza el armado y ambas reposiciones.
	layer.set_scroll(10u);
	SpyScheduler spy2 {};
	layer.emit_into(spy2);
	CHECK(spy2.moves[0].val == static_cast<eng::u16>((0u << 8u) | (30u >> 1u)));
	CHECK(spy2.repos[0].hpos == 30u && spy2.repos[1].hpos == 30u);

	// Scroll que saca el canal por la izquierda: no se emite nada.
	layer.set_scroll(100u);
	SpyScheduler spy3 {};
	layer.emit_into(spy3);
	CHECK(spy3.waits == 0u && spy3.move_count == 0u && spy3.repo_count == 0u);
}

} // namespace

int main() {
	std::printf("Test HOST-418 sprite_line_layer (capa/HUD por parcheo por linea)\n");
	std::printf("==============================================================\n");

	test_constants();
	test_configure_rejects();
	test_emit_per_line_data();
	test_attach_and_scroll();

	if (g_failures == 0) {
		std::printf("OK: capa de sprites por parcheo de POS+DATA por linea validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
