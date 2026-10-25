#pragma once

/// \file sprites.hpp
/// **Escena de sprites de nivel A** (`eng::SpriteScene<MaxActors>`): el juego describe **actores**
/// (`ActorDesc`: visual, posición de mundo, prioridad frente a los planos, animación y necesidades
/// de Copper) y llama `emit(...)`; el engine **compone** (HW sprites + `SpriteAllocator` + BOB
/// fallback + Copper) **sin** que el juego declare los buffers de trabajo ni un `FramePlan`.
///
/// Es la fachada del sistema de objetos NES (8 sprites/línea + overflow): los actores que no caben
/// como sprite hardware se **degradan a BOB** (`report.degraded`), dibujados por Blitter en el plan
/// del frame. El juego **no ve** canales, registros ni `BPLxPT`.
///
/// \deprecated **No usar en código nuevo.** Es la implementación interna/legacy de actores;
/// la fachada pública vigente es `eng::scene::SpriteDirector` (demo 225, HOST-485). Se retirará
/// de la fachada pública cuando las demos históricas migren.
///
/// **Modo avanzado.** La fachada por intención con `VisualId`/`CompositeId`, presupuesto real y
/// `SpriteLoadReport` es `eng::scene::SpriteDirector` (`scene/sprite_director.hpp`, incluida en
/// `eng/api/api.hpp`; `SpriteScene` ya no se exporta desde esa fachada). `SpriteScene` sigue siendo la implementación directa de actores, pero
/// **no es la vía nueva**: se conserva como modo avanzado para migrar código existente y se
/// retirará de la fachada pública cuando las demos hayan pasado a `SpriteDirector`.
///
/// Un actor cuyo **contenido** es de 4 planos y 16 px (par *attached* derivado del arte)
/// consume **dos canales contiguos** y se publica como dos placements de 15 colores; el engine
/// cocina sus dos estructuras DMA en el pool Chip que da `set_cooked_pool`
/// (`ChipView<SpriteTag>`), con la DATA del frame vigente. Sin pool, el par se rechaza de forma
/// controlada (`report.ok == false`).
///
/// ```cpp
/// eng::SpriteScene<64> sprites;                   // hasta 64 actores (NES OAM)
/// // `add_direct`/`SpriteDirector`
/// sprites.set_cooked_pool(cooked_block.mem_view()); // par attached: pool Chip de estructuras
/// sprites.add_direct({.visual = gema, .x = 100, .y = 40, .sprite_priority = 1});
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
#include <eng/graphics/sprite_chain.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/scene/actor.hpp>
#include <eng/scene/sprite_visual.hpp>

namespace eng {

/// Ver doc del fichero.
/// \tparam MaxActors capacidad máxima de actores
template <eng::u16 MaxActors = 64u>
class SpriteScene {
public:
	/// **Pool Chip para los pares *attached***: `emit` cocina sus estructuras DMA (dos por par).
	/// La DATA estática se cocina una vez y se reutiliza; el contenido animado se recocina al
	/// cambiar su fuente/altura. El llamador mantiene el pool vivo durante la escena.
	/// \param pool  vista Chip con capacidad para todos los pares que puedan estar vivos.
	void set_cooked_pool(eng::ChipView<eng::SpriteTag> pool) noexcept {
		m_cooked = pool;
		invalidate_cooked_cache();
	}

	/// **Cadenas DMA por canal** (F3, `sprite_chain.hpp`): el juego aporta ≥8
	/// `SpriteDmaChain` con sus storages **Chip** puestas a cero (con runway de ceros tras
	/// lo que escriba cada frame). Tras `emit`, `prepare_chains()` reconstruye lo que decida
	/// el selector y `present()` las usa; sin `set_chains`, `present` emite por el camino de
	/// rearmes (idéntico al de siempre). `budget` fija el umbral de DATA por frame (medido:
	/// `BUS_BUDGET.md`); `stable_channels_mask` marca canales **persistentes** (ranuras
	/// estables: el juego los parchea con `patch_sprite_chain_block`, no se reconstruyen).
	void set_chains(eng::Span<eng::graphics::SpriteDmaChain> chains,
			const eng::graphics::SpriteChainBudget& budget = {},
			eng::u8 stable_channels_mask = 0u) noexcept {
		m_chains = chains;
		m_chain_budget = budget;
		m_stable_channels = stable_channels_mask;
	}

