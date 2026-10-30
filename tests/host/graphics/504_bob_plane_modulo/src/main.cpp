// HOST-504: modulo de un BOB de un solo plano sobre destino interleaved 5bpp.
//
// Regresion del fallo real: con 2 palabras por fila y 5 planos, el destino debe
// avanzar 200 bytes por fila (40 * 5), no 40. El modulo correcto es 196.

#include <cstdio>

#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/bitmap_view.hpp>

namespace {
alignas(16) eng::u16 g_sheet[64] {};
alignas(16) eng::u8 g_bitmap[40u * 5u * 256u] {};

void check(bool ok, const char* what) {
	if (!ok) std::printf("[FAIL] %s\n", what);
}

} // namespace

int main() {
	using eng::Address;
	using eng::ChipView;
	using eng::MemoryKind;
	using eng::graphics::Bob;
	using eng::graphics::BobLayout;
	using eng::graphics::BobMaskPack;
	using eng::graphics::BobTarget;
	using eng::graphics::BobDraw;
	using eng::graphics::FramePlan;

	Bob bob {};
	bob.sheet = ChipView<eng::BobTag> {Address<MemoryKind::Chip>::from_storage(
						  reinterpret_cast<const eng::u8*>(g_sheet)),
					  sizeof(g_sheet)};
	bob.width = 12u;
	bob.height = 12u;
	bob.planes = 1u;
	bob.layout = BobLayout::Planar;
	bob.draw = BobDraw::Opaque;
	bob.mask_pack = BobMaskPack::SeparatePlane;

	BobTarget target {};
	target.planes = ChipView<eng::PlaneTag> {
		Address<MemoryKind::Chip>::from_storage(g_bitmap), sizeof(g_bitmap)};
	target.width = 320u;
	target.height = 256u;
	target.row_bytes = 40u;
	target.plane_count = 5u;
	target.layout = BobLayout::Interleaved;

	FramePlan plan {};
	const bool queued = eng::graphics::bob_draw_plane_opaque(plan, bob, 0u, 5, 10, target, 3u);
	check(queued, "el BOB se encola");
	check(plan.ok(), "el plan es valido");
	check(plan.blit_job_count() == 1u, "un solo job");
	if (plan.blit_job_count() == 1u) {
		const eng::graphics::BlitJob& j = plan.blit_job(0u);
		check(j.destination_modulo_bytes == 196, "BLTDMOD=196 (200-4), no 36");
		check(j.destination_plane_stride_bytes == 200u, "stride de plano 200");
		check(j.words_per_row == 2u, "2 palabras con shift");
		check(j.height == 12u, "altura 12 lineas");
		check(j.minterm == 0x00f0u, "D=A (0xF0)");
		const auto fp = eng::graphics::bob_footprint(bob, 5, 10, target);
		check(fp.left == 0 && fp.words == 2u && fp.width_px == 32u, "huella real 32 px");
		check(fp.top == 10u && fp.bottom == 22u, "huella vertical 10..22");
		check(j.bob && j.bob_top == 10u && j.bob_bottom == 22u, "caja beam 10..22");
	}
	std::printf("HOST-504: modulo de BOB de un plano OK\n");
	return 0;
}
