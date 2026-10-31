// HOST-505: capa de BOBs de un plano.
//
// Regresion de la API: el padding cubre el movimiento pequeno (un job), pero un
// salto mayor que el padding debe emitir clear de la huella previa + draw, en el
// plano correcto y dentro del mismo grupo atomico si se pide.

#include <cstdio>

#include <eng/graphics/bitmap_view.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/scene/plane_bobs.hpp>

namespace {
alignas(16) eng::u16 g_sheet[64] {};
alignas(16) eng::u8 g_bitmap[40u * 5u * 256u] {};

void check(bool ok, const char* what) {
	if (!ok) std::printf("[FAIL] %s\n", what);
}

eng::s32 byte_offset(const eng::graphics::BlitPtr& p, const eng::u8* base) {
	return static_cast<eng::s32>(reinterpret_cast<const eng::u8*>(p.words()) - base);
}

} // namespace

int main() {
	using eng::Address;
	using eng::ChipView;
	using eng::MemoryKind;
	using eng::graphics::Bob;
	using eng::graphics::BobLayout;
	using eng::graphics::BobTarget;
	using eng::graphics::BlitJobKind;
	using eng::graphics::FramePlan;

	Bob bob {};
	bob.sheet = ChipView<eng::BobTag> {
		Address<MemoryKind::Chip>::from_storage(reinterpret_cast<const eng::u8*>(g_sheet)),
		sizeof(g_sheet)};
	bob.width = 12u;
	bob.height = 12u;
	bob.planes = 1u;
	bob.frame_count = 1u;
	bob.layout = BobLayout::Planar;
	bob.draw = eng::graphics::BobDraw::Opaque;

	BobTarget target {};
	target.planes = ChipView<eng::PlaneTag> {
		Address<MemoryKind::Chip>::from_storage(g_bitmap), sizeof(g_bitmap)};
	target.width = 320u;
	target.height = 256u;
	target.row_bytes = 40u;
	target.plane_count = 5u;
	target.layout = BobLayout::Interleaved;

	eng::graphics::Sprite sprite {bob};
	eng::scene::PlaneBobLayer layer {};
	layer.set_sheet(sprite, 2u, 2u);
	layer.resize(1u);
	layer[0u] = {10, 10, 0u, 2u, true};

	FramePlan plan {};
	check(layer.emit(plan, target) == 1u, "primer frame dibuja");
	check(plan.blit_job_count() == 1u, "movimiento inicial: un job");
	if (plan.blit_job_count() == 1u) {
		const auto& j = plan.blit_job(0u);
		check(j.kind == BlitJobKind::OrBlob, "primer job es draw");
		check(byte_offset(j.destination, target.data()) == 2080, "draw en plano 2");
		check(j.minterm == 0x00f0u, "draw opaco D=A");
	}

	plan.clear();
	layer[0u].x = 20;
	check(layer.emit(plan, target) == 1u, "segundo frame dibuja");
	check(plan.blit_job_count() == 2u, "salto > padding: clear + draw");
	if (plan.blit_job_count() == 2u) {
		const auto& clear = plan.blit_job(0u);
		const auto& draw = plan.blit_job(1u);
		check(clear.kind == BlitJobKind::ClearRect, "primero limpia la huella");
		check(byte_offset(clear.destination, target.data()) == 2080,
		      "clear en el plano 2 y posicion previa");
		check(clear.words_per_row == 2u && clear.height == 12u, "caja de clear completa");
		check(clear.destination_modulo_bytes == 196, "clear usa BLTDMOD=196");
		check(byte_offset(draw.destination, target.data()) == 2082, "draw en la nueva x");
	}

	plan.clear();
	layer[0u].x = 30;
	check(layer.emit(plan, target, 0u) == 1u, "tercer frame con grupo");
	check(plan.blit_job_count() == 2u, "clear + draw en grupo");
	if (plan.blit_job_count() == 2u) {
		check(plan.blit_job(0u).bob_group == 0u, "clear marcado como grupo 0");
		check(plan.blit_job(1u).bob_group == 0u, "draw marcado como grupo 0");
	}

	layer.reset_history();
	plan.clear();
	check(layer.emit(plan, target) == 1u, "reset_history no borra ni dibuja de mas");
	check(plan.blit_job_count() == 1u, "tras reset solo draw");

	std::printf("HOST-505: capa de BOBs de un plano OK\n");
	return 0;
}
