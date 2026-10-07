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
/// Un actor cuyo **contenido** es de 4 planos y 16 px (par *attached* derivado del arte)
/// consume **dos canales contiguos** y se publica como dos placements de 15 colores; el engine
/// cocina sus dos estructuras DMA en el pool Chip que da `set_cooked_pool`
/// (`ChipView<SpriteTag>`), con la DATA del frame vigente. Sin pool, el par se rechaza de forma
/// controlada (`result.ok == false`).
///
/// ```cpp
/// eng::SpriteScene<64> sprites;                   // hasta 64 actores (NES OAM)
/// sprites.set_budget({8u, 60000u, 0u});           // 8 canales HW, 60k palabras de BOB, 0 capas
/// sprites.set_cooked_pool(cooked_block.mem_view()); // par attached: pool Chip de estructuras
/// sprites.add({.visual = gema, .x = 100, .y = 40, .sprite_priority = 1});
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
	/// \param b  `{sprite_channels, bob_budget_words, layer_slots}`.
	void set_budget(const eng::scene::RepresentationBudget& b) noexcept { m_alloc.reset(b); }

	/// **Pool Chip para los pares *attached***: donde `emit` cocina las estructuras DMA de los
	/// actores con contenido de par *attached* (4 planos y 16 px; dos por par,
	/// `attached_pair_structure_words(h) * 2` bytes cada una). Sin pool, un actor *attached*
	/// provoca rechazo controlado. El bloque lo posee el llamador; aquí solo se referencia
	/// (vista Chip tipada, la lee el DMA).
	/// \param pool  vista Chip de las estructuras (tamaño = pares vivos × 2 × estructura).
	void set_cooked_pool(eng::ChipView<eng::SpriteTag> pool) noexcept { m_cooked = pool; }

	/// **Alta de un actor**. `ActorId{}` (inválido) si el almacén está lleno o la descripción no
	/// tiene contenido (`visual.pixels`/`w`/`h`).
	/// \param desc  descripción del actor (visual + posición + prioridad + …).
	/// \return id del actor, o `ActorId{}` si no se pudo dar de alta.
	[[nodiscard]] eng::scene::ActorId add(const eng::scene::ActorDesc& desc) noexcept {
		return m_store.add(desc, m_alloc);
	}
	/// \param id  id devuelto por `add`.
	/// \return `true` si se quitó el actor.
	[[nodiscard]] bool remove(eng::scene::ActorId id) noexcept { return m_store.remove(id); }
	/// \return nº de actores vivos.
	[[nodiscard]] eng::u16 count() const noexcept { return m_store.count(); }
	/// Vacía el almacén (todos los actores).
	void clear() noexcept { m_store.reset(); }

	[[nodiscard]] eng::scene::ActorStore<MaxActors>& store() noexcept { return m_store; }
	[[nodiscard]] const eng::scene::ActorStore<MaxActors>& store() const noexcept { return m_store; }

	/// **Compone el frame**: añade los BOB degradados (y los actores CPU) al `plan` del frame y deja
	/// las colocaciones de sprite hardware en `placements()`; si `copper` es válido, recibe las
	/// intenciones ancladas de los actores. Devuelve el resumen del frame.
	/// \param plan    plan del frame (recibe los BOB degradados).
	/// \param ctx     contexto de emisión (targets, cámara, buffer, `display_top`).
	/// \param copper  plan de Copper opcional para las intenciones ancladas.
	/// \return resumen: `sprites` (HW), `degraded` (no cupieron), `bobs`, `copper`, `ok`.
	[[nodiscard]] eng::scene::SpriteComposeResult
	emit(eng::graphics::FramePlan& plan, const eng::scene::ActorEmitContext& ctx,
	     eng::Ref<eng::copper::Plan> copper = {}) noexcept {
		eng::scene::SpriteComposeScratch sc {
			eng::Span<eng::scene::ActorId> {m_order, MaxActors},
			eng::Span<eng::scene::SpriteIntent> {m_intents, kMaxIntents},
			eng::Span<eng::u16> {m_intent_actor, kMaxIntents},
			eng::Span<eng::scene::SpriteSlot> {m_slots, kMaxIntents},
			eng::Span<eng::scene::HwSpritePlacement> {m_placements, kMaxIntents},
			eng::Span<eng::scene::CopperIntent> {m_copper, kCopperMax},
			eng::Span<eng::graphics::SpritePaletteEvent> {m_palette, kPaletteMax}};
		sc.cooked = m_cooked;
		m_result = eng::scene::compose_sprites(plan, m_store, ctx, ctx.display_top, sc, copper);
		return m_result;
	}

	/// Colocaciones de sprite hardware del **último** `emit` (válidas las `result().sprites`
	/// primeras). El juego las aplica a su `SpriteManager` (o `Screen`).
	[[nodiscard]] eng::Span<const eng::scene::HwSpritePlacement> placements() const noexcept {
		return eng::Span<const eng::scene::HwSpritePlacement> {m_placements, m_result.sprites};
	}
	/// Intenciones de Copper del **último** `emit` (válidas las `result().copper` primeras):
	/// necesidades ancladas de los actores. Se materializan por el `Plan` de Copper del
	/// conductor.
	[[nodiscard]] eng::Span<const eng::scene::CopperIntent> copper_intents() const noexcept {
		return eng::Span<const eng::scene::CopperIntent> {m_copper, m_result.copper};
	}
	/// **Paleta por franja** de las plantillas de los actores (válidos los `result().palette`
	/// primeros; evento 0-based, con línea absoluta en la escala del sprite). Se materializa
	/// con `SpriteManager::emit_placements_into` (parámetro `extra`).
	[[nodiscard]] eng::Span<const eng::graphics::SpritePaletteEvent> palette_events() const
		noexcept {
		return eng::Span<const eng::graphics::SpritePaletteEvent> {m_palette, m_result.palette};
	}
	[[nodiscard]] const eng::scene::SpriteComposeResult& result() const noexcept { return m_result; }

private:
	static constexpr eng::u16 kCopperMax = static_cast<eng::u16>(MaxActors * 4u); ///< intents/actor
	/// Capacidad por buffer: un actor normal usa 1; un par *attached*, 2; una plantilla de
	/// franjas, una por segmento. Por defecto se dimensiona para **hasta 4 por actor**.
	static constexpr eng::u16 kMaxIntents = static_cast<eng::u16>(MaxActors * 4u);
	/// Capacidad de eventos de paleta (switches de plantilla): hasta 4 por actor.
	static constexpr eng::u16 kPaletteMax = static_cast<eng::u16>(MaxActors * 4u);
	eng::scene::ActorStore<MaxActors> m_store {};
	eng::scene::RepresentationAllocator m_alloc {};
	eng::scene::ActorId m_order[MaxActors] {};
	eng::scene::SpriteIntent m_intents[kMaxIntents] {};
	eng::u16 m_intent_actor[kMaxIntents] {};
	eng::scene::SpriteSlot m_slots[kMaxIntents] {};
	eng::scene::HwSpritePlacement m_placements[kMaxIntents] {};
	eng::scene::CopperIntent m_copper[kCopperMax] {};
	eng::graphics::SpritePaletteEvent m_palette[kPaletteMax] {};
	eng::ChipView<eng::SpriteTag> m_cooked {}; ///< pool de estructuras de pares *attached*
	eng::scene::SpriteComposeResult m_result {};
};

} // namespace eng
