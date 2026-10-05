#pragma once

/// \file display.hpp
/// **Descripción declarativa del display** del juego (`eng::GameDisplay`) y su traducción a los
/// contratos internos de composición y de bus (`scene_resources`/`bus_budget_input`). Es lo que el
/// juego declara (geometría, paleta, intenciones de Copper, presupuesto de bus) sin nombrar
/// registros; `App::start()` lo consume. Se separa de `api/game.hpp` para mantener la fachada
/// compacta.
///
/// Ver `docs/engine/architecture/DISPLAY_COMPOSITION.md` y `docs/engine/architecture/BUS_BUDGET.md`.

#include <eng/core/types/box.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/hw/bus_budget.hpp>
#include <eng/scene/world.hpp>

namespace eng {

/// Descripción declarativa del display que `App::start()` compone y posee.
struct GameDisplay {
	static constexpr u8 kMaxColorDepth = 8u;
	static constexpr u8 kWorldLayerCapacity = scene::kDefaultWorldLayerCapacity;
	static constexpr u16 kPaletteEntries = eng::kPaletteEntries;
	static constexpr u8 kDefaultColorDepth = 4u;
	static constexpr u8 kDefaultBufferCount = 1u;
	static constexpr u16 kDefaultWidth = 320u;
	static constexpr u16 kDefaultHeight = 256u;
	u16 width = kDefaultWidth;
	u16 height = kDefaultHeight;
	u8 color_depth = kDefaultColorDepth;
	u8 buffers = kDefaultBufferCount;
	graphics::PlaneLayout layout = graphics::PlaneLayout::Contiguous;
	Palette32 palette = kBlackPalette;
	/// **Efectos de Copper por línea** (opcional): gradientes, zonas de paleta, etc. El juego los
	/// declara como intenciones de dominio; `App::start()` los compone sin que el juego vea
	/// registros ni la copperlist. Ver `composition::intents`.
	eng::Span<const graphics::CopperIntent> intents {};
	/// **Presupuesto de bus declarado** (opcional, `eng/hw/bus_budget.hpp`): si el juego declara
	/// franjas/Blitter/Copper/CPU, `App::start()` lo comprueba y **falla rápido** si la escena no
	/// cabe en el bus del A500. Con `bands_count == 0` se usa una franja derivada del display
	/// (ancho/alto/planos) para no aceptar a ciegas un modo que ya satura.
	hw::BusBudgetInput bus {};
};

/// Motivo por el que no pudo prepararse el display propio de `App`.
enum class StartError : u8 {
	AlreadyStarted,
	MemoryUnavailable,
	InvalidDisplay,
	OutOfMemory,
	CompositionFailed,
	BusOverBudget, ///< la escena no cabe en el presupuesto de bus declarado (`GameDisplay::bus`)
};

/// Recursos de escena (`SceneResources`) derivados de un `GameDisplay`: geometría planar
/// (`width`×`height`×`color_depth`), número de buffers y layout de planos.
[[nodiscard]] inline graphics::composition::SceneResources scene_resources(const GameDisplay& d) noexcept {
	auto resources = graphics::composition::planar(d.width, d.height, d.color_depth);
	resources.buffers = d.buffers;
	resources.layout = d.layout;
	return resources;
}

/// Entrada del presupuesto de bus: la declarada por el juego, o **una franja derivada del
/// display** si no declaró ninguna, para no aceptar a ciegas un modo que ya satura el bus.
[[nodiscard]] inline hw::BusBudgetInput bus_budget_input(const GameDisplay& d) noexcept {
	hw::BusBudgetInput in = d.bus;
	if (in.bands_count == 0u) {
		in.bands_count = 1u;
		in.bands[0] = hw::BusBand {};
		in.bands[0].height = d.height;
		in.bands[0].width = d.width;
		in.bands[0].bitplanes = d.color_depth;
		in.bands[0].hires = d.width >= 640u;
	}
	return in;
}

} // namespace eng
