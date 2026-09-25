#pragma once

/// \file world.hpp
/// **Mundo retenido** (`eng::scene`): contenedor **aditivo** de **capas** (`Layer`), cada
/// una con su **cámara** (`Camera2D`) y su profundidad. Es el primer escalón del modelo
/// `SCENE_AND_RESOURCES.md` (`World` → `Layer` → cámara): hoy guarda y ordena las capas y
/// expone su cámara; el **planner** que las materializa (playfield/tilemap/efecto) y el
/// reparto de recursos llegan después, sobre este contenedor.
///
/// ```cpp
/// auto fondo = app.world().add_layer("fondo", 0);
/// if (fondo) {
///     fondo->camera().reset({{0, 0, 640, 256}}, {320, 256});
///     fondo->camera().set_scroll_x(128);        // `layer.camera().scroll_x`
/// }
/// ```
///
/// `add_layer` devuelve `Ref<Layer>` (**anulable**): si el mundo está lleno devuelve uno
/// inválido en vez de un `Layer&` a memoria basura, para no fallar en silencio.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/scene/virtual_scene.hpp>

namespace eng::scene {

/// Una capa del mundo: identidad, profundidad y cámara. El engine la materializa como
/// playfield/tilemap/efecto (planner); el juego describe y lee su cámara.
class Layer {
public:
	constexpr void configure(const char* id, u8 depth) noexcept {
		m_id = id;
		m_depth = depth;
	}
	[[nodiscard]] constexpr const char* id() const noexcept { return m_id; }
	[[nodiscard]] constexpr u8 depth() const noexcept { return m_depth; }
	/// Cámara de la capa: su `scroll_x`/`scroll_y` es la ventana al mundo (`PUBLIC_GAME_API.md` §2.1.3).
	[[nodiscard]] constexpr Camera2D& camera() noexcept { return m_camera; }
	[[nodiscard]] constexpr const Camera2D& camera() const noexcept { return m_camera; }

private:
	const char* m_id = "";
	u8 m_depth = 0;
	Camera2D m_camera {};
};

/// **Mundo**: conjunto fijo de capas (sin heap). El orden de dibujo lo fija la profundidad
/// (menor = al fondo); el planner lo usará al componer.
template <u8 MaxLayers = 8u>
class World {
public:
	/// Añade una capa. Devuelve `Ref<Layer>` inválido si el mundo está lleno (no hay fallo
	/// silencioso: comprueba `if (fondo)`).
	[[nodiscard]] Ref<Layer> add_layer(const char* id, u8 depth) noexcept {
		if (m_count >= MaxLayers) {
			return {};
		}
		Layer& l = m_layers[m_count];
		l.configure(id, depth);
		++m_count;
		return l;
	}

	[[nodiscard]] constexpr u8 count() const noexcept { return m_count; }
	[[nodiscard]] constexpr u8 capacity() const noexcept { return MaxLayers; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_count >= MaxLayers; }

	/// Capa por índice (`nullptr` si fuera de rango).
	[[nodiscard]] Layer* layer(u8 i) noexcept { return i < m_count ? &m_layers[i] : nullptr; }
	[[nodiscard]] const Layer* layer(u8 i) const noexcept {
		return i < m_count ? &m_layers[i] : nullptr;
	}
	/// Capa por id (`nullptr` si no existe).
	[[nodiscard]] Layer* find(const char* id) noexcept {
		for (u8 i = 0; i < m_count; ++i) {
			if (same_id(m_layers[i].id(), id)) {
				return &m_layers[i];
			}
		}
		return nullptr;
	}

private:
	[[nodiscard]] static bool same_id(const char* a, const char* b) noexcept {
		if (a == nullptr || b == nullptr) {
			return false;
		}
		while (*a != '\0' && *a == *b) {
			++a;
			++b;
		}
		return *a == *b;
	}

	Layer m_layers[MaxLayers] {};
	u8 m_count = 0u;
};

} // namespace eng::scene