	/// **Prepara las cadenas del último `emit`** (llamar tras `emit` y antes de `present`):
	/// `Build` reconstruye los canales que el selector vote, `Keep` conserva los
	/// persistentes (los parchea el juego). Sin `set_chains` es un no-op.
	[[nodiscard]] eng::graphics::SpriteChainPrep prepare_chains() noexcept {
		if (m_chains.empty()) {
			return {};
		}
		return eng::graphics::prepare_sprite_chains(placements(), m_chains, m_stable_channels,
							    m_chain_budget);
	}

	/// Cadenas configuradas (vacío = `present` emite por rearmes).
	[[nodiscard]] eng::Span<const eng::graphics::SpriteDmaChain> chains() const noexcept {
		return eng::Span<const eng::graphics::SpriteDmaChain> {m_chains.data(),
								       m_chains.size()};
	}

	/// **Alta de un actor**. `ActorId{}` (inválido) si el almacén está lleno o la descripción no
	/// tiene contenido (`visual.pixels`/`w`/`h`).
	/// \param desc  descripción del actor (visual + posición + prioridad + …).
	/// \return id del actor, o `ActorId{}` si no se pudo dar de alta.
	[[nodiscard]] eng::scene::ActorId add(const eng::scene::ActorDesc& desc) noexcept {
		invalidate_cooked_cache();
		return m_store.add(desc);
	}
	/// Alias historico de `add`; la materializacion real la decide la composicion.
	/// \param desc descripcion del actor.
	/// \return id del actor, o `ActorId{}` si no se pudo dar de alta.
	[[nodiscard]] eng::scene::ActorId add_direct(const eng::scene::ActorDesc& desc) noexcept {
		return add(desc);
	}

	/// \param id  id devuelto por `add`.
	/// \return `true` si se quitó el actor.
	[[nodiscard]] bool remove(eng::scene::ActorId id) noexcept {
		const bool removed = m_store.remove(id);
		if (removed) invalidate_cooked_cache();
		return removed;
	}
	/// \return nº de actores vivos.
	[[nodiscard]] eng::u16 count() const noexcept { return m_store.count(); }
	/// Vacía el almacén (todos los actores).
	void clear() noexcept {
		m_store.reset();
		invalidate_cooked_cache();
	}

	[[nodiscard]] eng::scene::ActorStore<MaxActors>& store() noexcept { return m_store; }
	[[nodiscard]] const eng::scene::ActorStore<MaxActors>& store() const noexcept { return m_store; }

