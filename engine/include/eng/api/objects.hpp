#pragma once

/// \file objects.hpp
/// **Fachada de sprites compuestos (F1)** de nivel A: `eng::CompositeScene`.
///
/// El juego da de alta **sprites** (contenido multi-parte, `graphics::CompositeVisual`) con
/// posición, secuencia y espejo; avanza el tiempo con `tick`; emite con `emit` (los BOB van
/// al `FramePlan` del frame) y publica los Sprite HW con una sola `present(sched, ...)` que
/// arma la copperlist. **No expone** intents, canales, placements, `SpriteManager` ni
/// punteros: el engine decide la materialización de cada parte (Sprite HW si su forma lo
/// admite, BOB si no) y degrada sin que la app lo sepa.
///
/// Lo que sí es API de juego (contenido y estado, sin hardware): `CompositeVisual`/
/// `CompositePart`/`CompositeFrame`/`CompositeSequence` (`graphics/composite_visual.hpp`) y
/// `scene::CompositeState` (posición, secuencia, frame, espejo). Las hitboxes del frame se
/// consultan con `hitboxes(id, ...)`.
///
/// Los pares *attached* (parte de 4 planos y 16 px) se cocinan en el pool Chip que dé
/// `set_cooked_pool` (como en `SpriteScene`); sin pool, esa parte se materializa como BOB.
///
/// ```cpp
/// eng::CompositeScene<4> sprites;
/// const auto id = sprites.add(hero, 100, 120, /*sequence*/ 0);
/// sprites.state(id)->facing_left = true;
/// sprites.tick(1);                              // avanza las secuencias (ticks de juego)
/// auto r = sprites.emit(plan, scene.bob_target());
/// sprites.present(scene.scheduler(), 32u);      // arma los Sprite HW del frame
/// ```
///
/// El borrado del rastro es política del juego (como en `BobLayer`): repinta o usa
/// `scene::clear_box` con las cajas de las partes (`part_box(id, part, ...)`).

#include <eng/graphics/bob.hpp>
#include <eng/graphics/composite_visual.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_allocator.hpp>
#include <eng/graphics/sprite_attached.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/scene/composite_actor.hpp>
#include <eng/scene/representation.hpp>

namespace eng {

/// Escena de sprites compuestos de capacidad fija (sin heap). Ver doc del fichero.
template <eng::u16 MaxActors, eng::u16 MaxIntents = static_cast<eng::u16>(MaxActors * 4u)>
class CompositeScene {
public:
	/// Identificador de actor dentro de la escena (`kNoId` si no se pudo dar de alta).
	using Id = eng::u16;
	static constexpr Id kNoId = 0xffffu;

	/// **Política de materialización** de las partes (el juego pide, el engine dispone).
	enum class PartMode : eng::u8 {
		Auto = 0,  ///< Sprite HW si la forma de la parte lo admite; si no, BOB
		Bob,       ///< todas las partes por Blitter
		Sprite,    ///< todas las partes por Sprite HW (las que no quepan degradan a BOB)
	};

	/// Resumen de la emisión de un frame.
	struct EmitResult {
		eng::u16 bobs = 0;      ///< partes dibujadas por Blitter (incluye degradadas)
		eng::u16 sprites = 0;   ///< partes publicadas como Sprite HW
		eng::u16 degraded = 0;  ///< partes que querían Sprite HW y cayeron a BOB
		bool ok = true;         ///< `false` = falta de buffers; rechazo controlado
	};

	/// Da de alta un sprite compuesto. El `visual` debe sobrevivir a la escena (no se copia).
	/// Devuelve el id o `kNoId` si no hay hueco.
	[[nodiscard]] Id add(const eng::graphics::CompositeVisual& visual, eng::s16 x, eng::s16 y,
			     eng::u8 sequence = 0u, bool facing_left = false) noexcept {
		for (eng::u16 i = 0u; i < MaxActors; ++i) {
			if (m_entries[i].active) {
				continue;
			}
			Entry& e = m_entries[i];
			e = Entry {};
			e.visual = &visual;
			e.state.x = x;
			e.state.y = y;
			e.state.sequence = sequence;
			e.state.facing_left = facing_left;
			e.active = true;
			++m_count;
			return i;
		}
		return kNoId;
	}

