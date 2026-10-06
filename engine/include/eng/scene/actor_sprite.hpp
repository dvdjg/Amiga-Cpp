#pragma once

/// \file actor_sprite.hpp
/// Composicion de sprites hardware de los actores (`SpriteComposeResult`/`Scratch` y las
/// funciones de compose). Definido aparte de `actor_types.hpp`; `actor.hpp` es la
/// cabecera de familia.

#include <eng/graphics/sprite_attached.hpp>
#include <eng/graphics/sprite_channel_window.hpp>
#include <eng/graphics/sprite_limits.hpp>
#include <eng/scene/actor_store.hpp>

namespace eng::scene {

/// Resumen de la composición de sprites de un frame.
struct SpriteComposeResult {
	eng::u16 sprites = 0;  ///< sprites hardware publicados (dos por actor *attached*, una por franja)
	eng::u16 degraded = 0; ///< actores que no caben en hardware (`as_bob`)
	eng::u16 bobs = 0;     ///< actores finalmente dibujados como BOB
	eng::u16 copper = 0;   ///< intenciones de Copper escritas (ancladas a los actores)
	eng::u16 palette = 0;  ///< eventos de paleta por franja (plantillas)
	bool ok = false;       ///< false = rechazo controlado (ver `OBJECT_SYSTEM.md`)
};

/// Buffers del llamador para `compose_sprites` (capacidad fija, sin heap).
struct SpriteComposeScratch {
	eng::Span<ActorId> order {};           ///< orden de emisión (tamaño = aforo)
	eng::Span<SpriteIntent> intents {};    ///< una intención por actor (dos si va en par *attached*, una por franja)
	eng::Span<eng::u16> intent_actor {};   ///< slot del actor de `intents[i]`
	eng::Span<SpriteSlot> slots {};        ///< canales asignados por el allocator
	eng::Span<HwSpritePlacement> placements {}; ///< sprites publicados
	eng::Span<CopperIntent> copper {};     ///< intenciones de Copper ancladas
	eng::Span<SpritePaletteEvent> palette {}; ///< paleta por franja de las plantillas
	/// Pool **Chip** del llamador para las estructuras DMA de los pares *attached* (dos
	/// por par; `attached_pair_structure_words(h) * 2` bytes cada una). Vacío o
	/// insuficiente = la composición rechaza el par (nunca cocina fuera de rango).
	eng::ChipView<eng::SpriteTag> cooked {};
	eng::usize cooked_used = 0; ///< cursor del pool; lo avanza `compose_sprites`
};

/// Compone los sprites del frame a partir de los actores:
///
///   1. orden de emisión por superficie y `z` (`plan_actor_order`);
///   2. las intenciones de sprite de cada actor, ordenadas por `top` (`build_sprite_intents`);
///      un actor con **par *attached*** produce dos contiguas (canal par + impar con `attach`)
///      y uno con **plantilla de franjas** una por segmento, encadenadas al mismo canal;
///   3. reparto de canales con multiplexado, tiras y cadenas (`SpriteAllocator`);
///   4. los que caben se publican como `HwSpritePlacement` (para `SpriteManager::apply`); un
///      par *attached* publica DOS —cada una con su estructura DMA cocinada en `s.cooked`
///      (`cook_attached_pair`, cabecera OFF)—, y una cadena publica **una por franja**
///      (mismo canal; el emisor rearma en cada `vstart`), con la DATA del frame vigente;
///   5. los degradados a BOB se dibujan en el `FramePlan`, en orden por superficie y `z`
///      (cada actor degradado se dibuja UNA vez, desde su `Visual` de 4 planos o normal);
///   6. las necesidades de Copper ancladas de cada actor y la **paleta por franja** de su
///      plantilla se escriben en `copper`.
///
/// `ledger` (opcional) descuenta los canales que ocupan los **fondos por sprites** (ventanas:
/// ver `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`): cada actor solo puede usar los
/// canales libres de su intervalo, y los recupera por encima/por debajo. Ventanas que solapan
/// en líneas con canales distintos conviven (el límite es por canal).
///
/// Es un paso PURO de composición: no escribe registros. Las necesidades de Copper se
/// emiten para todos los actores (son contenido anclado a su Y); las que dependan de un
/// canal de sprite concreto (rearmes) deben declararse solo en actores que vayan a
/// materializarse como sprite. Devuelve el resumen; `ok == false` marca rechazo.
template <eng::u16 MaxActors>
inline SpriteComposeResult compose_sprites(FramePlan& plan, ActorStore<MaxActors>& store,
					   const ActorEmitContext& ctx, eng::u16 display_top,
					   SpriteComposeScratch& s,
					   eng::Ref<Plan> copper_plan = {},
					   eng::Ref<const eng::graphics::SpriteChannelLedger> ledger = {}) {
	SpriteComposeResult r {};
	if (store.count() == 0u) {
		r.ok = true; // nada que componer
		return r;
	}
	if (s.order.empty() || s.intents.empty() || s.intent_actor.empty() ||
	    s.slots.empty() || s.placements.empty()) {
		return r;
	}
	s.cooked_used = 0u; // el pool se reutiliza entero cada frame
	const eng::u16 ordered = plan_actor_order(store, s.order);
	if (ordered == 0u) {
		return r; // no caben en el buffer del llamador
	}
	// `n` cuenta INTENTS, no actores: un par *attached* produce dos (líder + impar).
	const eng::u16 n =
		build_sprite_intents(store, s.order.first(ordered), ctx, s.intents, s.intent_actor);
	if (n == 0u) {
		return r;
	}
	SpriteAllocator{}.assign(s.intents.first(n), s.slots, ledger);

	// Un actor puede aportar varios intents (par *attached*, cadena de franjas): el
	// Copper y la paleta se emiten UNA vez (su primer intent); los placements, uno por
	// intent en el orden (ascendente por `top`) que exonera `emit_placements_into`.
	eng::util::BitSet<MaxActors> done {};
	for (eng::u16 i = 0; i < n; ++i) {
		const eng::u16 slot = s.intent_actor[i];
		const bool first = !done.test(slot);
		done.set(slot);
		auto a = store.get(store.id_at(slot));
		if (!a.valid()) {
			return r;
		}
		if (first) {
			const Frame f = actor_current_frame(*a);
			const DirtyRect rect = actor_screen_rect(*a, f, ctx.cam_x, ctx.cam_y);
			if (copper_plan.valid()) {
				// Al Plan, con la prioridad (superficie, z) del actor: activa la fusion de
				// conflictos en la misma linea.
				r.copper = static_cast<eng::u16>(
					r.copper +
					actor_add_copper(*copper_plan, *a, rect.top, display_top));
			} else {
				const eng::usize room = s.copper.size() > r.copper
								? s.copper.size() - r.copper
								: 0u;
				r.copper = static_cast<eng::u16>(
					r.copper + actor_emit_copper(*a, rect.top, display_top,
								     s.copper.subspan(r.copper, room)));
			}
			// **Paleta por franja de la plantilla** (`HwSpritePaletteSwitch`): evento
			// 0-based (como el switch) con la línea ya absoluta en la escala del sprite
			// (la misma que `HwSpritePlacement::vstart`); el emisor lo intercala con los
			// rearmes (`emit_placements_into`).
			const eng::graphics::HwSpriteTemplateView tpl = a->desc.sprite_template;
			eng::u16 span = 0u;
			for (eng::usize k = 0; k < tpl.segments.size(); ++k) {
				span = static_cast<eng::u16>(span + tpl.segments[k].height + 1u);
			}
			if (span != 0u) {
				span = static_cast<eng::u16>(span - 1u); // sin el gap final
			}
			for (eng::usize k = 0; k < tpl.switches.size(); ++k) {
				const eng::graphics::HwSpritePaletteSwitch& sw = tpl.switches[k];
				if (sw.line >= span || sw.colors == nullptr || sw.count == 0u) {
					continue; // fuera del tramo o switch vacío
				}
				if (r.palette >= s.palette.size()) {
					continue; // sin sitio: se omite (no invalida el frame)
				}
				SpritePaletteEvent& ev = s.palette[r.palette++];
				ev.line = static_cast<eng::u16>(rect.top + sw.line);
				ev.colors = sw.colors;
				ev.first = sw.first;
				ev.count = sw.count;
			}
		}
		if (s.slots[i].as_bob) {
			if (first) {
				++r.degraded;
			}
			continue;
		}
		// **Animación del bitmap del sprite** (SPRITE_CHANNEL_WINDOWS §7): la DATA publicada es
		// la del **frame vigente** de la animación, igual que un BOB animado: `base + índice *
		// frame_stride` (bytes). Sin stride (0) o sin animación, la base. Si la hoja no cubre
		// ese frame, se cae a la base (rechazo controlado, nunca lectura fuera de rango).
		eng::usize word_off = 0u;
		if (a->desc.visual.frame_stride != 0u && a->desc.animation.valid()) {
			eng::usize byte_off = a->anim.index; // u16 -> usize (sin cast)
			byte_off *= a->desc.visual.frame_stride;
			if (byte_off / 2u < a->desc.visual.pixels.size()) {
				word_off = byte_off / 2u;
			}
		}
		const eng::u16 h = static_cast<eng::u16>(s.intents[i].bottom - s.intents[i].top);
		if (h == 0u) {
			return r;
		}
		if (eng::graphics::visual_is_attached_pair(a->desc.visual)) {
			if (!first) {
				continue; // el líder ya publicó los dos canales del par
			}
			// **Par *attached* end-to-end**: el `Visual` trae los 4 planos del frame; el
			// engine cocina las DOS estructuras DMA (par = planos 0-1, impar = 2-3 +
			// ATTACH) en el pool Chip del llamador y publica un placement por canal, en
			// la misma X/Y (AHRM cap. 4, «Attached Sprites»).
			if (i + 1u >= n || !s.intents[i + 1u].attach ||
			    s.intent_actor[i + 1u] != s.intent_actor[i]) {
				return r; // un `attached` sin su intent impar es un par mal formado
			}
			if (s.slots[i + 1u].as_bob) {
				return r; // el allocator reparte el par entero o ninguno
			}
			if (static_cast<eng::u16>(r.sprites + 2u) > s.placements.size()) {
				return r; // sin sitio para publicar los dos canales del par
			}
			const eng::usize src_words = static_cast<eng::usize>(h) * 4u;
			if (word_off > a->desc.visual.pixels.size() ||
			    a->desc.visual.pixels.size() - word_off < src_words) {
				return r; // la hoja no cubre los 4 planos del frame
			}
			const eng::usize struct_bytes =
				static_cast<eng::usize>(
					eng::graphics::attached_pair_structure_words(h)) * 2u;
			if (s.cooked.empty() || s.cooked_used > s.cooked.size() ||
			    s.cooked.size() - s.cooked_used < struct_bytes * 2u) {
				return r; // sin pool/sitio para las dos estructuras del par
			}
			const eng::ChipView<eng::SpriteTag> even =
				s.cooked.subview(s.cooked_used, struct_bytes);
			const eng::ChipView<eng::SpriteTag> odd =
				s.cooked.subview(s.cooked_used + struct_bytes, struct_bytes);
			if (!eng::graphics::cook_attached_pair(
				    a->desc.visual.pixels.subspan(word_off, src_words), h,
				    eng::graphics::kSpriteOffPos, eng::graphics::kSpriteOffCtl, even,
				    odd)) {
				return r;
			}
			s.cooked_used += struct_bytes * 2u;
			// `SPRxPT` apunta a la DATA (tras la cabecera POS/CTL) y el canal termina en
			// el par de ceros: misma vista que consume `SpriteManager::arm_object`.
			const eng::usize data_bytes = static_cast<eng::usize>(h) * 4u + 4u;
			HwSpritePlacement& pe = s.placements[r.sprites];
			pe = HwSpritePlacement {};
			pe.channel = s.slots[i].channel;
			pe.priority = s.intents[i].priority;
			pe.hpos = s.intents[i].hpos;
			pe.vstart = s.intents[i].top;
			pe.height = h;
			pe.width_words = 1u;
			pe.attach = false;
			pe.data = even.subview(4u, data_bytes);
			++r.sprites;
			HwSpritePlacement& po = s.placements[r.sprites];
			po = HwSpritePlacement {};
			po.channel = s.slots[i + 1u].channel;
			po.priority = pe.priority;
			po.hpos = pe.hpos;
			po.vstart = pe.vstart;
			po.height = h;
			po.width_words = 1u;
			po.attach = true; // canal impar del par: une los 4 bits sobre COLOR16-31
			po.data = odd.subview(4u, data_bytes);
			++r.sprites;
			continue;
		}
		const eng::graphics::HwSpriteTemplateView tpl = a->desc.sprite_template;
		if (!tpl.empty()) {
			// **Franja de una plantilla** ("chasing the raster"): una placement por
			// segmento, todas en el canal que el allocator reservó para la cadena; el
			// emisor rearma el canal en el `vstart` de cada franja. `chain_index` indexa
			// el segmento proyectado por `build_sprite_intents`.
			const eng::u16 k = s.intents[i].chain_index;
			if (k >= tpl.segments.size()) {
				return r; // cadena mal formada
			}
			const eng::graphics::HwSpriteSegment& seg = tpl.segments[k];
			const eng::usize seg_words = static_cast<eng::usize>(seg.height) *
						     tpl.width_words * 2u;
			if (tpl.bitmap.size() < static_cast<eng::usize>(seg.data_offset) + seg_words) {
				return r; // el bitmap no cubre la franja
			}
			if (r.sprites >= s.placements.size()) {
				return r;
			}
			HwSpritePlacement& p = s.placements[r.sprites];
			p = HwSpritePlacement {};
			p.channel = s.slots[i].channel;
			p.priority = s.intents[i].priority;
			p.hpos = s.intents[i].hpos;
			p.vstart = s.intents[i].top;
			p.height = static_cast<eng::u16>(s.intents[i].bottom - s.intents[i].top);
			p.width_words = tpl.width_words;
			p.attach = false;
			// Mismo contrato que `emit_template_into`: `SPRxPT` apunta a la DATA de la
			// franja dentro del bitmap cocinado (Chip) de la plantilla.
			p.data = eng::ChipView<eng::SpriteTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(
					tpl.bitmap.data() + seg.data_offset),
				seg_words * 2u};
			++r.sprites;
			continue;
		}
		if (r.sprites >= s.placements.size()) {
			return r; // sin sitio para publicar el sprite
		}
		HwSpritePlacement& p = s.placements[r.sprites];
		p = HwSpritePlacement {};
		p.channel = s.slots[i].channel;
		p.priority = a->desc.sprite_priority;
		p.hpos = s.intents[i].hpos;
		p.vstart = s.intents[i].top;
		p.height = h;
		p.width_words = s.intents[i].width_words;
		p.attach = s.intents[i].attach;
		// Puente documentado `Visual` -> contrato DMA: el camino de sprite exige contenido en
		// **Chip** (los `Visual` del camino de sprite se cocinan en Chip; `sprite-layer.md` §9).
		// El tipo `ChipView<SpriteTag>` del placement fuerza que el emisor no lo olvide.
		p.data = eng::ChipView<eng::SpriteTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(a->desc.visual.pixels.data() +
									  word_off),
			(a->desc.visual.pixels.size() - word_off) * 2u};
		++r.sprites;
	}

	const eng::u16 emitted = emit_bob_fallbacks(plan, store, s.intent_actor, s.slots, n, ctx);
	if (emitted == 0u && r.degraded != 0u) {
		return r; // un degradado fue rechazado
	}
	r.bobs = emitted;
	r.ok = true;
	return r;
}

} // namespace eng::scene
