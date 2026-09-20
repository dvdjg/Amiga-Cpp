#pragma once

/// \file actor_store.hpp
/// Almacen generacional de actores (`ActorStore`) y sus funciones de emision. Definido
/// aparte de `actor_types.hpp`; `actor.hpp` es la cabecera de familia.

#include <eng/scene/actor_types.hpp>

namespace eng::scene {

namespace actor_detail {

inline bool emit_save(FramePlan& plan, const Actor& a, const DirtyRect& r,
		      const ActorEmitContext& ctx, const BobTarget& target) {
	const eng::Span<eng::u16> save = a.desc.save[ctx.buffer];
	// Igual que el borrado: con desplazamiento fino hay que cubrir la palabra extra del
	// barrel shifter, o la restauración deja el borde derecho sin devolver.
	const eng::u16 words = static_cast<eng::u16>((r.width() + 15u) / 16u +
						     ((r.left & 15) != 0 ? 1u : 0u));
	const eng::u16 h = r.height();
	if (save.empty() || words > a.desc.save_words_per_row || h > a.desc.save_height) {
		return false; // sin buffer o buffer insuficiente: rechazo controlado
	}
	const eng::u32 save_row_bytes = static_cast<eng::u32>(a.desc.save_words_per_row) * 2u;
	eng::graphics::BlitJob job {};
	job.destination = {save.data()};
	job.source = {reinterpret_cast<const eng::u16*>(
		target.base + static_cast<eng::u32>(r.top) * target.row_bytes +
		(static_cast<eng::u32>(r.left & ~15) >> 3u))};
	job.words_per_row = words;
	job.height = h;
	job.bitplane_count = a.bob.planes;
	job.source_modulo_bytes = static_cast<eng::s16>(target.row_bytes - static_cast<eng::u32>(words) * 2u);
	job.destination_modulo_bytes = static_cast<eng::s16>(save_row_bytes - static_cast<eng::u32>(words) * 2u);
	job.source_plane_stride_bytes = target.plane_bytes;
	job.destination_plane_stride_bytes = save_row_bytes * a.desc.save_height;
	return plan.add_copy_rect(job);
}

inline bool emit_restore(FramePlan& plan, const Actor& a, const DirtyRect& r,
			 const ActorEmitContext& ctx, const BobTarget& target) {
	const eng::Span<eng::u16> save = a.desc.save[ctx.buffer];
	const eng::u16 words = static_cast<eng::u16>((r.width() + 15u) / 16u +
						     ((r.left & 15) != 0 ? 1u : 0u));
	const eng::u16 h = r.height();
	if (save.empty() || words > a.desc.save_words_per_row || h > a.desc.save_height) {
		return false;
	}
	const eng::u32 save_row_bytes = static_cast<eng::u32>(a.desc.save_words_per_row) * 2u;
	eng::graphics::BlitJob job {};
	job.source = {save.data()};
	job.destination = {reinterpret_cast<eng::u16*>(
		target.base + static_cast<eng::u32>(r.top) * target.row_bytes +
		(static_cast<eng::u32>(r.left & ~15) >> 3u))};
	job.words_per_row = words;
	job.height = h;
	job.bitplane_count = a.bob.planes;
	job.source_modulo_bytes = static_cast<eng::s16>(save_row_bytes - static_cast<eng::u32>(words) * 2u);
	job.destination_modulo_bytes = static_cast<eng::s16>(target.row_bytes - static_cast<eng::u32>(words) * 2u);
	job.source_plane_stride_bytes = save_row_bytes * a.desc.save_height;
	job.destination_plane_stride_bytes = target.plane_bytes;
	return plan.add_restore_rect(job);
}

/// Dibuja el frame `f` del actor como BOB, con el origen del frame dentro de la hoja.
inline bool emit_bob(FramePlan& plan, const Actor& a, const Frame& f, eng::s16 x, eng::s16 y,
		     const BobTarget& target) {
	using eng::graphics::bob_detail::sheet_row_of;
	Bob b = a.bob;
	const eng::u32 row = sheet_row_of(a.bob); // stride de la hoja (antes de cambiar w/h)
	b.sheet += static_cast<eng::u32>(f.y) * row + (static_cast<eng::u32>(f.x) >> 4u) * 2u;
	b.width = f.w;
	b.height = f.h;
	b.frame_count = 1u;
	b.frame_stride = 0u;
	return eng::graphics::bob_draw(plan, b, 0u, x, y, target);
}

} // namespace actor_detail

/// Emite el actor para el frame actual: fondo (borrado o save-under) y objeto. El rect
/// efectivo queda en `out_rect`. Las necesidades de Copper ancladas las escribe el
/// llamador con `actor_emit_copper` (usando el rect devuelto), para que quepan en su
/// propio buffer de intenciones.
inline ActorEmitStatus actor_emit(FramePlan& plan, Actor& a, const ActorEmitContext& ctx,
				  DirtyRect* out_rect = nullptr) {
	if (a.desc.visual.pixels.empty() || a.desc.visual.w == 0u || a.desc.visual.h == 0u) {
		return ActorEmitStatus::Nothing;
	}
	if (ctx.buffer >= kActorBuffers) {
		return ActorEmitStatus::Full;
	}
	if (ctx.targets == nullptr || a.desc.surface >= ctx.target_count) {
		return ActorEmitStatus::Full; // superficie declarada fuera de la composición
	}
	const BobTarget& target = ctx.targets[a.desc.surface];
	const Frame f = actor_current_frame(a);
	const DirtyRect rect = actor_screen_rect(a, f, ctx.cam_x, ctx.cam_y);
	if (!rect.valid()) {
		return ActorEmitStatus::Nothing;
	}
	if (ctx.clip.valid() && !rect_contains(ctx.clip, rect)) {
		return ActorEmitStatus::Clipped; // recorte parcial: pendiente (ver OBJECT_SYSTEM.md)
	}

	const BackgroundPolicy policy = resolve_background(a.desc.background, f.w, f.h);
	const DirtyRect& prev = a.prev[ctx.buffer];

	switch (policy) {
		case BackgroundPolicy::ClearRect: {
			if (prev.valid() &&
			    !eng::graphics::bob_erase_box(plan, a.bob, prev.width(), prev.height(),
							  prev.left, prev.top, target)) {
				return ActorEmitStatus::Full;
			}
			break;
		}
		case BackgroundPolicy::SaveUnder: {
			// El save-under cuenta con planos contiguos en el destino.
			if (target.layout != BobLayout::Planar) {
				return ActorEmitStatus::Full;
			}
			if (prev.valid() && !actor_detail::emit_restore(plan, a, prev, ctx, target)) {
				return ActorEmitStatus::Full;
			}
			if (!actor_detail::emit_save(plan, a, rect, ctx, target)) {
				return ActorEmitStatus::Full;
			}
			break;
		}
		case BackgroundPolicy::None:
		case BackgroundPolicy::DirtyRect:
		case BackgroundPolicy::Auto:
			break;
	}

	if (!actor_detail::emit_bob(plan, a, f, rect.left, rect.top, target)) {
		return ActorEmitStatus::Full;
	}

	a.prev[ctx.buffer] = rect;
	plan.add_dirty_rect(rect);
	if (out_rect != nullptr) {
		*out_rect = rect;
	}
	return plan.ok() ? ActorEmitStatus::Ok : ActorEmitStatus::Full;
}

/// Escribe las necesidades de Copper del actor como líneas ABSOLUTAS, dado su borde
/// superior de pantalla y la primera línea del display. Devuelve cuántas escribió.
inline eng::u8 actor_emit_copper(const Actor& a, eng::s16 screen_y, eng::u16 display_top,
				 CopperIntent* out, eng::u8 capacity) {
	if (out == nullptr) {
		return 0u;
	}
	const eng::s32 base = static_cast<eng::s32>(display_top) + screen_y;
	eng::u8 n = 0;
	for (const CopperIntent& need : a.desc.copper) {
		if (n >= capacity) {
			break;
		}
		CopperIntent abs = need;
		abs.top = static_cast<eng::u16>(base + need.top);
		abs.bottom = static_cast<eng::u16>(base + need.bottom);
		out[n++] = abs;
	}
	return n;
}

/// Escribe en `plan` las necesidades de Copper del actor, ya convertidas a líneas
/// absolutas y CON su prioridad `(superficie, z)`: en conflicto (dos objetos que piden el
/// mismo registro en la misma línea) gana la de mayor `(superficie, z)`, porque el Plan
/// la emite la última. Devuelve cuántas añadió.
inline eng::u8 actor_add_copper(Plan& plan, const Actor& a, eng::s16 screen_y, eng::u16 display_top) {
	const eng::s32 base = static_cast<eng::s32>(display_top) + screen_y;
	eng::u8 n = 0;
	for (const CopperIntent& need : a.desc.copper) {
		CopperIntent abs = need;
		abs.top = static_cast<eng::u16>(base + need.top);
		abs.bottom = static_cast<eng::u16>(base + need.bottom);
		plan.add_prioritized(&abs, 1u, a.desc.surface, a.desc.z);
		++n;
	}
	return n;
}

/// Almacén de actores de capacidad fija con handles generacionales. Sin heap.
/// Se apoya en `eng::util::Pool<Actor, MaxActors>`: slots, free-list y generaciones
/// viven ahí (alta/baja O(1), sin gestión manual de índices).
template <eng::u16 MaxActors>
class ActorStore {
	using PoolType = eng::util::Pool<Actor, MaxActors>;

public:
	static constexpr eng::u16 kMax = MaxActors;