	/// Baja un actor (el hueco se reutiliza).
	bool remove(Id id) noexcept {
		if (!valid(id)) {
			return false;
		}
		m_entries[id] = Entry {};
		--m_count;
		return true;
	}

	/// Vacía la escena.
	void clear() noexcept {
		for (eng::u16 i = 0u; i < MaxActors; ++i) {
			m_entries[i] = Entry {};
		}
		m_count = 0u;
	}

	[[nodiscard]] eng::u16 count() const noexcept { return m_count; }
	[[nodiscard]] bool valid(Id id) const noexcept {
		return id < MaxActors && m_entries[id].active;
	}

	/// Estado de un actor (mover, girar, cambiar secuencia). `nullptr` si el id no existe.
	[[nodiscard]] eng::scene::CompositeState* state(Id id) noexcept {
		return valid(id) ? &m_entries[id].state : nullptr;
	}
	[[nodiscard]] const eng::scene::CompositeState* state(Id id) const noexcept {
		return valid(id) ? &m_entries[id].state : nullptr;
	}

	/// Contenido del actor (para consultar partes o hitboxes por el juego).
	[[nodiscard]] const eng::graphics::CompositeVisual* visual(Id id) const noexcept {
		return valid(id) ? m_entries[id].visual : nullptr;
	}

	/// Cambia la secuencia de un actor y reinicia su avance (animación dirigida por juego).
	void set_sequence(Id id, eng::u8 sequence, bool restart = true) noexcept {
		if (auto* st = state(id); st != nullptr) {
			eng::scene::composite_set_sequence(*st, sequence, restart);
		}
	}

	/// Avanza las animaciones de TODOS los actores `ticks` ticks de juego.
	void tick(eng::u16 ticks) noexcept {
		for (eng::u16 i = 0u; i < MaxActors; ++i) {
			Entry& e = m_entries[i];
			if (e.active && e.visual != nullptr) {
				(void)eng::scene::composite_advance(*e.visual, e.state, ticks);
			}
		}
	}

	/// Política de materialización **global** (fallback de las partes sin política propia).
	void set_part_mode(PartMode mode) noexcept { m_part_mode = mode; }
	[[nodiscard]] PartMode part_mode() const noexcept { return m_part_mode; }

	/// Política **por parte** (mismo orden que `CompositeVisual::parts`): permite forzar a
	/// BOB las partes cuyo contenido está en formato de BOB y dejar en Sprite HW las que
	/// están en formato de Sprite HW. Vacío = política global (`Auto` por defecto).
	void set_part_modes(eng::Span<const PartMode> modes) noexcept { m_part_modes = modes; }
	[[nodiscard]] PartMode part_mode(eng::usize part_index) const noexcept {
		return (part_index < m_part_modes.size()) ? m_part_modes[part_index] : m_part_mode;
	}

	/// **Pool Chip para los pares *attached***: donde `emit` cocina sus dos estructuras DMA.
	void set_cooked_pool(eng::ChipView<eng::SpriteTag> pool) noexcept { m_cooked = pool; }

	/// Caja de una parte en pantalla (mundo), para consultas del juego (borrado, debug).
	[[nodiscard]] eng::Box part_box(Id id, eng::usize part_index) const noexcept {
		if (!valid(id) || m_entries[id].visual == nullptr) {
			return {};
		}
		return eng::scene::composite_part_box(*m_entries[id].visual, m_entries[id].state,
						      part_index);
	}

	/// **Hitboxes** del frame vigente del actor, en coordenadas de pantalla. Devuelve cuántas.
	[[nodiscard]] eng::u8 hitboxes(Id id, eng::Span<eng::Box> out) const noexcept {
		if (!valid(id) || m_entries[id].visual == nullptr) {
			return 0u;
		}
		return eng::scene::composite_hitboxes(*m_entries[id].visual, m_entries[id].state, out);
	}

