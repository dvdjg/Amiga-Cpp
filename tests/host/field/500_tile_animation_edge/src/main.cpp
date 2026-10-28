// ============================================================================
// Test HOST-500: borde del anillo de TileAnimator::draw_group.
// ============================================================================
//
// Valida la geometría que usa el parche de animaciones de 207 sin WinUAE:
//
//   1) Un tile en x=336 (última palabra de una fila de 352 px) no se sale
//      de la fila del destino.
//   2) Si ring_y+16 cruza display_height, draw_group parte el tile en dos
//      segmentos y la segunda mitad empieza en la fila 0 del anillo.
//   3) Cada segmento emite un job por plano, con el módulo y stride del
//      destino interleaved (row_bytes * plane_count).
//   4) El segmento que cruza conserva el offset de fuente correcto.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/field/500_tile_animation_edge

#include <cstdio>
#include <cstring>

#include <eng/core/types/types.hpp>
#include <eng/field/tile_animation.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::graphics::BobTarget;
using eng::graphics::FramePlan;
using eng::playfield::TileAnimationCellView;
using eng::playfield::TileAnimationGroupView;
using eng::playfield::TileAnimationLayerView;
using eng::playfield::TileAnimator;

constexpr u16 kRowBytes = 44u;
constexpr u16 kHeight = 288u;
constexpr u8 kPlanes = 6u;
constexpr u16 kFrameStride = 192u;
constexpr u16 kMaskStride = 32u;
constexpr u16 kTile = 16u;
constexpr u16 kFrameStride32 = 768u;
constexpr u16 kMaskStride32 = 128u;

alignas(16) eng::u8 g_target[kRowBytes * kHeight * kPlanes] {};
alignas(16) eng::u8 g_sheet[kFrameStride32] {};
alignas(16) eng::u8 g_masks[kMaskStride32] {};

int g_failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::printf("FALLO: %s\n", what);
		++g_failures;
	}
}

BobTarget make_target(u8 planes = kPlanes) {
	return eng::graphics::make_bob_target(
		eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(g_target),
			sizeof(g_target)},
		kRowBytes, kHeight, planes, eng::graphics::BobLayout::Interleaved);
}

TileAnimator make_animator(const TileAnimationGroupView* groups,
			   const TileAnimationLayerView* layers,
			   const TileAnimationCellView* cells,
			   u16 frame_stride = kFrameStride,
			   u16 mask_stride = kMaskStride,
			   u8 planes = kPlanes,
			   u16 tile = kTile) {
	TileAnimator animator {};
	animator.configure(
		eng::Span<const TileAnimationGroupView> {groups, 1u},
		eng::Span<const TileAnimationLayerView> {layers, 1u},
		eng::Span<const TileAnimationCellView> {cells, 1u},
		eng::as_chip(eng::Pattern {g_sheet, sizeof(g_sheet)}),
		eng::as_chip(eng::MaskBytes {g_masks, sizeof(g_masks)}),
		frame_stride, mask_stride, planes, tile);
	animator.set_tick(0u);
	return animator;
}

u32 destination_offset(u16 x, u16 y, u8 plane, u8 planes = kPlanes) {
	return (static_cast<u32>(y) * planes + plane) * kRowBytes + (x >> 3u);
}

void test_last_word() {
	FramePlan plan {};
	const TileAnimationGroupView group {0u, 1u, 0u, 0u, 1u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}};
	const TileAnimationLayerView layer {nullptr, 0u, 0u, 0u, 0u};
	const TileAnimationCellView cell {0u, 0u, 0u, 0u};
	TileAnimator animator = make_animator(&group, &layer, &cell);
	const BobTarget target = make_target();

	check(animator.draw_group(plan, target, kHeight, 336u, 0u, 0u),
	      "draw_group en x=336: true");
	check(plan.blit_job_count() == 1u, "x=336: un job para todos los planos");
	const eng::graphics::BlitJob& job = plan.blit_job(0u);
	const eng::u8* expected = target.data() + destination_offset(336u, 0u, 0u);
	check(reinterpret_cast<const eng::u8*>(job.destination.cwords()) == expected,
	      "x=336: destino de la última palabra (plano 0)");
	check(job.bitplane_count == kPlanes, "x=336: bitplane_count = planos");
	check(job.words_per_row == 1u && job.height == kTile,
	      "x=336: 1 palabra x 16 filas");
	check(job.source_plane_stride_bytes == 32u,
	      "x=336: stride de plano de fuente");
	check(job.destination_plane_stride_bytes == kRowBytes,
	      "x=336: paso entre planos del anillo");
	check(job.destination_modulo_bytes ==
		      eng::graphics::mod16(static_cast<eng::s32>(kRowBytes) * kPlanes - 2),
	      "x=336: módulo de fila interleaved");
	check(reinterpret_cast<const eng::u8*>(job.mask.cwords()) == g_masks,
	      "x=336: máscara compartida por los planos");
	check(job.minterm == 0x00cau, "x=336: minterm cookie-cut");
}

