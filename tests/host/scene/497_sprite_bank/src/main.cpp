// ============================================================================
// Test HOST-497: SpriteBank, parcheo POS/CTL y set_position del director.
// ============================================================================
//
//   bash tools/run-host-tests.sh tests/host/scene/497_sprite_bank

#include <cstdio>

#include <eng/scene/sprite_bank.hpp>
#include <eng/scene/sprite_director.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

template <class Tag = eng::SpriteTag, eng::usize N>
eng::ChipView<Tag> chip_view(eng::u16 (&a)[N]) {
	return eng::ChipView<Tag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(a),
		static_cast<eng::usize>(N) * sizeof(eng::u16)};
}

eng::u16 g_pixels[64] {};

struct RecordingScheduler {
	eng::u16 words[128] {};
	eng::u16 used = 0u;

	void wait_line(eng::u16) noexcept {}
	void move(eng::copper::Register reg, eng::u16 value) noexcept {
		move(eng::copper::RegisterOffset {static_cast<eng::u16>(reg)}, value);
	}
	void move(eng::copper::RegisterOffset reg, eng::u16 value) noexcept {
		words[used++] = reg.offset;
		words[used++] = value;
	}
	eng::copper::MoveSlot move_at(eng::copper::RegisterOffset reg,
				     eng::u16 value) noexcept {
		const eng::u16 index = used;
		move(reg, value);
		return eng::copper::MoveSlot {index};
	}
};

} // namespace

int main() {
	std::printf("== HOST-497 sprite bank ==\n");

	// --- SpriteBank ---------------------------------------------------------
	eng::scene::SpriteBank<4u> bank {};
	check(bank.bind_data(chip_view(g_pixels)), "bind DATA Chip");
	const auto slot0 = bank.slot_data(0u, 64u);
	check(slot0.size() == 64u, "rebanada del slot 0");
	check(bank.slot_data(4u, 64u).empty(), "slot fuera de capacidad");
	auto bank_cfg = bank.config(0u);
	check(bank_cfg.valid(), "config válida");
	bank_cfg->enabled = true;
	bank_cfg->data = slot0;
	bank_cfg->width_words = 1u;
	bank_cfg->height = 16u;
	check(!bank.config(4u).valid(), "config fuera de rango vacía");

	// --- Parcheo POS/CTL ----------------------------------------------------
	eng::graphics::HwSpritePlacement placement {};
	placement.channel = 3u;
	placement.data = chip_view(g_pixels);
	placement.hpos = 0x8au;
	placement.vstart = 50u;
	placement.height = 16u;
	placement.width_words = 1u;
	eng::graphics::SpritePatchTable patch_table {};
	RecordingScheduler sched {};
	const eng::Span<const eng::graphics::HwSpritePlacement> one {&placement, 1u};
	check(eng::graphics::SpriteManager::emit_placements_patchable(
		      sched, one, 32u, patch_table),
	      "emite placements parcheables");
	const eng::Span<eng::u16> words {sched.words, sched.used};
	check(patch_table.patch(words, one), "parchea POS/CTL");
	const eng::graphics::SpritePosCtl first =
		eng::graphics::encode_sprite_pos_ctl(placement.hpos, placement.vstart,
						     placement.height, placement.attach);
	check(words[patch_table.channel[3].pos.data_word()] == first.pos,
	      "POS escrito en el slot");
	check(words[patch_table.channel[3].ctl.data_word()] == first.ctl,
	      "CTL escrito en el slot");

	placement.vstart = 60u;
	check(patch_table.patch(words, one), "parchea un segundo frame");
	const eng::graphics::SpritePosCtl second =
		eng::graphics::encode_sprite_pos_ctl(placement.hpos, placement.vstart,
						     placement.height, placement.attach);
	check(words[patch_table.channel[3].pos.data_word()] == second.pos,
	      "POS actualizado");

	eng::u16 other_pixels[64] {};
	eng::graphics::HwSpritePlacement changed_data = placement;
	changed_data.data = chip_view(other_pixels);
	const eng::Span<const eng::graphics::HwSpritePlacement> changed {&changed_data, 1u};
	check(!patch_table.patch(words, changed), "DATA distinta exige reemisión");

	// --- SpriteDirector::set_position --------------------------------------
	using Director = eng::scene::SpriteDirector<4u, 1u, 1u, 1u, 8u, 4u>;
	Director director {};
	eng::graphics::Visual visual {};
	visual.pixels = eng::Span<const eng::u16> {g_pixels, 32u};
	visual.w = 16u;
	visual.h = 16u;
	visual.bitplanes = 2u;
	const eng::scene::VisualId visual_id = director.add_visual(visual);
	check(visual_id.valid(), "alta de visual");

	eng::scene::SpriteDesc desc {};
	desc.content = eng::scene::SpriteVisualRef::simple(visual_id);
	desc.hint.policy = eng::scene::SpritePolicy::Sprite;
	desc.x = 10;
	desc.y = 20;
	const eng::scene::SpriteId id = director.add(desc);
	check(id.valid(), "alta de actor");
	check(director.count() == 1u, "count tras alta");

	const eng::s16 x = 100;
	const eng::s16 y = 50;
	check(director.set_position(id, x, y), "set_position válido");
	auto actor = director.actor(id);
	check(actor.valid(), "actor accesible");
	if (actor.valid()) {
		check(actor->desc.x ==
			      static_cast<eng::s16>(x + eng::graphics::kSpriteScreenXOrigin),
		      "set_position suma HSTART");
		check(actor->desc.y == y, "set_position actualiza Y");
	}
	check(!director.set_position({}, x, y), "set_position rechaza id inválido");
	check(director.count() == 1u, "count estable tras mover");
	director.clear();
	check(director.count() == 0u, "clear vacía el director");

	if (g_fail != 0) {
		std::printf("[FAIL] %d comprobaciones\n", g_fail);
		return 1;
	}
	std::printf("OK: SpriteBank, parcheo POS/CTL y set_position validados.\n");
	return 0;
}
