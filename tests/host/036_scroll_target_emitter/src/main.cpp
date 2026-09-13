// ============================================================================
// Test HOST-036: contrato del scroll separado en ScrollTarget + ScrollEmitter
// ============================================================================
//
// Valida que el contrato del `ScrollEngine` está dividido:
//   ScrollTarget  = geometría del anillo/layout (sin dibujar)
//   ScrollEmitter = dibujo + costura (saveword)
//   ScrollSink    = ScrollTarget && ScrollEmitter
// y que `XLimitedPlayfield` cumple ambas mitades.

#include <cstdio>

#include <eng/field/scroll_engine.hpp>
#include <eng/field/xlimited.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::u16;
using eng::u32;

// Solo geometría (no dibuja): cumple Target, NO Emitter.
struct TargetOnly {
	u16 tile_width() const { return 16; }
	u16 tile_height() const { return 16; }
	eng::u8 planes() const { return 4; }
	u16 viewport_w() const { return 320; }
	u16 viewport_h() const { return 256; }
	u16 display_height() const { return 288; }
	u16 display_planelines() const { return 1152; }
	u16 bitmap_blocks_per_row() const { return 27; }
	u16 bitmap_blocks_per_col() const { return 18; }
	u16 block_planes_lines() const { return 64; }
	u16 bytes_per_row() const { return 54; }
	u16 bitmap_width() const { return 432; }
	u16 map_width_blocks() const { return 256; }
	u16 map_height_blocks() const { return 128; }
	u16 map_wrap_x() const { return 256; }
	u16 map_wrap_y() const { return 128; }
	bool one_direction() const { return false; }
};

// Solo dibujo (no expone geometría): cumple Emitter, NO Target.
struct EmitterOnly {
	bool add_draw(eng::graphics::FramePlan&, u16, u16, u16, u16) const { return true; }
	void save_word(u32) const {}
	void restore_saveword() const {}
};
} // namespace

// La mitad "layout" del playfield real.
static_assert(eng::field::ScrollTarget<eng::field::XLimitedPlayfield<>>,
              "XLimitedPlayfield debe cumplir ScrollTarget");
// La mitad "dibujo" del playfield real.
static_assert(eng::field::ScrollEmitter<eng::field::XLimitedPlayfield<>>,
              "XLimitedPlayfield debe cumplir ScrollEmitter");

int main() {
	// El contrato está dividido: una mitad no implica la otra.
	check(eng::field::ScrollTarget<TargetOnly>, "TargetOnly cumple ScrollTarget");
	check(!eng::field::ScrollEmitter<TargetOnly>, "TargetOnly NO cumple ScrollEmitter");
	check(eng::field::ScrollEmitter<EmitterOnly>, "EmitterOnly cumple ScrollEmitter");
	check(!eng::field::ScrollTarget<EmitterOnly>, "EmitterOnly NO cumple ScrollTarget");
	// El sink completo exige ambas.
	check(!eng::field::ScrollSink<TargetOnly> && !eng::field::ScrollSink<EmitterOnly>,
	      "una sola mitad no basta para ScrollSink");
	check(eng::field::ScrollSink<eng::field::XLimitedPlayfield<>>,
	      "XLimitedPlayfield cumple ScrollSink completo");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: contrato separado ScrollTarget/ScrollEmitter/ScrollSink validado.\n");
	return 0;
}
