#pragma once

/// \file composite_actor.hpp
/// **Estado y materialización de un sprite compuesto** (`graphics/composite_visual.hpp`).
///
/// Es la capa de juego de F1 (`ROADMAP_JUEGO_SPRITES_BOBS.md` §5): avance determinista de
/// secuencias por ticks de juego, geometría de cada parte en pantalla (con espejo por
/// `facing_left`) y **hitboxes** del frame, más dos materializadores concretos (sin
/// virtual en el camino caliente):
///
///   - `composite_to_sprite_intents`: una `SpriteIntent` por parte apta (dos si el contenido
///     es un par *attached*), para el `SpriteAllocator`/compositor de sprites;
///   - `composite_emit_bobs`: los `BlitJob` de las partes que van a BOB en el `FramePlan`.
///
/// La **representación de cada parte la decide el planner** (una `Representation` por parte,
/// en el mismo orden que `CompositeVisual::parts`); si el span está vacío, el
/// materializador de BOB dibuja todas las partes (representación por defecto). Ni el
/// contenido ni el estado llevan registros, canales ni flags de hardware.
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/composite_visual.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/scene/actor_types.hpp>
#include <eng/scene/representation.hpp>

namespace eng::scene {

using eng::graphics::bob_draw;
using eng::graphics::CompositeFrame;
using eng::graphics::CompositePart;
using eng::graphics::CompositeSequence;
using eng::graphics::CompositeVisual;
using eng::graphics::visual_is_attached_pair;

/// **Estado de runtime** de un compuesto: enteros, independiente de la representación.
struct CompositeState {
	u8 sequence = 0;        ///< secuencia vigente (índice en `CompositeVisual::sequences`)
	u16 frame = 0;          ///< frame vigente dentro de la secuencia
	u16 elapsed = 0;        ///< ticks consumidos del frame vigente
	bool finished = false;  ///< secuencia sin `loop` y sin `next` terminada
	bool facing_left = false; ///< espejo horizontal de las partes y las hitboxes
	s16 x = 0;              ///< posición de pantalla del **ancla**
	s16 y = 0;
};

/// Frame vigente o `nullptr` si el compuesto no tiene secuencias/frames.
[[nodiscard]] inline const CompositeFrame* composite_current_frame(
	const CompositeVisual& vis, const CompositeState& st) noexcept {
	if (vis.sequences.empty() || st.sequence >= vis.sequences.size()) {
		return nullptr;
	}
	const CompositeSequence& seq = vis.sequences[st.sequence];
	if (seq.frames.empty() || st.frame >= seq.frames.size()) {
		return nullptr;
	}
	return &seq.frames[st.frame];
}

/// Frame que usa la parte `part_index` en el frame `fr` (0 si no viene declarado).
[[nodiscard]] inline u8 composite_part_frame_index(const CompositeFrame& fr,
						   eng::usize part_index) noexcept {
	return (part_index < fr.part_frames.size()) ? fr.part_frames[part_index] : 0u;
}

/// Fija la secuencia; `restart` vuelve al frame 0 y limpia `elapsed`/`finished`.
inline void composite_set_sequence(CompositeState& st, u8 sequence, bool restart = true) noexcept {
	st.sequence = sequence;
	if (restart) {
		st.frame = 0;
		st.elapsed = 0;
		st.finished = false;
	}
}

/// Avanza la animación `ticks` ticks de juego. Devuelve `true` si cambió el frame o la
/// secuencia (útil para disparar eventos). Al terminar una secuencia sin `loop`: salta a
/// `next` si está declarada, o queda `finished` en el último frame.
[[nodiscard]] inline bool composite_advance(const CompositeVisual& vis, CompositeState& st,
					    u16 ticks) noexcept {
	if (vis.sequences.empty() || st.finished) {
		return false;
	}
	const u8 seq_before = st.sequence;
	const u16 frame_before = st.frame;
	eng::u32 remaining = ticks;
	while (remaining > 0u) {
		if (st.sequence >= vis.sequences.size()) {
			st.finished = true;
			break;
		}
		const CompositeSequence& seq = vis.sequences[st.sequence];
		if (seq.frames.empty() || st.frame >= seq.frames.size()) {
			st.finished = true;
			break;
		}
		const CompositeFrame& fr = seq.frames[st.frame];
		const u16 dur = fr.ticks != 0u ? fr.ticks : 1u;
		const u16 left = static_cast<u16>(dur - st.elapsed);
		if (remaining < left) {
			st.elapsed = static_cast<u16>(st.elapsed + remaining);
			break;
		}
		remaining -= left;
		st.elapsed = 0;
		if (static_cast<eng::u32>(st.frame) + 1u < seq.frames.size()) {
			++st.frame;
		} else if (seq.loop) {
			st.frame = 0;
		} else if (seq.next != 0xffu && seq.next < vis.sequences.size()) {
			st.sequence = seq.next;
			st.frame = 0;
		} else {
			st.finished = true;
			break;
		}
	}
	return st.sequence != seq_before || st.frame != frame_before;
}

/// Posición **relativa al ancla** de la parte (ya espejada si `facing_left`).
[[nodiscard]] inline eng::Box composite_part_box(const CompositeVisual& vis,
						 const CompositeState& st,
						 eng::usize part_index) noexcept {
	if (part_index >= vis.parts.size()) {
		return {};
	}
	const CompositePart& part = vis.parts[part_index];
	if (part.visual.w == 0u || part.visual.h == 0u) {
		return {};
	}
	const s16 rel_x = st.facing_left
				  ? static_cast<s16>(-part.offset_x - static_cast<s16>(part.visual.w))
				  : part.offset_x;
	return eng::Box { static_cast<s16>(st.x - vis.anchor_x + rel_x),
			  static_cast<s16>(st.y - vis.anchor_y + part.offset_y),
			  part.visual.w, part.visual.h };
}

/// **Hitboxes** del frame vigente, transformadas a pantalla (espejo incluido). Si el frame
/// no trae `hits`, se emite `CompositeVisual::bounds` (una caja) si no está vacío.
/// Devuelve cuántas escribió (0 si no hay frame o no caben en `out`).
[[nodiscard]] inline u8 composite_hitboxes(const CompositeVisual& vis, const CompositeState& st,
					   eng::Span<eng::Box> out) noexcept {
	const CompositeFrame* fr = composite_current_frame(vis, st);
	if (fr == nullptr || out.empty()) {
		return 0u;
	}
	auto emit_one = [&](const eng::Box& rel, u8& n) -> bool {
		if (n >= out.size()) {
			return false;
		}
		const s16 rel_x = st.facing_left
					  ? static_cast<s16>(-rel.x - static_cast<s16>(rel.w))
					  : rel.x;
		out[n] = eng::Box { static_cast<s16>(st.x - vis.anchor_x + rel_x),
				    static_cast<s16>(st.y - vis.anchor_y + rel.y), rel.w, rel.h };
		++n;
		return true;
	};
	u8 n = 0u;
	if (!fr->hits.empty()) {
		for (eng::usize i = 0; i < fr->hits.size(); ++i) {
			if (!emit_one(fr->hits[i].box, n)) {
				break;
			}
		}
		return n;
	}
	if (!vis.bounds.empty()) {
		(void)emit_one(vis.bounds, n);
	}
	return n;
}

/// ¿La representación de la parte `i` es `want`? Con `repr` vacío o corto, vale el defecto.
[[nodiscard]] constexpr bool composite_part_is(eng::Span<const Representation> repr,
					       eng::usize i,
					       Representation want) noexcept {
	return repr.empty() || i >= repr.size() || repr[i] == want;
}

/// **Materializa a intents de Sprite HW** las partes marcadas `Representation::Sprite` (con
/// `part_repr` vacío no se emite ninguna: sin planner, el defecto es BOB). Un contenido de
/// par *attached* (4 planos, w <= 16) produce DOS intents contiguos, igual que
/// `build_sprite_intents`. Devuelve cuántas intents escribió (puede parar por falta de sitio).
[[nodiscard]] inline u16 composite_to_sprite_intents(const CompositeVisual& vis,
						     const CompositeState& st,
						     eng::Span<const Representation> part_repr,
						     eng::Span<SpriteIntent> out) noexcept {
	if (composite_current_frame(vis, st) == nullptr) {
		return 0u;
	}
	u16 n = 0u;
	for (eng::usize i = 0; i < vis.parts.size(); ++i) {
		const CompositePart& part = vis.parts[i];
		if (!composite_part_is(part_repr, i, Representation::Sprite)) {
			continue;
		}
		if (part.visual.pixels.empty() || part.visual.w == 0u || part.visual.h == 0u) {
			continue;
		}
		const eng::Box b = composite_part_box(vis, st, i);
		if (b.empty()) {
			continue;
		}
		const bool pair = visual_is_attached_pair(part.visual);
		const u16 need = pair ? 2u : 1u;
		if (static_cast<eng::u32>(n) + need > out.size()) {
			break; // sin sitio: rechazo controlado del resto
		}
		SpriteIntent it {};
		it.top = static_cast<u16>(b.y);
		it.bottom = static_cast<u16>(b.y + static_cast<s16>(b.h));
		it.hpos = static_cast<u16>(b.x);
		it.width_words = static_cast<u8>((part.visual.w + 15u) / 16u);
		out[n++] = it;
		if (pair) {
			SpriteIntent odd = it;
			odd.attach = true; // canal impar del par: une los 4 bits sobre COLOR16-31
			out[n++] = odd;
		}
	}
	return n;
}

/// **Materializa a BOB** las partes marcadas `Representation::Bob` (con `part_repr` vacío,
/// todas). `layout`/`transparency` describen el formato del contenido de las partes.
/// Devuelve cuántas partes se dibujaron (0 si el plan rechazó todos los jobs). El borrado
/// del rastro es política del llamador (como en `BobLayer`).
[[nodiscard]] inline u16 composite_emit_bobs(const CompositeVisual& vis, const CompositeState& st,
					     eng::Span<const Representation> part_repr,
					     FramePlan& plan, const BobTarget& target,
					     BobLayout layout = BobLayout::Planar,
					     TransparencyMode transparency =
						     TransparencyMode::ColorKey0) noexcept {
	const CompositeFrame* fr = composite_current_frame(vis, st);
	if (fr == nullptr) {
		return 0u;
	}
	u16 drawn = 0u;
	for (eng::usize i = 0; i < vis.parts.size(); ++i) {
		const CompositePart& part = vis.parts[i];
		if (!composite_part_is(part_repr, i, Representation::Bob)) {
			continue;
		}
		if (part.visual.pixels.empty()) {
			continue;
		}
		const eng::Box b = composite_part_box(vis, st, i);
		if (b.empty()) {
			continue;
		}
		const Bob bob = bob_from_visual(part.visual, layout, transparency);
		if (bob_draw(plan, bob, composite_part_frame_index(*fr, i), b.x, b.y, target)) {
			++drawn;
		}
	}
	return drawn;
}

/// **DATA del frame vigente** de una parte, como vista Chip (el DMA de sprites la lee).
/// Mismo puente documentado que `compose_sprites`: el contenido de una parte de sprite se
/// cocina en Chip; el offset de frame sale de `Visual::frame_stride`.
[[nodiscard]] inline eng::ChipView<eng::SpriteTag> composite_part_data(
	const CompositeVisual& vis, const CompositeState& st, eng::usize part_index) noexcept {
	const CompositeFrame* fr = composite_current_frame(vis, st);
	if (fr == nullptr || part_index >= vis.parts.size()) {
		return {};
	}
	const CompositePart& part = vis.parts[part_index];
	const eng::Span<const eng::u16> px = part.visual.pixels;
	if (px.empty()) {
		return {};
	}
	eng::usize off_words = 0u;
	if (part.visual.frame_stride != 0u) {
		const eng::usize byte_off = static_cast<eng::usize>(
			composite_part_frame_index(*fr, part_index)) * part.visual.frame_stride;
		if (byte_off / 2u < px.size()) {
			off_words = byte_off / 2u;
		}
	}
	return eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(px.data() + off_words),
		(px.size() - off_words) * 2u};
}

} // namespace eng::scene
