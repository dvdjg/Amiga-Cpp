#pragma once

/// \file playfield_layer.hpp
/// **Planificación de una capa PF** (enemigo gigante/fondo) sin convertirla en actor.
///
/// Un PF no es un objeto con identidad de actor: es una **región de display** que se
/// materializa en uno de los dos playfields de un DPF. El modelo de capas (`Layer`,
/// `LayerPlayfield`, `ScenePlan`) expresa la intención; este header decide si la capa
/// cabe como PF y en cuál, a partir de las capacidades del display.
///
/// Reglas:
///   - `LayerPlayfield::Pf2` exige DPF activo y PF2 libre.
///   - `LayerPlayfield::Pf1` exige PF1 libre.
///   - `Any` prefiere PF2 libre (detrás del FG del juego), luego PF1.
///   - una capa que no ocupa la altura completa no se materializa como PF (un PF es una
///     banda de display completa; para bandas se usa `WorldRegion`/`Bands`).
///   - si no hay DPF/PF libre, se rechaza; el llamador decide degradar (p. ej. a bitmap
///     estático) o rediseñar la escena. No se crea un actor como sustituto.
///
/// Es lógica pura, sin heap ni hardware: se puede usar en preflight/`constexpr` y en el
/// planner runtime.

#include <eng/graphics/cpu_blit.hpp>
#include <eng/scene/actor_types.hpp>
#include <eng/scene/world.hpp>

namespace eng::scene {

/// Capacidades de display para colocar una capa PF.
struct PlayfieldLayerCaps {
	bool dpf_enabled = false; ///< el display tiene DPF (dos playfields)
	bool pf1_free = true;     ///< PF1 no está reservado por otra capa
	bool pf2_free = true;     ///< PF2 no está reservado por otra capa
	eng::u8 max_planes = 3u;  ///< planos por playfield del DPF
};

/// Petición de una capa PF (sin identidad de actor).
struct PlayfieldLayerRequest {
	LayerPlayfield prefer = LayerPlayfield::Any;
	eng::u8 planes = 1u;
	bool full_height = true;
};

/// Resultado de planificar la capa PF.
struct PlayfieldLayerPlan {
	bool accepted = false;
	LayerPlayfield field = LayerPlayfield::Pf1;
};

/// Decide el playfield de una capa PF. Función pura y determinista.
[[nodiscard]] constexpr PlayfieldLayerPlan plan_playfield_layer(
	const PlayfieldLayerRequest& req, const PlayfieldLayerCaps& caps) noexcept {
	PlayfieldLayerPlan out {};
	if (!req.full_height || req.planes == 0u || req.planes > caps.max_planes ||
	    !caps.dpf_enabled) {
		return out;
	}
	if (req.prefer == LayerPlayfield::Pf2 && caps.pf2_free) {
		return {true, LayerPlayfield::Pf2};
	}
	if (req.prefer == LayerPlayfield::Pf1 && caps.pf1_free) {
		return {true, LayerPlayfield::Pf1};
	}
	if (req.prefer == LayerPlayfield::Any) {
		if (caps.pf2_free) {
			return {true, LayerPlayfield::Pf2};
		}
		if (caps.pf1_free) {
			return {true, LayerPlayfield::Pf1};
		}
	}
	return out;
}

/// Contenido de una capa PF (fondo/enemigo gigante) sin identidad de actor. Es una
/// vista de contenido + posición en el playfield; la capa decide su `field` con
/// `plan_playfield_layer`, pero el dibujo CPU no crea un `Actor` ni consume canales.
struct PlayfieldLayer {
	eng::graphics::Visual visual {}; ///< contenido planar (misma vista que un actor)
	eng::s16 x = 0;                 ///< esquina izquierda en píxeles de pantalla
	eng::s16 y = 0;                 ///< esquina superior en píxeles de pantalla
	LayerPlayfield field = LayerPlayfield::Pf2; ///< PF planificado (informativo)
	TransparencyMode transparency = TransparencyMode::Opaque;
	eng::graphics::BobLayout layout = eng::graphics::BobLayout::Planar;
	bool visible = true;

	/// Copia CPU de la capa en `target` (p. ej. el bitmap de PF2). No encola blits.
	[[nodiscard]] bool draw_cpu(const eng::graphics::BobTarget& target) const noexcept {
		if (!visible || visual.pixels.empty()) {
			return false;
		}
		const eng::graphics::Bob bob =
			bob_from_visual(visual, layout, transparency);
		return eng::graphics::cpu_draw_bob(target, bob, 0u, x, y).ok;
	}
};

} // namespace eng::scene
