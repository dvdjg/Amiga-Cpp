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
	ActorId* order = nullptr;              ///< capacidad = `capacity`
	SpriteIntent* intents = nullptr;       ///< capacidad = `capacity`
	eng::u16* intent_actor = nullptr;      ///< capacidad = `capacity`
	SpriteSlot* slots = nullptr;           ///< capacidad = `capacity`
	SpritePlacement* placements = nullptr; ///< capacidad = `placement_capacity`
	CopperIntent* copper = nullptr;        ///< capacidad = `copper_capacity`
	eng::u16 capacity = 0;                 ///< actores que caben en los buffers
	eng::u16 placement_capacity = 0;
	eng::u16 copper_capacity = 0;
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
					   Plan* copper_plan = nullptr) {
	SpriteComposeResult r {};
	if (store.count() == 0u) {
		r.ok = true; // nada que componer
		return r;
	}
	if (s.order == nullptr || s.intents == nullptr || s.intent_actor == nullptr ||
	    s.slots == nullptr || s.placements == nullptr || s.capacity == 0u ||
	    s.placement_capacity == 0u) {
		return r;
	}
	const eng::u16 n = plan_actor_order(store, s.order, s.capacity);
	if (n == 0u) {
		return r; // no caben en el buffer del llamador
	}
	if (build_sprite_intents(store, s.order, n, ctx, s.intents, s.intent_actor, s.capacity) != n) {
		return r;
	}
	SpriteAllocator{}.assign(s.intents, static_cast<eng::u8>(n), s.slots);

	for (eng::u16 i = 0; i < n; ++i) {
		Actor* a = store.get(store.id_at(s.intent_actor[i]));
		if (a == nullptr) {
			return r;
		}
		const Frame f = actor_current_frame(*a);
		const DirtyRect rect = actor_screen_rect(*a, f, ctx.cam_x, ctx.cam_y);
		if (copper_plan != nullptr) {
			// Al Plan, con la prioridad (superficie, z) del actor: activa la fusion de
			// conflictos en la misma linea.
			r.copper = static_cast<eng::u16>(
				r.copper + actor_add_copper(*copper_plan, *a, rect.top, display_top));
		} else {
			const eng::u16 room = s.copper_capacity > r.copper
						      ? static_cast<eng::u16>(s.copper_capacity - r.copper)
						      : 0u;
			r.copper = static_cast<eng::u16>(
				r.copper + actor_emit_copper(*a, rect.top, display_top,
							     s.copper != nullptr ? s.copper + r.copper : nullptr,
							     static_cast<eng::u8>(room > 255u ? 255u : room)));
		}
		if (s.slots[i].as_bob) {
			++r.degraded;
			continue;
		}
		if (r.sprites >= s.placement_capacity) {
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
