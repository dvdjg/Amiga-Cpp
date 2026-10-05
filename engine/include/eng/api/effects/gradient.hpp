#pragma once

#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/effects/rotozoom.hpp>
#include <eng/graphics/sprite_channel_window.hpp>
#include <eng/core/types/memory_kind.hpp>

namespace eng::effects {

/// **Degradado por banda de alto nivel**: envuelve `RasterGradientEffect` para que el juego
/// pida el efecto, no la lista de `CopperIntent`. Cumple el contrato `Effect` (ver
/// `docs/engine/architecture/EFFECT_MODEL.md`).
///
/// ```cpp
/// eng::effects::Gradient sky;
/// sky.attach({.first_line = 0x2c, .band_height = 8, .bands = 24, .first = 0}, keys);
/// // por frame:
/// sky.set_phase(frame);        // anima el degradado
/// sky.frame(scene);            // aporta las intenciones al plan de la escena
/// ```
class Gradient {
public:
	/// Configura rango y colores clave. `false` si no hay claves (nada que emitir).
	[[nodiscard]] bool attach(graphics::effects::RasterGradientRange range,
				  eng::Span<const eng::u16> keys, bool cyclic = true) {
		m_fx.configure(range);
		m_fx.set_keys(keys);
		m_fx.set_cyclic(cyclic);
		return m_fx.key_count() != 0u;
	}

	/// Avanza el estado temporal con el `tick` (índice de frame). El degradado anima por
	/// `set_phase`; aquí solo se satisface el contrato `Effect::update`.
	void update(eng::u16) noexcept {}

	/// Desplaza el muestreo en unidades de clave (animación del degradado).
	void set_phase(eng::u16 phase) noexcept { m_fx.set_phase(phase); }

	/// Aporta las intenciones de Copper a cualquier plan con `add(CopperIntent*, u16)`.
	template <typename Plan>
	void apply_into(Plan& plan) {
		m_fx.apply_into(plan);
	}

	/// Aporta al plan de la escena (azúcar de `apply_into(scene.plan())`).
	void frame(graphics::composition::Scene& scene) { m_fx.apply_into(scene.plan()); }

	/// Aporta el degradado al plan del **setup** y **ata sus palabras de dato** (una sola vez):
	/// materializa las intenciones y registra dónde quedaron. Después, `patch` anima por frame
	/// reescribiendo solo esas palabras, sin re-emitir la copperlist (coste ~0 en el bucle).
	/// Equivale a la etapa que aporta intenciones fijas pero dejándolas **animables**.
	void bind(graphics::composition::Scene& scene) {
		m_fx.apply_into(scene.plan());
		scene.plan().materialize();
		m_fx.bind_slots(scene.plan());
	}

	/// Anima el degradado **parcheando** la lista ya construida (tras `bind`). Por frame.
	void patch(graphics::composition::Scene& scene) const { m_fx.patch_into(scene.plan()); }

	[[nodiscard]] eng::u16 bands() const noexcept { return m_fx.bands(); }

private:
	graphics::effects::RasterGradientEffect m_fx {};
};

} // namespace eng::effects
