#pragma once

/// \file plan.hpp
/// **Vocabulario de intención de una escena** (`ROADMAP_GAME_API.md` §7, modelo decidido): una escena
/// es una **lista de capas** y cada capa declara su **rol** (semántica/orden), su **colocación**
/// (banda/campo) y su **scroll** (`playfield::ScrollPlan`). A partir de esa lista, el **planner**
/// elige **una de tres estrategias acotadas** (`Single`/`Dpf`/`Bands`) o rechaza (`Unsupported`) —
/// nunca un «compilador de escena general»: lo que no encaje es un **escape** a `eng::field`.
///
/// Es **puro** (sin motores ni hardware): describe y decide la estrategia; el disparo a cada motor
/// (que reusa mecanismos que ya existen: `XlimitedDualConfig`, `copper::Plan`) llega en las etapas
/// siguientes.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/scroll_plan.hpp>

namespace eng::scene {

/// **Rol** de una capa: su semántica y su orden de composición.
enum class LayerRole : eng::u8 {
	Background = 0, ///< capa de fondo (se compone antes)
	Foreground,     ///< capa delantera (p. ej. el segundo field de un DPF)
	Overlay,        ///< HUD/panel por encima del resto
};

/// **Colocación** de una capa: dónde se ve.
struct LayerPlacement {
	eng::u16 top = 0u;    ///< línea de display donde empieza (si es banda)
	eng::u16 height = 0u; ///< alto de la banda; `0` = **banda completa** (full)
	eng::u8 field = 0u;   ///< playfield preferido (`0` = auto; `1` = PF1, `2` = PF2)

	[[nodiscard]] constexpr bool full() const noexcept { return height == 0u; }
	[[nodiscard]] constexpr bool band() const noexcept { return height != 0u; }
	[[nodiscard]] constexpr bool ok() const noexcept { return height == 0u || (top + height) > top; }
};

/// **Contenido** de una capa: un campo de scroll o un **lienzo estático** (p. ej. el FG de un DPF que
/// solo dibuja objetos, sin tilemap ni scroll propio — `dpf.fg_canvas`).
enum class LayerContent : eng::u8 {
	Scroll = 0, ///< campo de scroll (tilemap)
	Canvas,     ///< lienzo estático (dibuja sobre él; sin mapa)
};

/// **Capa** de una escena: rol + colocación + contenido + scroll (un campo).
struct LayerPlan {
	LayerRole role = LayerRole::Background;
	LayerPlacement placement {};
	LayerContent content = LayerContent::Scroll;
	eng::playfield::ScrollPlan scroll {};
};

/// **Estrategia de composición** elegida por el planner (conjunto acotado).
enum class SceneStrategy : eng::u8 {
	Empty = 0,   ///< sin capas
	Single,      ///< un campo a banda completa
	Dpf,         ///< dos capas a banda completa (un campo cada una)
	Bands,       ///< ≥2 capas en bandas apiladas (split-screen)
	Unsupported, ///< no encaja en las estrategias: escape a `eng::field`
};

/// Elige la **estrategia** para un conjunto de capas (ver el modelo en el roadmap). Reglas:
///   - sin capas → `Empty`;
///   - si hay bandas y capas a banda completa mezcladas → `Unsupported`;
///   - solo bandas: ≥2 → `Bands`, 1 → `Unsupported`;
///   - solo a banda completa: 1 → `Single`; 2 con roles complementarios (uno `Background`, otro
///     `Foreground`) → `Dpf`; el resto → `Unsupported`.
[[nodiscard]] constexpr SceneStrategy choose_strategy(eng::Span<const LayerPlan> layers) noexcept {
	if (layers.empty()) {
		return SceneStrategy::Empty;
	}
	eng::u16 bands = 0u;
	eng::u16 fulls = 0u;
	for (eng::usize i = 0u; i < layers.size(); ++i) {
		if (!layers[i].placement.ok()) {
			return SceneStrategy::Unsupported;
		}
		if (layers[i].placement.band()) {
			++bands;
		} else {
			++fulls;
		}
	}
	if (bands != 0u && fulls != 0u) {
		return SceneStrategy::Unsupported; // mezcla de bandas y bandas completas
	}
	if (bands != 0u) {
		return bands >= 2u ? SceneStrategy::Bands : SceneStrategy::Unsupported;
	}
	if (fulls == 1u) {
		return SceneStrategy::Single;
	}
	if (fulls == 2u) {
		const LayerRole a = layers[0].role;
		const LayerRole b = layers[1].role;
		const bool complementary = (a == LayerRole::Background && b == LayerRole::Foreground) ||
					   (a == LayerRole::Foreground && b == LayerRole::Background);
		return complementary ? SceneStrategy::Dpf : SceneStrategy::Unsupported;
	}
	return SceneStrategy::Unsupported;
}

/// **Plan de escena** de capacidad fija (sin heap): lista de capas + la estrategia que le toca.
template <eng::u16 MaxLayers = 8u>
class ScenePlan {
public:
	/// Añade una capa (rol + colocación + contenido + scroll). `false` si no cabe o la colocación es
	/// inválida.
	[[nodiscard]] bool add(LayerRole role, LayerPlacement placement,
			       const eng::playfield::ScrollPlan& scroll = {},
			       LayerContent content = LayerContent::Scroll) noexcept {
		if (m_count >= MaxLayers || !placement.ok()) {
			return false;
		}
		m_layers[m_count].role = role;
		m_layers[m_count].placement = placement;
		m_layers[m_count].content = content;
		m_layers[m_count].scroll = scroll;
		++m_count;
		return true;
	}

	[[nodiscard]] constexpr eng::u16 count() const noexcept { return m_count; }
	/// Vista de las capas declaradas (para `choose_strategy`/el planner).
	[[nodiscard]] constexpr eng::Span<const LayerPlan> layers() const noexcept {
		return eng::Span<const LayerPlan> {m_layers, m_count};
	}
	[[nodiscard]] constexpr const LayerPlan& layer(eng::u16 i) const noexcept { return m_layers[i]; }
	/// Estrategia de composición para las capas declaradas (ver `choose_strategy`).
	[[nodiscard]] constexpr SceneStrategy strategy() const noexcept {
		return choose_strategy(layers());
	}

private:
	LayerPlan m_layers[MaxLayers] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::scene
