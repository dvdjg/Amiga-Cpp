#pragma once

/// \file actor_sprite.hpp
/// Composicion de sprites hardware de los actores (`SpriteComposeResult`/`Scratch` y las
/// funciones de compose). Definido aparte de `actor_types.hpp`; `actor.hpp` es la
/// cabecera de familia.

#include <eng/scene/actor_store.hpp>

namespace eng::scene {

/// Resumen de la composición de sprites de un frame.
struct SpriteComposeResult {
	eng::u16 sprites = 0;  ///< actores materializados como sprite hardware
	eng::u16 degraded = 0; ///< actores que no caben en hardware (`as_bob`)
	eng::u16 bobs = 0;     ///< actores finalmente dibujados como BOB
	eng::u16 copper = 0;   ///< intenciones de Copper escritas (ancladas a los actores)
	bool ok = false;       ///< false = rechazo controlado (ver `OBJECT_SYSTEM.md`)
};

/// Buffers del llamador para `compose_sprites` (capacidad fija, sin heap).
struct SpriteComposeScratch {
	eng::Span<ActorId> order {};           ///< orden de emisión (tamaño = aforo)
	eng::Span<SpriteIntent> intents {};    ///< una intención por actor
	eng::Span<eng::u16> intent_actor {};   ///< slot del actor de `intents[i]`
	eng::Span<SpriteSlot> slots {};        ///< canales asignados por el allocator
	eng::Span<SpritePlacement> placements {}; ///< sprites publicados
	eng::Span<CopperIntent> copper {};     ///< intenciones de Copper ancladas
};

/// Compone los sprites del frame a partir de los actores:
///
///   1. orden de emisión por superficie y `z` (`plan_actor_order`);
///   2. una intención de sprite por actor, ordenada por `top` (`build_sprite_intents`);
///   3. reparto de canales con multiplexado y tiras (`SpriteAllocator`);
///   4. los que caben se publican como `SpritePlacement` (para `SpriteManager::apply`);
///   5. los degradados a BOB se dibujan en el `FramePlan`, en orden por superficie y `z`;
///   6. las necesidades de Copper ancladas de cada actor se escriben en `copper`.
///
/// Es un paso PURO de composición: no escribe registros. Las necesidades de Copper se
/// emiten para todos los actores (son contenido anclado a su Y); las que dependan de un
/// canal de sprite concreto (rearmes) deben declararse solo en actores que vayan a
/// materializarse como sprite. Devuelve el resumen; `ok == false` marca rechazo.
template <eng::u16 MaxActors>
inline SpriteComposeResult compose_sprites(FramePlan& plan, ActorStore<MaxActors>& store,
					   const ActorEmitContext& ctx, eng::u16 display_top,
					   SpriteComposeScratch& s,
					   eng::Ref<Plan> copper_plan = {}) {
	SpriteComposeResult r {};
	if (store.count() == 0u) {
		r.ok = true; // nada que componer
		return r;
	}
	if (s.order.empty() || s.intents.empty() || s.intent_actor.empty() ||
	    s.slots.empty() || s.placements.empty()) {
		return r;
	}
	const eng::u16 n = plan_actor_order(store, s.order);
	if (n == 0u) {
		return r; // no caben en el buffer del llamador
	}
	if (build_sprite_intents(store, s.order.first(n), ctx, s.intents, s.intent_actor) != n) {
		return r;
	}
	SpriteAllocator{}.assign(s.intents.first(n), s.slots);

	for (eng::u16 i = 0; i < n; ++i) {
		auto a = store.get(store.id_at(s.intent_actor[i]));
		if (!a.valid()) {
			return r;
		}
		const Frame f = actor_current_frame(*a);
		const DirtyRect rect = actor_screen_rect(*a, f, ctx.cam_x, ctx.cam_y);
		if (copper_plan.valid()) {
			// Al Plan, con la prioridad (superficie, z) del actor: activa la fusion de
			// conflictos en la misma linea.
			r.copper = static_cast<eng::u16>(
				r.copper + actor_add_copper(*copper_plan, *a, rect.top, display_top));
		} else {
			const eng::usize room = s.copper.size() > r.copper
							? s.copper.size() - r.copper
							: 0u;
			r.copper = static_cast<eng::u16>(
				r.copper + actor_emit_copper(*a, rect.top, display_top,
							     s.copper.subspan(r.copper, room)));
		}
		if (s.slots[i].as_bob) {
			++r.degraded;
			continue;
		}
		if (r.sprites >= s.placements.size()) {
			return r; // sin sitio para publicar el sprite
		}
		SpritePlacement& p = s.placements[r.sprites];
		p = SpritePlacement {};
		p.channel = s.slots[i].channel;
		p.priority = a->desc.sprite_priority;
		p.hpos = s.intents[i].hpos;
		p.vstart = s.intents[i].top;
		p.height = static_cast<eng::u16>(s.intents[i].bottom - s.intents[i].top);
		p.width_words = s.intents[i].width_words;
		p.attach = s.intents[i].attach;
		p.data = a->desc.visual.pixels.data();
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
