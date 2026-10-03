#pragma once

/// \file xlimited_scroll_layer.hpp
/// **Adapta una `XlimitedScene` a `ScrollLayer<Backend>`** (`eng::playfield::XlimitedScrollLayer`)
/// para que el `App` la arranque y conduce por el mismo contrato que el camino de tiras, **sin
/// `void*` ni punteros a función**. `Scene` es el tipo concreto de la escena (lleva su `Map` y su
/// `Profile`); `Backend`, el del `App`.
///
/// El juego declara el adaptador (típicamente como miembro, junto a la escena) y lo registra:
///
/// ```cpp
/// playfield::XlimitedScene<kScroll, MapView, Profile> scene;
/// playfield::XlimitedScrollLayer<decltype(scene), eng::amiga::AmigaBackend> layer {scene};
/// // en init(App&):
/// scene.set_config(config);
/// scene.track_camera(&cam_x, &cam_y);
/// app.add_scroll_layer(layer);
/// ```

#include <eng/field/scroll_layer.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng::playfield {

/// Adaptador `XlimitedScene` → `ScrollLayer<Backend>` (ver doc del fichero).
template <class Scene, class Backend>
class XlimitedScrollLayer final : public ScrollLayer<Backend> {
public:
	explicit XlimitedScrollLayer(Scene& scene) noexcept : m_scene(scene) {}

	/// Arranca la escena: `begin` (reserva en Chip) → cámara inicial → `fill` → `compose` → `takeover`.
	[[nodiscard]] bool begin(MemoryManager& memory, Backend& backend) noexcept override {
		if (!m_scene.begin(memory, m_scene.config())) return false;
		m_scene.bg().set_camera(0, 0); // posición inicial del scroll antes de pintar el anillo
		if (!m_scene.fill(backend, m_plan)) return false;
		if (!m_scene.compose()) return false;
		m_scene.takeover(backend);
		return true;
	}

	/// Conduce un frame: la cámara registrada en la escena → `update`+blit+`compose`+`install`.
	void frame(Backend& backend) noexcept override { m_scene.frame_from_source(backend); }

private:
	Scene& m_scene;
	graphics::FramePlan m_plan {};
};

} // namespace eng::playfield
