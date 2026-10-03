#pragma once

/// \file dpf_plan.hpp
/// **Estrategia `Dpf`** del planner (`ROADMAP_GAME_API.md` §7): siembra la parte **común** —geometría
/// (viewport/tiles/planos), paleta y **roles** (BG detrás / FG delante)— de una
/// `XlimitedSceneConfigT` a partir de un `ScenePlan` con estrategia `Dpf`.
///
/// El **contenido por field** (mapa, banco de tiles, row-fns) lo aporta el juego: hoy los dos motores
/// usan **formatos de banco distintos** (tiras: contiguo `[tile][line][plane]`; corcóscru: X-Limited
/// interleaved), así que unificarlos es trabajo pendiente. Esta etapa fija el **vocabulario** (los
/// roles y la geometría del DPF) sobre el mecanismo que ya existe (`XlimitedDualConfig`).

#include <eng/field/xlimited_scene.hpp>
#include <eng/scene/plan.hpp>

namespace eng::scene {

/// **Aplica un `ScenePlan` de estrategia `Dpf`** a una config de Xlimited: exige BG + FG a banda
/// completa, toma la geometría/paleta del **BG** y marca el campo delantero (`foreground_is_pf2`).
/// `false` si la estrategia no es `Dpf` o faltan los roles. El mapa/banco por field queda al juego.
template <class MapT, eng::u16 MaxLayers>
[[nodiscard]] bool apply_dpf_plan(eng::playfield::XlimitedSceneConfigT<MapT>& cfg,
				  const ScenePlan<MaxLayers>& plan) noexcept {
	if (plan.strategy() != SceneStrategy::Dpf) {
		return false;
	}
	constexpr eng::u16 kNone = 0xffffu;
	eng::u16 bg = kNone;
	eng::u16 fg = kNone;
	for (eng::u16 i = 0u; i < plan.count(); ++i) {
		const LayerRole role = plan.layer(i).role;
		if (role == LayerRole::Background) {
			bg = i;
		} else if (role == LayerRole::Foreground) {
			fg = i;
		}
	}
	if (bg == kNone || fg == kNone) {
		return false;
	}
	const eng::playfield::ScrollPlan& g = plan.layer(bg).scroll;
	cfg.viewport_w = g.viewport_w;
	cfg.viewport_h = g.viewport_h;
	cfg.tile_width = g.tile_w;
	cfg.tile_height = g.tile_h;
	cfg.planes = g.planes;
	if (g.display_height != 0u) {
		cfg.display_height = g.display_height;
	}
	if (g.tilemap.palette.size() != 0u) {
		cfg.palette = g.tilemap.palette;
	}
	cfg.dpf.enabled = true;
	// El FG puede ser un **campo de scroll** (PF2) o un **lienzo estático** (`fg_canvas`: solo
	// dibuja objetos, como la 203). El rol lo dice el `content` de la capa.
	cfg.dpf.fg_canvas = plan.layer(fg).content == LayerContent::Canvas;
	cfg.dpf.foreground_is_pf2 = true; // el FG (rol `Foreground`) va delante, en PF2
	return true;
}

} // namespace eng::scene
