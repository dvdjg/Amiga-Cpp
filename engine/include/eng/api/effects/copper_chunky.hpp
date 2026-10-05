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

/// **Display copper chunky** de alto nivel: compone la escena (sin bitplanes, modo
/// `SceneMode::CopperChunky`) y emite la estructura de la lista; el juego escribe los colores
/// del frame con `row(y)`.
///
/// ```cpp
/// eng::effects::CopperChunky fx;
/// fx.init(scene, memory, limits, {.cols = 36, .rows = 64});
/// // por frame:
/// fx.begin_frame(scene);
/// for (y...) { u16* p = fx.row(y); ... }   // escribe los colores
/// fx.end_frame(scene, backend);            // flip + install
/// ```
template <eng::u8 MaxCols = 64, eng::u8 MaxRows = 64>
class CopperChunky {
public:
	/// Compone la escena copper-chunky en `scene` (con `memory`/`limits`) y emite la estructura
	/// de la lista en los dos bloques del `Plan`. `false` si no cabe (geometría o memoria).
	/// \param scene        escena a componer (modo `CopperChunky`).
	/// \param memory       gestor de memoria (Chip).
	/// \param limits       perfil de display (OCS/ECS/AGA).
	/// \param cfg          geometría del efecto `{.cols, .rows}`.
	/// \param width,height tamaño de pantalla del efecto.
	/// \param copper_bytes capacidad reservada de copperlist.
	/// \return `false` si no cabe.
	[[nodiscard]] bool init(graphics::composition::Scene& scene, eng::MemoryManager& memory,
				const graphics::composition::DisplayLimits& limits,
				graphics::composition::CopperChunkyConfig cfg,
				eng::u16 width = 288u, eng::u16 height = 256u,
				eng::u32 copper_bytes = 12288u) {
		graphics::composition::SceneResources res =
			graphics::composition::planar(width, height, 0u);
		res.mode = graphics::composition::SceneMode::CopperChunky;
		res.copper_bytes = copper_bytes;
		if (!graphics::composition::compose(scene, memory, res, limits)) {
			return false;
		}
		return m_layer.attach(scene, cfg);
	}

	/// Inicia el frame (destino = bloque inactivo de la copperlist).
	void begin_frame(graphics::composition::Scene& scene) { m_layer.begin_frame(scene); }

	/// `data` del bloque 0 de la fila `row` para escribir los colores (paso de 2 words).
	[[nodiscard]] eng::u16* row(eng::u8 r) const { return m_layer.row(r); }

	/// Cierra el frame: voltea al bloque escrito y lo publica (swap de `COP1LC`).
	template <typename Backend>
	void end_frame(graphics::composition::Scene& scene, Backend& backend) {
		m_layer.end_frame(scene, backend);
	}

	[[nodiscard]] eng::u8 cols() const { return m_layer.cols(); }
	[[nodiscard]] eng::u8 rows() const { return m_layer.rows(); }

private:
	graphics::composition::CopperChunkyLayer<MaxCols, MaxRows> m_layer {};
};

} // namespace eng::effects