	constexpr void reset() { m_pool.reset(); }

	constexpr eng::u16 count() const { return static_cast<eng::u16>(m_pool.size()); }
	constexpr bool full() const { return m_pool.full(); }

	/// Alta de un actor. Consume presupuesto del allocator. Devuelve un id inválido si
	/// no queda capacidad o si la descripción no tiene contenido.
	ActorId add(const ActorDesc& desc, RepresentationAllocator& alloc) {
		if (m_pool.full() || desc.visual.pixels.empty() || desc.visual.w == 0u ||
		    desc.visual.h == 0u) {
			return {};
		}
		const auto handle = m_pool.add();
		if (!handle.valid()) {
			return {};
		}
		Actor& a = *m_pool.get(handle);
		a.desc = desc;
		a.bob = bob_from_visual(desc.visual, desc.layout, desc.transparency);
		a.bob.sheet_row_bytes = desc.sheet_row_bytes;
		ActorTemplate tmpl {};
		tmpl.width = desc.visual.w;
		tmpl.height = desc.visual.h;
		tmpl.planes = desc.visual.bitplanes;
		tmpl.preferred = desc.preferred;
		tmpl.priority = desc.z;
		tmpl.scrolls = false;
		a.actual = alloc.allocate(tmpl);
		return ActorId {handle.index, handle.generation};
	}