void test_ring_wrap() {
	FramePlan plan {};
	const TileAnimationGroupView group {0u, 1u, 0u, 0u, 1u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}};
	const TileAnimationLayerView layer {nullptr, 0u, 0u, 0u, 0u};
	const TileAnimationCellView cell {0u, 0u, 0u, 0u};
	TileAnimator animator = make_animator(&group, &layer, &cell);
	const BobTarget target = make_target();

	check(animator.draw_group(plan, target, kHeight, 336u, 280u, 0u),
	      "ring wrap: draw_group true");
	check(plan.blit_job_count() == 2u, "ring wrap: 2 segmentos (uno por tramo)");
	const eng::graphics::BlitJob& first = plan.blit_job(0u);
	const eng::graphics::BlitJob& second = plan.blit_job(1u);
	const eng::u8* first_expected = target.data() + destination_offset(336u, 280u, 0u);
	const eng::u8* second_expected = target.data() + destination_offset(336u, 0u, 0u);
	check(reinterpret_cast<const eng::u8*>(first.destination.cwords()) == first_expected,
	      "ring wrap: primer segmento en fila 280");
	check(reinterpret_cast<const eng::u8*>(second.destination.cwords()) == second_expected,
	      "ring wrap: segundo segmento en fila 0");
	check(first.height == 8u && second.height == 8u, "ring wrap: mitades de 8 filas");
	check(first.bitplane_count == kPlanes && second.bitplane_count == kPlanes,
	      "ring wrap: bitplane_count en ambos tramos");
	check(reinterpret_cast<const eng::u8*>(first.source.cwords()) == g_sheet,
	      "ring wrap: fuente del primer segmento");
	check(reinterpret_cast<const eng::u8*>(second.source.cwords()) == g_sheet + 8u * 2u,
	      "ring wrap: fuente del segundo segmento");
	check(reinterpret_cast<const eng::u8*>(first.mask.cwords()) == g_masks,
	      "ring wrap: máscara compartida");
}

void test_three_planes() {
	FramePlan plan {};
	const TileAnimationGroupView group {0u, 1u, 0u, 0u, 1u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}};
	const TileAnimationLayerView layer {nullptr, 0u, 0u, 0u, 0u};
	const TileAnimationCellView cell {0u, 0u, 0u, 0u};
	TileAnimator animator = make_animator(&group, &layer, &cell, 96u, 32u, 3u, 16u);
	const BobTarget target = make_target(3u);

	check(animator.configured(), "3 planos: configuración válida");
	check(animator.plane_count() == 3u, "3 planos: plane_count");
	check(animator.draw_group(plan, target, kHeight, 336u, 0u, 0u),
	      "3 planos: draw_group");
	check(plan.blit_job_count() == 1u, "3 planos: un job para los 3 planos");
	const eng::graphics::BlitJob& job = plan.blit_job(0u);
	const eng::u8* expected = target.data() + destination_offset(336u, 0u, 0u, 3u);
	check(reinterpret_cast<const eng::u8*>(job.destination.cwords()) == expected,
	      "3 planos: destino");
	check(job.bitplane_count == 3u, "3 planos: bitplane_count");
	check(job.words_per_row == 1u && job.height == kTile,
	      "3 planos: una palabra por fila");
	check(job.source_plane_stride_bytes == 32u, "3 planos: stride de plano");
	check(job.destination_plane_stride_bytes == kRowBytes,
	      "3 planos: paso entre planos del anillo");
	check(job.destination_modulo_bytes ==
		      eng::graphics::mod16(static_cast<eng::s32>(kRowBytes) * 3 - 2),
	      "3 planos: módulo de fila");
}

void test_tile_32() {
	FramePlan plan {};
	const TileAnimationGroupView group {0u, 1u, 0u, 0u, 1u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}};
	const TileAnimationLayerView layer {nullptr, 0u, 0u, 0u, 0u};
	const TileAnimationCellView cell {0u, 0u, 0u, 0u};
	TileAnimator animator = make_animator(&group, &layer, &cell,
					      kFrameStride32, kMaskStride32, 6u, 32u);
	const BobTarget target = make_target(6u);

	check(animator.configured(), "tile 32: configuración válida");
	check(animator.tile() == 32u, "tile 32: tile()");
	check(animator.draw_group(plan, target, kHeight, 304u, 0u, 0u),
	      "tile 32: draw_group");
	check(plan.blit_job_count() == 1u, "tile 32: un job para los 6 planos");
	const eng::graphics::BlitJob& job = plan.blit_job(0u);
	const eng::u8* expected = target.data() + destination_offset(304u, 0u, 0u);
	check(reinterpret_cast<const eng::u8*>(job.destination.cwords()) == expected,
	      "tile 32: destino");
	check(job.bitplane_count == 6u, "tile 32: bitplane_count");
	check(reinterpret_cast<const eng::u8*>(job.source.cwords()) == g_sheet,
	      "tile 32: base de la fuente (plano 0)");
	check(job.words_per_row == 2u && job.height == 32u,
	      "tile 32: dos palabras por fila");
	check(job.source_plane_stride_bytes == 128u,
	      "tile 32: stride de plano de fuente");
	check(job.destination_plane_stride_bytes == kRowBytes,
	      "tile 32: paso entre planos del anillo");
	check(job.destination_modulo_bytes ==
		      eng::graphics::mod16(static_cast<eng::s32>(kRowBytes) * 6 - 4),
	      "tile 32: módulo de fila");
}

} // namespace

int main() {
	std::memset(g_sheet, 0x5a, sizeof(g_sheet));
	std::memset(g_masks, 0xaa, sizeof(g_masks));
	test_last_word();
	test_ring_wrap();
	test_three_planes();
	test_tile_32();
	if (g_failures != 0) {
		std::printf("HOST-500: %d fallos\n", g_failures);
		return 1;
	}
	std::printf("HOST-500: borde de anillo OK\n");
	return 0;
}
