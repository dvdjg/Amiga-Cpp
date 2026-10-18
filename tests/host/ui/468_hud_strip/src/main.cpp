// ============================================================================
// Test HOST-468: franja del HUD (`eng/hud/strip.hpp`, `HudStrip` + `apply_hud_ops`).
// ============================================================================
//
// Fase H1 del roadmap (`docs/guides/roadmap/ROADMAP_HUD.md`): ligado del lienzo Chip,
// validacion de configuracion y tamano del bloque, limpieza con el fondo del tema y
// materializacion de operaciones (`Fill`, `Frame`, `Text`/`Digits`) sobre la `Surface`
// de la banda, con recorte. La ruta CPU es la referencia de H1; el compositor publica
// la banda (no lo toca este test).
//
//   bash tools/run-host-tests.sh tests/host/ui/468_hud_strip

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/hud/strip.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

constexpr eng::u16 kW = 320u;
constexpr eng::u16 kH = 48u;
constexpr eng::u8 kPlanes = 2u;
constexpr eng::u16 kRowBytes = static_cast<eng::u16>((kW / 8u) & ~1u); // 40 B por fila y plano
constexpr eng::u32 kBytes = static_cast<eng::u32>(kRowBytes) * kPlanes * kH;

alignas(16) eng::u8 g_chip[64u * 1024u];

/// Pixel del lienzo interleaved (`planeline = y*planes + p`, bit 15 = x=0 de la palabra).
eng::u8 pixel(const eng::u8* base, eng::s16 x, eng::s16 y) {
	eng::u8 c = 0u;
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		const eng::u16* w = reinterpret_cast<const eng::u16*>(
			base + (static_cast<eng::u32>(y) * kPlanes + p) * kRowBytes +
			static_cast<eng::u32>(x / 16) * 2u);
		const eng::u8 bit = static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u);
		c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
	}
	return c;
}

/// Operacion de relleno en coordenadas de banda.
eng::hud::HudDrawOp fill_op(eng::s16 x, eng::s16 y, eng::u16 w, eng::u16 h, eng::u8 color) {
	eng::hud::HudDrawOp op {};
	op.kind = eng::hud::HudOpKind::Fill;
	op.box = eng::Box {x, y, w, h};
	op.color = color;
	return op;
}

/// Ligado, validacion de configuracion y limpieza con el fondo.
void test_bind_and_clear(eng::MemoryManager& mem) {
	eng::hud::HudStrip strip;
	CHECK(!strip.ok());
	eng::hud::HudStripConfig cfg {};
	cfg.width = kW;
	cfg.height = kH;
	cfg.planes = kPlanes;
	cfg.background = 3u;
	auto block = mem.chip().reserve<eng::PlaneTag>(kBytes, 16u);
	CHECK(block.valid());
	CHECK(strip.bind(block, cfg));
	CHECK(strip.ok());
	CHECK(strip.config().height == kH);
	CHECK(strip.bounds().w == kW && strip.bounds().h == kH);

	eng::hud::HudStripConfig bad = cfg;
	bad.planes = 0u;
	CHECK(!strip.bind(block, bad));
	bad = cfg;
	bad.planes = 7u;
	CHECK(!strip.bind(block, bad));
	bad = cfg;
	bad.width = 0u;
	CHECK(!strip.bind(block, bad));

	auto small = mem.chip().reserve<eng::PlaneTag>(64u, 16u);
	CHECK(!strip.bind(small, cfg)); // el bloque no cubre el lienzo

	// La ultima bind valida es la del bloque pequeno y dejo `m_ok=false`? No: bind falla
	// antes de tocar el estado; revalidamos con el bloque bueno.
	CHECK(strip.bind(block, cfg));
	strip.clear();
	CHECK(pixel(block.data(), 5, 5) == 3u);
	CHECK(pixel(block.data(), 315, 45) == 3u);
}