	/// **Compone el frame**: añade los BOB degradados (y los actores CPU) al `plan` del frame y deja
	/// las colocaciones de sprite hardware en `placements()`; si `copper` es válido, recibe las
	/// intenciones ancladas de los actores. Devuelve el resumen del frame.
	/// \param plan    plan del frame (recibe los BOB degradados).
	/// \param ctx     contexto de emisión (targets, cámara, buffer, `display_top`).
	/// \param copper  plan de Copper opcional para las intenciones ancladas.
	/// \return reporte con los mismos campos que `SpriteLoadReport` (runtime/preflight).
	[[nodiscard]] eng::scene::SpriteLoadReport
	emit(eng::graphics::FramePlan& plan, const eng::scene::ActorEmitContext& ctx,
	     eng::Ref<eng::copper::Plan> copper = {},
	     eng::Ref<const eng::graphics::SpriteChannelLedger> ledger = {}) noexcept {
		eng::scene::SpriteComposeScratch sc {
			eng::Span<eng::scene::ActorId> {m_order, MaxActors},
			eng::Span<eng::scene::SpriteIntent> {m_intents, kMaxIntents},
			eng::Span<eng::u16> {m_intent_actor, kMaxIntents},
			eng::Span<eng::scene::SpriteSlot> {m_slots, kMaxIntents},
			eng::Span<eng::scene::HwSpritePlacement> {m_placements, kMaxIntents},
			eng::Span<eng::scene::CopperIntent> {m_copper, kCopperMax},
			eng::Span<eng::graphics::SpritePaletteEvent> {m_palette, kPaletteMax}};
		sc.cooked = m_cooked;
		// Memoria del **orden Y incremental** (secuencia de actores del frame anterior).
		sc.prev_actors = eng::Span<eng::u16> {m_prev_actors, MaxActors};
		sc.prev_count = m_prev_count;
		sc.sort_intents = eng::Span<eng::scene::SpriteIntent> {m_sort_intents, kMaxIntents};
		sc.sort_actor = eng::Span<eng::u16> {m_sort_actor, kMaxIntents};
		sc.y_offsets = eng::Span<eng::u16> {m_y_offsets, kRasterLines};
		sc.cooked_pairs = eng::Span<eng::scene::AttachedCookedCache> {m_cooked_pairs, MaxActors};
		m_result = eng::scene::compose_sprites(plan, m_store, ctx, ctx.display_top, sc, copper,
						       ledger);
		m_result.cpu = eng::scene::emit_cpu_actors(plan, m_store, ctx);
		m_prev_count = sc.prev_count; // la secuencia de este frame queda para el siguiente
		m_cooked_used = sc.cooked_used;
		eng::scene::SpriteLoadReport r {};
		r.hw = m_result.sprites;
		r.bob = m_result.bobs;
		r.cpu = m_result.cpu;
		r.degraded = m_result.degraded;
		r.copper_intents = m_result.copper;
		r.palette_events = m_result.palette;
		r.channels_used = m_result.channels;
		r.max_reuse = m_result.max_reuse;
		r.ok = m_result.ok;
		return r;
	}