	/// **Emite el frame**: las partes BOB van al `plan`; las de Sprite HW quedan preparadas
	/// para `present`. `transparency`/`layout` describen el contenido de las partes BOB.
	EmitResult emit(eng::graphics::FramePlan& plan, const eng::graphics::BobTarget& target,
			eng::graphics::BobLayout layout = eng::graphics::BobLayout::Planar,
			eng::scene::TransparencyMode transparency =
				eng::scene::TransparencyMode::Mask1Bit) noexcept {
		EmitResult r {};
		m_intent_count = 0u;
		m_place_count = 0u;
		m_cooked_used = 0u;

		for (eng::u16 a = 0u; a < MaxActors; ++a) {
			Entry& e = m_entries[a];
			if (!e.active || e.visual == nullptr) {
				continue;
			}
			const eng::graphics::CompositeFrame* fr =
				eng::scene::composite_current_frame(*e.visual, e.state);
			if (fr == nullptr) {
				continue;
			}
			for (eng::usize p = 0u; p < e.visual->parts.size(); ++p) {
				const eng::graphics::CompositePart& part = e.visual->parts[p];
				const eng::Box box = eng::scene::composite_part_box(*e.visual, e.state, p);
				if (box.empty() || part.visual.pixels.empty()) {
					continue;
				}
				const PartMode mode = part_mode(p);
				const bool want_sprite =
					mode != PartMode::Bob && fits_sprite_hw(part.visual);
				const eng::u16 need =
					eng::graphics::visual_is_attached_pair(part.visual) ? 2u : 1u;
				if (want_sprite && m_intent_count + need <= MaxIntents) {
					if (!push_intents(a, p, box)) {
						r.ok = false;
					}
				} else {
					if (want_sprite) {
						++r.degraded; // quería Sprite HW y no había sitio en el buffer
						r.ok = false;
					}
					r.bobs += draw_bob(plan, target, part.visual,
							   eng::scene::composite_part_frame_index(*fr, p),
							   box.x, box.y, layout, transparency) ? 1u : 0u;
				}
			}
		}

		// Reparto de canales y materialización de los Sprite HW.
		(void)eng::graphics::SpriteAllocator {}.assign(
			eng::Span<const eng::graphics::SpriteIntent> {m_intents, m_intent_count},
			eng::Span<eng::graphics::SpriteSlot> {m_slots, m_intent_count});
		for (eng::u16 k = 0u; k < m_intent_count; ++k) {
			const eng::u16 a = m_intent_actor[k];
			Entry& e = m_entries[a];
			const eng::graphics::CompositeFrame* fr =
				eng::scene::composite_current_frame(*e.visual, e.state);
			if (fr == nullptr) {
				continue;
			}
			const eng::usize p = m_intent_part[k];
			const eng::graphics::CompositePart& part = e.visual->parts[p];
			if (m_slots[k].as_bob) {
				if (k > 0u && m_intents[k].attach) {
					continue; // el líder del par ya lo dibujó (o lo dibujará)
				}
				++r.degraded;
				r.bobs += draw_bob(plan, target, part.visual,
						   eng::scene::composite_part_frame_index(*fr, p),
						   m_intents[k].hpos, m_intents[k].top, layout, transparency)
					  ? 1u : 0u;
				continue;
			}
			const bool pair = m_intents[k].attach; // el impar no publica: lo hace el líder
			if (pair) {
				continue;
			}
			const bool is_pair = (k + 1u < m_intent_count) && m_intents[k + 1u].attach;
			const bool published =
				is_pair ? (!m_slots[k + 1u].as_bob &&
					   publish_pair(e, p, static_cast<eng::u16>(m_slots[k].channel),
							k))
					: publish_single(e, p, k);
			if (published) {
				r.sprites = static_cast<eng::u16>(r.sprites + (is_pair ? 2u : 1u));
				if (is_pair) {
					++k; // el impar ya está publicado
				}
				continue;
			}
			// No se pudo publicar (sin pool Chip, sin sitio…): se degrada a BOB UNA vez.
			++r.degraded;
			r.ok = false;
			r.bobs += draw_bob(plan, target, part.visual,
					   eng::scene::composite_part_frame_index(*fr, p),
					   static_cast<eng::s16>(m_intents[k].hpos),
					   static_cast<eng::s16>(m_intents[k].top), layout, transparency)
				  ? 1u : 0u;
		}
		return r;
	}

