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

// Scheduler espía: guarda la lista que `SpriteLineLayer` habría emitido.
struct SpyScheduler {
	static constexpr eng::u16 kMax = 512;
	SpriteHorizontalRearm items[kMax] {};
	eng::u16 count = 0;

	void emit_sprite_horizontal_rearm(const SpriteHorizontalRearm& r) {
		if (count < kMax) {
			items[count++] = r;
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

	// Una válida sí entra.
	SpriteLineLayerConfig ok {};
	ok.first_line = 10u;
	ok.lines = 3u;
	ok.channel_first = 4u;
	ok.channels = 2u;
	ok.hpos0 = 100u;
	ok.hpos_step = 16u;
	CHECK(layer.configure(ok));
	CHECK(layer.rearm_count() == 6u);
}

void test_emit_per_line_data() {
	std::printf("emit: un rearm por (linea,canal) con DATA por linea\n");

	// 3 líneas x 2 canales; DATA distinta por línea.
	eng::u16 data[3 * 2 * 2] {};
	for (eng::u16 l = 0; l < 3u; ++l) {
		for (eng::u16 c = 0; c < 2u; ++c) {
			const eng::u32 i = (static_cast<eng::u32>(l) * 2u + c) * 2u;
			data[i] = static_cast<eng::u16>(0x1000u + l * 16u + c); // DAT
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
	cfg.line_data = data;
	CHECK(layer.configure(cfg));

	SpyScheduler spy {};
	layer.emit_into(spy);
	CHECK(spy.count == 6u);

	// Orden: línea 0 (canales 4 y 5), luego línea 1, luego línea 2.
	for (eng::u16 l = 0; l < 3u; ++l) {
		for (eng::u16 c = 0; c < 2u; ++c) {
			const SpriteHorizontalRearm& r = spy.items[l * 2u + c];
			CHECK(r.channel == static_cast<eng::u8>(4u + c));
			CHECK(r.vstart == 100u + l);
			CHECK(r.vstop == 100u + l + 1u);
			CHECK(r.hpos == static_cast<eng::u16>(50u + c * 16u));
			CHECK(r.data_high == static_cast<eng::u16>(0x1000u + l * 16u + c));
			CHECK(r.data_low == static_cast<eng::u16>(0x2000u + l * 16u + c));
		}
		// Dentro de cada línea, la X crece con el canal (WAIT monotono).
		CHECK(spy.items[l * 2u + 1u].hpos > spy.items[l * 2u].hpos);
	}
}

void test_constant_data_and_attach() {
	std::printf("emit: DATA constante, attach y scroll\n");

	SpriteLineLayer layer {};
	SpriteLineLayerConfig cfg {};
	cfg.first_line = 0u;
	cfg.lines = 2u;
	cfg.channel_first = 0u;
	cfg.channels = 1u;
	cfg.hpos0 = 40u;
	cfg.hpos_step = 16u;
	cfg.data_high = 0xaaaa;
	cfg.data_low = 0x5555;
	cfg.attach = true;
	CHECK(layer.configure(cfg));

	SpyScheduler spy {};
	layer.emit_into(spy);
	CHECK(spy.count == 2u);
	CHECK(spy.items[0].data_high == 0xaaaau && spy.items[1].data_low == 0x5555u);
	CHECK(spy.items[0].attach && spy.items[1].attach);

	// Scroll: +10 px desplaza ambas X.
	layer.set_scroll(10u);
	SpyScheduler spy2 {};
	layer.emit_into(spy2);
	CHECK(spy2.items[0].hpos == 30u);
	CHECK(spy2.items[1].hpos == 30u);

	// Scroll que saca el canal por la izquierda: no se emite esa línea.
	layer.set_scroll(100u);
	SpyScheduler spy3 {};
	layer.emit_into(spy3);
	CHECK(spy3.count == 0u);
}

} // namespace

int main() {
	std::printf("Test HOST-418 sprite_line_layer (capa/HUD por parcheo por linea)\n");
	std::printf("==============================================================\n");

	test_constants();
	test_configure_rejects();
	test_emit_per_line_data();
	test_constant_data_and_attach();

	if (g_failures == 0) {
		std::printf("OK: capa de sprites por parcheo de POS+DATA por linea validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