	/// Actualiza X/Y, alto y **DATA del frame vigente** de una escena estable: actores
	/// simples (uno o dos canales si el ancho pasa de 16 px) y pares *attached*. No
	/// reordena ni reasigna canales; si la geometría del frame cambia de ancho o hay
	/// BOB/CPU/plantillas, devuelve `false` y el llamador hace un `emit` completo.
	///
	/// La animación usa el mismo contrato que `emit`: `Visual::frame_stride` avanza
	/// la DATA por `anim.index`. Un par *attached* se recocina solo cuando cambia la
	/// fuente o el alto (`cook_actor_attached_pair`); si no, reutiliza sus estructuras.
	/// \param ctx contexto de emisión; usa cámara y display_top como `emit`.
	/// \return `true` si todos los actores quedaron actualizados en su sitio;
	///         `false` si hace falta recomponer (BOB, CPU, plantilla o cambio de ancho).
	[[nodiscard]] bool refresh_simple_positions(const eng::scene::ActorEmitContext& ctx) noexcept {
		const eng::u16 count = m_store.count();
		if (count == 0u || m_result.sprites == 0u || !m_result.ok ||
		    m_result.degraded != 0u || m_result.bobs != 0u || m_result.cpu != 0u) {
			return false;
		}
		eng::util::BitSet<MaxActors> seen {};
		for (eng::u16 i = 0u; i < m_result.sprites; ++i) {
			const eng::u16 slot = m_intent_actor[i];
			if (slot >= MaxActors) {
				return false;
			}
			auto a = m_store.get(m_store.id_at(slot));
			if (!a.valid() || a->desc.force_cpu || a->desc.force_bob ||
			    !a->desc.sprite_template.empty()) {
				return false;
			}
			const eng::graphics::Frame f = actor_current_frame(*a);
			const eng::graphics::DirtyRect rect =
				actor_screen_rect(*a, f, ctx.cam_x, ctx.cam_y);
			// Mismo culling que `compose_sprites`: un actor fuera del clip no tiene
			// placement que actualizar (y no debe impedir el refresh de los visibles).
			if (ctx.clip.valid() &&
			    (rect.right <= ctx.clip.left || rect.left >= ctx.clip.right ||
			     rect.bottom <= ctx.clip.top || rect.top >= ctx.clip.bottom)) {
				continue;
			}
			const bool first = !seen.test(slot);
			seen.set(slot);
			if (eng::graphics::visual_is_attached_pair(a->desc.visual)) {
				if (!first) {
					continue; // el líder actualiza los dos placements del par
				}
				if (static_cast<eng::u16>(i + 1u) >= m_result.sprites ||
				    !m_intents[i + 1u].attach || m_intent_actor[i + 1u] != slot) {
					return false; // par mal formado: recomponer
				}
				const eng::usize word_off = actor_frame_word_offset(*a);
				const eng::scene::AttachedPairData pair =
					eng::scene::cook_actor_attached_pair(
					*a, f.h, word_off, m_cooked, m_cooked_used,
					&m_cooked_pairs[slot]);
				if (pair.even.empty() || pair.odd.empty()) {
					return false; // hoja/pool insuficiente: recomponer
				}
				const eng::usize data_bytes =
					static_cast<eng::usize>(f.h) * 4u + 4u;
				eng::graphics::HwSpritePlacement& pe = m_placements[i];
				pe.hpos = rect.left;
				pe.vstart = rect.top;
				pe.height = f.h;
				pe.width_words = 1u;
				pe.attach = false;
				pe.data = pair.even.subview(4u, data_bytes);
				eng::graphics::HwSpritePlacement& po = m_placements[i + 1u];
				po.hpos = rect.left;
				po.vstart = rect.top;
				po.height = f.h;
				po.width_words = 1u;
				po.attach = true;
				po.data = pair.odd.subview(4u, data_bytes);
				continue;
			}
			const eng::u16 width_words =
				static_cast<eng::u16>((f.w + 15u) / 16u);
			eng::graphics::HwSpritePlacement& p = m_placements[i];
			if (width_words != p.width_words || p.attach != (width_words > 1u)) {
				return false; // el ancho del frame cambiaría canales: recomponer
			}
			p.hpos = rect.left;
			p.vstart = rect.top;
			p.height = f.h;
			const eng::usize word_off = actor_frame_word_offset(*a);
			p.data = eng::ChipView<eng::SpriteTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(
					a->desc.visual.pixels.data() + word_off),
				(a->desc.visual.pixels.size() - word_off) * 2u};
		}
		// Todos los actores vivos **visibles** deben haber quedado representados; si
		// alguno no tiene placement (truncado), el refresh no puede dar la escena por
		// buena. Los actores culled no cuentan.
		for (eng::u16 i = 0u; i < MaxActors; ++i) {
			if (!m_store.used(i) || seen.test(i)) {
				continue;
			}
			auto a = m_store.get(m_store.id_at(i));
			if (!a.valid()) {
				return false;
			}
			const eng::graphics::Frame f = actor_current_frame(*a);
			const eng::graphics::DirtyRect rect =
				actor_screen_rect(*a, f, ctx.cam_x, ctx.cam_y);
			if (ctx.clip.valid() &&
			    (rect.right <= ctx.clip.left || rect.left >= ctx.clip.right ||
			     rect.bottom <= ctx.clip.top || rect.top >= ctx.clip.bottom)) {
				continue;
			}
			return false;
		}
		return true;
	}