	constexpr bool remove(ActorId id) { return m_pool.remove(to_handle(id)); }

	constexpr Actor* get(ActorId id) { return m_pool.get(to_handle(id)); }

	constexpr const Actor* get(ActorId id) const { return m_pool.get(to_handle(id)); }

	constexpr bool valid_id(ActorId id) const { return m_pool.valid(to_handle(id)); }

	/// Acceso por índice de slot (para iterar el parque); comprobar `used`.
	constexpr bool used(eng::u16 index) const { return m_pool.used(index); }
	constexpr Actor& at(eng::u16 index) { return m_pool.at(index); }
	constexpr const Actor& at(eng::u16 index) const { return m_pool.at(index); }

	/// Identificador (con generación) del slot, o inválido si está libre.
	constexpr ActorId id_at(eng::u16 index) const {
		const auto handle = m_pool.handle_at(index);
		return handle.valid() ? ActorId {handle.index, handle.generation} : ActorId {};
	}

private:
	static constexpr typename PoolType::Handle to_handle(ActorId id) {
		return {id.index, id.generation};
	}

	PoolType m_pool {};
};

/// Clave de orden de emisión: primero la **superficie**, después `z` DENTRO de la
/// superficie (de atrás hacia delante) y, para desempatar, el índice de slot. Los
/// objetos de superficies distintas no compiten por `z`: los superpone el hardware.
constexpr eng::u32 actor_order_key(const ActorDesc& d, eng::u16 index) {
	return (static_cast<eng::u32>(d.surface) << 24u) |
	       (static_cast<eng::u32>(d.z) << 16u) |
	       static_cast<eng::u32>(index);
}

/// Rellena `out` con los actores vivos ordenados por superficie y, dentro de cada una,
/// de atrás hacia delante por `z`. Devuelve cuántos escribió, o 0 si no caben en
/// `capacity` (rechazo controlado). Ordenación por inserción: sin heap y determinista.
template <eng::u16 MaxActors>
inline eng::u16 plan_actor_order(const ActorStore<MaxActors>& store, ActorId* out,
				 eng::u16 capacity) {
	if (out == nullptr || store.count() > capacity) {
		return 0u;
	}
	eng::u16 n = 0;
	for (eng::u16 i = 0; i < MaxActors; ++i) {
		if (store.used(i)) {
			out[n++] = store.id_at(i);
		}
	}
	for (eng::u16 i = 1; i < n; ++i) {
		const ActorId cur = out[i];
		const eng::u32 key = actor_order_key(store.at(cur.index).desc, cur.index);
		eng::u16 j = i;
		while (j > 0u) {
			const ActorId prev = out[j - 1u];
			if (actor_order_key(store.at(prev.index).desc, prev.index) <= key) {
				break;
			}
			out[j] = prev;
			--j;
		}
		out[j] = cur;
	}
	return n;
}

/// Emite los actores en orden por superficie y `z` (usa `plan_actor_order`). Devuelve
/// cuántos se emitieron (los `Nothing`/`Clipped` no cuentan); 0 si el orden no cabe en
/// `order` o si algún actor devuelve `Full` (rechazo controlado).
template <eng::u16 MaxActors>
inline eng::u16 emit_actors_in_order(FramePlan& plan, ActorStore<MaxActors>& store,
				     const ActorEmitContext& ctx, ActorId* order,
				     eng::u16 capacity) {
	const eng::u16 n = plan_actor_order(store, order, capacity);
	if (n == 0u) {
		return 0u;
	}
	eng::u16 emitted = 0;
	for (eng::u16 i = 0; i < n; ++i) {
		Actor* a = store.get(order[i]);
		if (a == nullptr) {
			return 0u;
		}
		const ActorEmitStatus st = actor_emit(plan, *a, ctx);
		if (st == ActorEmitStatus::Full) {
			return 0u;
		}
		if (st == ActorEmitStatus::Ok) {
			++emitted;
		}
	}
	return emitted;
}

/// Construye una `SpriteIntent` por actor (en el orden dado) y las ordena por `top`, que
/// es el contrato de `SpriteAllocator::assign`. `intent_actor[i]` recibe el índice de
/// slot del actor de `intents[i]`, para asociar después los `SpriteSlot` con su actor.
/// Devuelve cuántas escribió, o 0 si no caben en `capacity` (rechazo controlado).
template <eng::u16 MaxActors>
inline eng::u16 build_sprite_intents(const ActorStore<MaxActors>& store, const ActorId* order,
				     eng::u16 count, const ActorEmitContext& ctx,
				     SpriteIntent* intents, eng::u16* intent_actor,
				     eng::u16 capacity) {
	if (intents == nullptr || intent_actor == nullptr || count > capacity) {
		return 0u;
	}
	eng::u16 n = 0;
	for (eng::u16 i = 0; i < count; ++i) {
		const Actor* a = store.get(order[i]);
		if (a == nullptr) {
			return 0u;
		}
		const Frame f = actor_current_frame(*a);
		const DirtyRect r = actor_screen_rect(*a, f, ctx.cam_x, ctx.cam_y);
		intents[n] = actor_to_sprite_intent(*a, f, r);
		intent_actor[n] = order[i].index;
		++n;
	}
	// Orden por `top` (inserción, estable: los empates conservan el orden por
	// superficie/`z` con el que llegaron).
	for (eng::u16 i = 1; i < n; ++i) {
		const SpriteIntent cur = intents[i];
		const eng::u16 cur_actor = intent_actor[i];
		eng::u16 j = i;
		while (j > 0u && intents[j - 1u].top > cur.top) {
			intents[j] = intents[j - 1u];
			intent_actor[j] = intent_actor[j - 1u];
			--j;
		}
		intents[j] = cur;
		intent_actor[j] = cur_actor;
	}
	return n;
}

/// Emite como BOB los actores que el `SpriteAllocator` degradó (`SpriteSlot::as_bob`),
/// respetando el orden por superficie y `z`. `intent_actor[i]` asocia `slots[i]` con su
/// actor. Los actores que sí caben en sprite NO se dibujan aquí (los materializa el
/// camino de sprite). Devuelve cuántos emitió; 0 si algún actor devuelve `Full`.
template <eng::u16 MaxActors>
inline eng::u16 emit_bob_fallbacks(FramePlan& plan, ActorStore<MaxActors>& store,
				   const eng::u16* intent_actor, const SpriteSlot* slots,
				   eng::u16 count, const ActorEmitContext& ctx) {
	if (intent_actor == nullptr || slots == nullptr) {
		return 0u;
	}
	eng::util::StaticVector<eng::u16, MaxActors> fallback;
	for (eng::u16 i = 0; i < count && !fallback.full(); ++i) {
		if (slots[i].as_bob) {
			fallback.push_back(intent_actor[i]);
		}
	}
	const eng::u16 nf = static_cast<eng::u16>(fallback.size());
	// Los degradados se dibujan en orden por superficie y `z`, no en orden de intent
	// (que va por `top`).
	for (eng::u16 i = 1; i < nf; ++i) {
		const eng::u16 cur = fallback[i];
		const eng::u32 key = actor_order_key(store.at(cur).desc, cur);
		eng::u16 j = i;
		while (j > 0u &&
		       actor_order_key(store.at(fallback[j - 1u]).desc, fallback[j - 1u]) > key) {
			fallback[j] = fallback[j - 1u];
			--j;
		}
		fallback[j] = cur;
	}
	eng::u16 emitted = 0;
	for (eng::u16 i = 0; i < nf; ++i) {
		Actor* a = store.get(store.id_at(fallback[i]));
		if (a == nullptr) {
			return 0u;
		}
		const ActorEmitStatus st = actor_emit(plan, *a, ctx);
		if (st == ActorEmitStatus::Full) {
			return 0u;
		}
		if (st == ActorEmitStatus::Ok) {
			++emitted;
		}
	}
	return emitted;
}

} // namespace eng::scene
