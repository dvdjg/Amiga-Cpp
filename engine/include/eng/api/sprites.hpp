#pragma once

/// \file sprites.hpp
/// **Escena de sprites de nivel A** (`eng::SpriteScene<MaxActors>`): el juego describe **actores**
/// (`ActorDesc`: visual, posición de mundo, prioridad frente a los planos, animación y necesidades
/// de Copper) y llama `emit(...)`; el engine **compone** (HW sprites + `SpriteAllocator` + BOB
/// fallback + Copper) **sin** que el juego declare los buffers de trabajo ni un `FramePlan`.
///
/// Es la fachada del sistema de objetos NES (8 sprites/línea + overflow): los actores que no caben
/// como sprite hardware se **degradan a BOB** (`result.degraded`), dibujados por Blitter en el plan
/// del frame. El juego **no ve** canales, registros ni `BPLxPT`.
///
/// ```cpp
/// eng::SpriteScene<64> sprites;                   // hasta 64 actores (NES OAM)
/// sprites.set_budget({8u, 60000u, 0u});           // 8 canales HW, 60k palabras de BOB, 0 capas
/// sprites.add({.visual = nave, .x = 100, .y = 40, .sprite_priority = 1});
/// // render: compone (los BOB van al `plan`) y deja las colocaciones HW en `placements()`
/// auto r = sprites.emit(plan, ctx);
/// ```
///
/// El `static_cast` de `kCopperMax` es de **frontera** (tamaño `int` de una capacidad `constexpr`
/// pasada a `u16`).

#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite.hpp>
#include <eng/graphics/sprite_allocator.hpp>
#include <eng/scene/actor.hpp>
#include <eng/scene/representation.hpp>

namespace eng {

/// Ver doc del fichero.
template <eng::u16 MaxActors = 64u>
class SpriteScene {
public:
	/// Fija el **presupuesto de representación** (canales de sprite HW, slots de capa, palabras de
	/// BOB): cuántos actores caben como sprite hardware antes de degradar a BOB. Llámalo **antes**
	/// de `add` (el presupuesto se consume al dar de alta cada actor).
	void set_budget(const eng::scene::RepresentationBudget& b) noexcept { m_alloc.reset(b); }

	/// **Alta de un actor**. `ActorId{}` (inválido) si el almacén está lleno o la descripción no
	/// tiene contenido (`visual.pixels`/`w`/`h`).
	[[nodiscard]] eng::scene::ActorId add(const eng::scene::ActorDesc& desc) noexcept {
		return m_store.add(desc, m_alloc);
	}
	[[nodiscard]] bool remove(eng::scene::ActorId id) noexcept { return m_store.remove(id); }
	[[nodiscard]] eng::u16 count() const noexcept { return m_store.count(); }
	void clear() noexcept { m_store.reset(); }

	[[nodiscard]] eng::scene::ActorStore<MaxActors>& store() noexcept { return m_store; }
	[[nodiscard]] const eng::scene::ActorStore<MaxActors>& store() const noexcept { return m_store; }

	/// **Compone el frame**: añade los BOB degradados (y los actores CPU) al `plan` del frame y deja
	/// las colocaciones de sprite hardware en `placements()`; si `copper` es válido, recibe las
	/// intenciones ancladas de los actores. Devuelve el resumen del frame.
	[[nodiscard]] eng::scene::SpriteComposeResult
	emit(eng::graphics::FramePlan& plan, const eng::scene::ActorEmitContext& ctx,
	     eng::Ref<eng::copper::Plan> copper = {}) noexcept {
		eng::scene::SpriteComposeScratch sc {
			eng::Span<eng::scene::ActorId> {m_order, MaxActors},
			eng::Span<eng::scene::SpriteIntent> {m_intents, MaxActors},
			eng::Span<eng::u16> {m_intent_actor, MaxActors},
			eng::Span<eng::scene::SpriteSlot> {m_slots, MaxActors},
			eng::Span<eng::scene::HwSpritePlacement> {m_placements, MaxActors},
			eng::Span<eng::scene::CopperIntent> {m_copper, kCopperMax}};
		m_result = eng::scene::compose_sprites(plan, m_store, ctx, ctx.display_top, sc, copper);
		return m_result;
	}

	/// Colocaciones de sprite hardware del **último** `emit` (válidas las `result().sprites`
	/// primeras). El juego las aplica a su `SpriteManager` (o `Screen`).
	[[nodiscard]] eng::Span<const eng::scene::HwSpritePlacement> placements() const noexcept {
		return eng::Span<const eng::scene::HwSpritePlacement> {m_placements, m_result.sprites};
	}
	[[nodiscard]] const eng::scene::SpriteComposeResult& result() const noexcept { return m_result; }

private:
	static constexpr eng::u16 kCopperMax = static_cast<eng::u16>(MaxActors * 4u); ///< intents/actor
	eng::scene::ActorStore<MaxActors> m_store {};
	eng::scene::RepresentationAllocator m_alloc {};
	eng::scene::ActorId m_order[MaxActors] {};
	eng::scene::SpriteIntent m_intents[MaxActors] {};
	eng::u16 m_intent_actor[MaxActors] {};
	eng::scene::SpriteSlot m_slots[MaxActors] {};
	eng::scene::HwSpritePlacement m_placements[MaxActors] {};
	eng::scene::CopperIntent m_copper[kCopperMax] {};
	eng::scene::SpriteComposeResult m_result {};
};

} // namespace eng