	/// Colocaciones de sprite hardware del **último** `emit` (válidas las `result().sprites`
	/// primeras). Para el caso normal el juego no las necesita: usa `present`.
	[[nodiscard]] eng::Span<const eng::scene::HwSpritePlacement> placements() const noexcept {
		return eng::Span<const eng::scene::HwSpritePlacement> {m_placements, m_result.sprites};
	}

	/// **Publica los Sprite HW del frame** en la copperlist: primera config de cada canal en
	/// `arm_line` (línea temprana) + rearmes verticales del multiplexado, con la paleta por
	/// franja de las plantillas si la hay. Con `set_chains` configurado y `prepare_chains()`
	/// hecho, los canales con cadena se arman **sin rearmes** (la cadena los sirve). Una
	/// sola llamada por frame tras `emit`; el juego no toca placements, canales ni
	/// `SpriteManager`.
	/// \tparam Sched tipo de emisor de Copper/scheduler
	template <class Sched>
	void present(Sched& sched, eng::u16 arm_line = 32u) const noexcept {
		eng::graphics::SpriteManager::emit_placements_into(sched, placements(), arm_line,
								  palette_events(), chains());
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
	/// Invalida los pares cocinados al cambiar el conjunto de actores o la vista del pool.
	void invalidate_cooked_cache() noexcept {
		m_cooked_used = 0u;
		for (eng::u16 i = 0u; i < MaxActors; ++i) {
			m_cooked_pairs[i] = eng::scene::AttachedCookedCache {};
		}
	}

	static constexpr eng::u16 kCopperMax = static_cast<eng::u16>(MaxActors * 4u); ///< intents/actor
	/// Capacidad por buffer: un actor normal usa 1; un par *attached*, 2; una plantilla de
	/// franjas, una por segmento. Por defecto se dimensiona para **hasta 4 por actor**.
	static constexpr eng::u16 kMaxIntents = static_cast<eng::u16>(MaxActors * 4u);
	/// Capacidad de eventos de paleta (switches de plantilla): hasta 4 por actor.
	static constexpr eng::u16 kPaletteMax = static_cast<eng::u16>(MaxActors * 4u);
	/// Claves de raster PAL/ECS (0..511) para counting-sort estable de placements.
	static constexpr eng::u16 kRasterLines = 512u;
	eng::scene::ActorStore<MaxActors> m_store {};
	eng::scene::ActorId m_order[MaxActors] {};
	eng::u16 m_prev_actors[MaxActors] {}; ///< secuencia Y del frame anterior (incremental)
	eng::u16 m_prev_count = 0u;
	eng::scene::SpriteIntent m_intents[kMaxIntents] {};
	eng::u16 m_intent_actor[kMaxIntents] {};
	eng::scene::SpriteSlot m_slots[kMaxIntents] {};
	eng::scene::HwSpritePlacement m_placements[kMaxIntents] {};
	eng::scene::SpriteIntent m_sort_intents[kMaxIntents] {}; ///< staging de counting-sort para intents
	eng::u16 m_sort_actor[kMaxIntents] {};                   ///< índice de actor paralelo al staging
	eng::u16 m_y_offsets[kRasterLines] {};                   ///< histograma y cursor de cada línea
	eng::scene::AttachedCookedCache m_cooked_pairs[MaxActors] {}; ///< DATA attached cacheada por actor
	eng::usize m_cooked_used = 0u;                            ///< high-water del pool de DATA cocinada
	eng::scene::CopperIntent m_copper[kCopperMax] {};
	eng::graphics::SpritePaletteEvent m_palette[kPaletteMax] {};
	eng::ChipView<eng::SpriteTag> m_cooked {}; ///< pool de estructuras de pares *attached*
	eng::Span<eng::graphics::SpriteDmaChain> m_chains {};     ///< cadenas DMA por canal (F3)
	eng::graphics::SpriteChainBudget m_chain_budget {};       ///< presupuesto del selector
	eng::u8 m_stable_channels = 0u;                           ///< bits de canales persistentes
	eng::scene::SpriteComposeResult m_result {};
};

} // namespace eng