	/// **Publica los Sprite HW del frame**: arma las colocaciones en la copperlist (un solo
	/// `WAIT` temprano + rearmes verticales). Se llama una vez por frame, tras `emit`.
	template <class Sched>
	void present(Sched& sched, eng::u16 arm_line = 32u) const {
		eng::graphics::SpriteManager::emit_placements_into(
			sched,
			eng::Span<const eng::graphics::HwSpritePlacement> {m_placements,
									  m_place_count},
			arm_line);
	}

private:
	struct Entry {
		const eng::graphics::CompositeVisual* visual = nullptr;
		eng::scene::CompositeState state {};
		bool active = false;
	};

	/// ¿La forma del contenido admite Sprite HW? (16 px de ancho: 1-2 planos directos o
	/// 4 planos como par *attached*; alto ≤ 32.)
	[[nodiscard]] static constexpr bool fits_sprite_hw(
		const eng::graphics::Visual& v) noexcept {
		return v.w > 0u && v.w <= 16u && v.h > 0u && v.h <= 32u &&
		       (v.bitplanes == 1u || v.bitplanes == 2u || v.bitplanes == 4u);
	}

	/// Añade las intents de una parte de Sprite HW (dos si es par *attached*).
	bool push_intents(eng::u16 actor, eng::usize part_index, const eng::Box& box) noexcept {
		const eng::graphics::CompositePart& part = m_entries[actor].visual->parts[part_index];
		if (m_intent_count >= MaxIntents) {
			return false;
		}
		eng::graphics::SpriteIntent it {};
		it.top = static_cast<eng::u16>(box.y);
		it.bottom = static_cast<eng::u16>(box.y + static_cast<eng::s16>(box.h));
		it.hpos = static_cast<eng::u16>(box.x);
		it.width_words = static_cast<eng::u8>((part.visual.w + 15u) / 16u);
		m_intents[m_intent_count] = it;
		m_intent_actor[m_intent_count] = actor;
		m_intent_part[m_intent_count] = static_cast<eng::u8>(part_index);
		++m_intent_count;
		if (eng::graphics::visual_is_attached_pair(part.visual)) {
			if (m_intent_count >= MaxIntents) {
				return false;
			}
			eng::graphics::SpriteIntent odd = it;
			odd.attach = true;
			m_intents[m_intent_count] = odd;
			m_intent_actor[m_intent_count] = actor;
			m_intent_part[m_intent_count] = static_cast<eng::u8>(part_index);
			++m_intent_count;
		}
		return true;
	}

	/// Publica una parte no *attached* como `HwSpritePlacement`.
	bool publish_single(Entry& e, eng::usize part_index, eng::u16 k) noexcept {
		if (m_place_count >= MaxIntents) {
			return false;
		}
		eng::graphics::HwSpritePlacement& pl = m_placements[m_place_count];
		pl = eng::graphics::HwSpritePlacement {};
		pl.channel = m_slots[k].channel;
		pl.hpos = m_intents[k].hpos;
		pl.vstart = m_intents[k].top;
		pl.height = static_cast<eng::u16>(m_intents[k].bottom - m_intents[k].top);
		pl.width_words = m_intents[k].width_words;
		pl.attach = false;
		pl.data = eng::scene::composite_part_data(*e.visual, e.state, part_index);
		if (pl.data.empty() || pl.height == 0u) {
			return false;
		}
		++m_place_count;
		return true;
	}