/// Materializacion de operaciones y recorte.
void test_apply(eng::MemoryManager& mem) {
	eng::hud::HudStrip strip;
	eng::hud::HudStripConfig cfg {};
	cfg.width = kW;
	cfg.height = kH;
	cfg.planes = kPlanes;
	cfg.background = 0u;
	auto block = mem.chip().reserve<eng::PlaneTag>(kBytes, 16u);
	CHECK(strip.bind(block, cfg));
	strip.clear();

	// Fill dentro.
	const eng::hud::HudDrawOp ops1[] = {fill_op(10, 10, 20u, 8u, 2u)};
	CHECK(strip.apply(eng::Span<const eng::hud::HudDrawOp> {ops1, 1u}) == 1u);
	CHECK(pixel(block.data(), 10, 10) == 2u);
	CHECK(pixel(block.data(), 29, 17) == 2u);
	CHECK(pixel(block.data(), 30, 18) == 0u);

	// Fill que rebasa el clip: pinta lo visible y no cuenta como aplicada.
	const eng::hud::HudDrawOp ops2[] = {fill_op(310, 40, 20u, 16u, 1u)};
	CHECK(strip.apply(eng::Span<const eng::hud::HudDrawOp> {ops2, 1u}) == 0u);
	CHECK(pixel(block.data(), 315, 45) == 1u);
	CHECK(pixel(block.data(), 5, 5) == 0u);

	// Frame: borde pintado, interior intacto.
	eng::hud::HudDrawOp frame {};
	frame.kind = eng::hud::HudOpKind::Frame;
	frame.box = eng::Box {50, 4, 12u, 10u};
	frame.color = 2u;
	CHECK(strip.apply(eng::Span<const eng::hud::HudDrawOp> {&frame, 1u}) == 1u);
	CHECK(pixel(block.data(), 50, 4) == 2u);
	CHECK(pixel(block.data(), 61, 13) == 2u);
	CHECK(pixel(block.data(), 55, 8) == 0u);

	// Texto: algo se pinta (glifo Font8) y una op vacia no cuenta.
	char msg[3] = {'A', 'B', '\0'};
	eng::hud::HudDrawOp text {};
	text.kind = eng::hud::HudOpKind::Text;
	text.box = eng::Box {100, 4, 16u, 8u};
	text.color = 3u;
	CHECK(text.set_text(eng::Span<const char> {msg, 2u}));
	const eng::hud::HudDrawOp empty {};
	const eng::hud::HudDrawOp ops4[] = {text, empty};
	CHECK(strip.apply(eng::Span<const eng::hud::HudDrawOp> {ops4, 2u}) == 1u);
	eng::u16 painted = 0u;
	for (eng::s16 x = 100; x < 116; ++x) {
		for (eng::s16 y = 4; y < 12; ++y) {
			if (pixel(block.data(), x, y) == 3u) {
				++painted;
			}
		}
	}
	CHECK(painted > 0u);

	// Texto en celda alineada a 16 px: ruta `draw_text_words` (palabra completa por fila).
	char msg2[3] = {'A', 'B', '\0'};
	eng::hud::HudDrawOp textw {};
	textw.kind = eng::hud::HudOpKind::Text;
	textw.box = eng::Box {96, 20, 16u, 8u};
	textw.color = 2u;
	CHECK(textw.set_text(eng::Span<const char> {msg2, 2u}));
	const eng::hud::HudDrawOp bgfill = fill_op(96, 20, 16u, 8u, 3u);
	CHECK(strip.apply(eng::Span<const eng::hud::HudDrawOp> {&bgfill, 1u}) == 1u);
	CHECK(strip.apply(eng::Span<const eng::hud::HudDrawOp> {&textw, 1u}) == 1u);
	eng::u16 glyph_px = 0u;
	eng::u16 clear_px = 0u;
	for (eng::s16 x = 96; x < 112; ++x) {
		for (eng::s16 y = 20; y < 28; ++y) {
			const eng::u8 c = pixel(block.data(), x, y);
			if (c == 2u) {
				++glyph_px;
			} else if (c == 0u) {
				++clear_px;
			}
		}
	}
	CHECK(glyph_px > 0u); // texto por palabras: glifos pintados
	CHECK(clear_px > 0u); // texto por palabras: la palabra sustituye el fondo de la celda
	CHECK(pixel(block.data(), 95, 24) == 0u && pixel(block.data(), 112, 24) == 0u); // no toca fuera

	// Blit/Sprite/Palette se omiten en H1 (no cuentan).
	eng::hud::HudDrawOp sprite {};
	sprite.kind = eng::hud::HudOpKind::Sprite;
	sprite.box = eng::Box {0, 0, 16u, 16u};
	CHECK(strip.apply(eng::Span<const eng::hud::HudDrawOp> {&sprite, 1u}) == 0u);
}

} // namespace

int main() {
	eng::MemoryManager mem;
	mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	test_bind_and_clear(mem);
	test_apply(mem);
	if (g_fail == 0) {
		std::printf("HOST-468 hud_strip: OK\n");
		return 0;
	}
	std::printf("HOST-468 hud_strip: %d fallos\n", g_fail);
	return 1;
}