	/// Cocina y publica las DOS estructuras DMA de un par *attached* (canal par + impar).
	/// `k` es el índice de la intent líder (su geometría es la del par).
	bool publish_pair(Entry& e, eng::usize part_index, eng::u16 even_channel,
			  eng::u16 k) noexcept {
		const eng::graphics::CompositePart& part = e.visual->parts[part_index];
		const eng::graphics::CompositeFrame* fr =
			eng::scene::composite_current_frame(*e.visual, e.state);
		if (fr == nullptr) {
			return false;
		}
		const eng::u16 h = part.visual.h;
		const eng::u8 frame = eng::scene::composite_part_frame_index(*fr, part_index);
		eng::usize off_words = 0u;
		if (part.visual.frame_stride != 0u) {
			const eng::usize byte_off =
				static_cast<eng::usize>(frame) * part.visual.frame_stride;
			if (byte_off / 2u < part.visual.pixels.size()) {
				off_words = byte_off / 2u;
			}
		}
		const eng::usize src_words = static_cast<eng::usize>(h) * 4u;
		if (off_words > part.visual.pixels.size() ||
		    part.visual.pixels.size() - off_words < src_words) {
			return false;
		}
		const eng::usize struct_bytes =
			static_cast<eng::usize>(eng::graphics::attached_pair_structure_words(h)) * 2u;
		if (m_cooked.empty() || m_cooked_used > m_cooked.size() ||
		    m_cooked.size() - m_cooked_used < struct_bytes * 2u ||
		    m_place_count + 2u > MaxIntents) {
			return false;
		}
		const eng::ChipView<eng::SpriteTag> even = m_cooked.subview(m_cooked_used, struct_bytes);
		const eng::ChipView<eng::SpriteTag> odd =
			m_cooked.subview(m_cooked_used + struct_bytes, struct_bytes);
		if (!eng::graphics::cook_attached_pair(
			    part.visual.pixels.subspan(off_words, src_words), h,
			    eng::graphics::kSpriteOffPos, eng::graphics::kSpriteOffCtl, even, odd)) {
			return false;
		}
		m_cooked_used += struct_bytes * 2u;
		const eng::usize data_bytes = static_cast<eng::usize>(h) * 4u + 4u;
		eng::graphics::HwSpritePlacement& pe = m_placements[m_place_count];
		pe = eng::graphics::HwSpritePlacement {};
		pe.channel = static_cast<eng::u8>(even_channel);
		pe.hpos = m_intents[k].hpos;
		pe.vstart = m_intents[k].top;
		pe.height = h;
		pe.width_words = 1u;
		pe.data = even.subview(4u, data_bytes);
		++m_place_count;
		eng::graphics::HwSpritePlacement& po = m_placements[m_place_count];
		po = eng::graphics::HwSpritePlacement {};
		po.channel = static_cast<eng::u8>(even_channel + 1u);
		po.hpos = pe.hpos;
		po.vstart = pe.vstart;
		po.height = h;
		po.width_words = 1u;
		po.attach = true;
		po.data = odd.subview(4u, data_bytes);
		++m_place_count;
		return true;
	}

	static bool draw_bob(eng::graphics::FramePlan& plan, const eng::graphics::BobTarget& target,
			     const eng::graphics::Visual& v, eng::u8 frame, eng::s16 x, eng::s16 y,
			     eng::graphics::BobLayout layout,
			     eng::scene::TransparencyMode transparency) noexcept {
		const eng::graphics::Bob bob = eng::scene::bob_from_visual(v, layout, transparency);
		return eng::graphics::bob_draw(plan, bob, frame, x, y, target);
	}

	Entry m_entries[MaxActors] {};
	eng::u16 m_count = 0u;
	PartMode m_part_mode = PartMode::Auto;
	eng::Span<const PartMode> m_part_modes {};
	eng::ChipView<eng::SpriteTag> m_cooked {};
	eng::usize m_cooked_used = 0u;

	eng::graphics::SpriteIntent m_intents[MaxIntents] {};
	eng::graphics::SpriteSlot m_slots[MaxIntents] {};
	eng::u16 m_intent_actor[MaxIntents] {};
	eng::u8 m_intent_part[MaxIntents] {};
	eng::u16 m_intent_count = 0u;
	eng::graphics::HwSpritePlacement m_placements[MaxIntents] {};
	eng::u16 m_place_count = 0u;
};

} // namespace eng
