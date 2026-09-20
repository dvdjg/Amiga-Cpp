#pragma once

/// \file compose.hpp
/// **Composición de escenas** (prototipo del modelo de tres planos): una escena se construye
/// uniendo **etapas** que piden recursos y emiten Copper, en vez de una clase por driver.
/// Ver `docs/engine/architecture/SCENE_COMPOSITION.md`.
///
/// Uso:
///
/// ```cpp
/// scene::Scene s;
/// scene::compose(s, memory, scene::planar4(320, 256),
///     scene::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x4200),
///     scene::palette(pal, 0, 16));
/// s.install(backend);        // o takeover
/// ```
///
/// Una etapa es un callable `void(Scene&)`: pide lo que necesita a la escena (geometría,
/// planos) y emite al `copper::Scheduler`. Los **presets** son funciones que devuelven un
/// `SceneResources`; no hay clase por efecto. El **programa** es data (Copper) que ejecuta
/// el chip; la **variabilidad** se hace con `copper::PatchHandle` (`scheduler().patchable`).

#include <eng/core/domains.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::scene {

/// **Recursos** que una escena planar necesita (plano de recursos del modelo).
struct SceneResources {
	u16 width = 320;   ///< ancho visible (múltiplo de 16)
	u16 height = 256;  ///< filas del display
	u16 rows = 0;      ///< filas lógicas del bitmap (0 = igual a `height`)
	u8 planes = 4;     ///< planos de bitplane
	u32 copper_bytes = 4096;
};

/// Escena viva: posee los bitplanes (contiguos) y la copperlist, y el emisor de Copper.
/// No reserva al sistema más que a través de la `MemorySystem` del backend.
class Scene {
public:
	bool init(MemorySystem& memory, const SceneResources& res) {
		m_res = res;
		const u16 row = row_bytes();
		const u16 rows = res.rows != 0u ? res.rows : res.height;
		if (row == 0u || rows == 0u || res.planes == 0u) {
			return false;
		}
		m_plane_bytes = static_cast<u32>(row) * rows;
		m_bitplanes = memory.chip.allocate_block<eng::PlaneTag>(m_plane_bytes * res.planes + 16u, 16);
		m_copper = memory.chip.allocate_block<eng::CopperTag>(res.copper_bytes, 16);
		if (!m_bitplanes.valid() || !m_copper.valid()) {
			return false;
		}
		m_sched.retarget(m_copper);
		return true;
	}

	[[nodiscard]] copper::Scheduler& scheduler() { return m_sched; }
	[[nodiscard]] const copper::Scheduler& scheduler() const { return m_sched; }
	[[nodiscard]] constexpr eng::PlaneBytes bitplanes() const { return m_bitplanes.view; }
	[[nodiscard]] constexpr u32 plane_bytes() const { return m_plane_bytes; }
	[[nodiscard]] constexpr u16 row_bytes() const {
		return static_cast<u16>((m_res.width / 8u) & ~1u);
	}
	[[nodiscard]] constexpr u8 planes() const { return m_res.planes; }
	[[nodiscard]] constexpr u16 width() const { return m_res.width; }
	[[nodiscard]] constexpr u16 height() const { return m_res.height; }
	[[nodiscard]] bool ok() const { return m_sched.ok(); }
	[[nodiscard]] const copper::ScheduleReport& report() const { return m_sched.report(); }

	template <typename Backend>
	void install(Backend& backend) const {
		if (m_sched.ok()) backend.install_copper_list(m_sched.data());
	}
	template <typename Backend>
	void takeover(Backend& backend) const {
		if (m_sched.ok()) backend.takeover_display(m_sched.data());
	}

private:
	SceneResources m_res {};
	eng::Block<eng::PlaneTag> m_bitplanes {};
	eng::Block<eng::CopperTag> m_copper {};
	copper::Scheduler m_sched {};
	u32 m_plane_bytes = 0;
};

/// Preset: escena planar de 4 planos por defecto.
[[nodiscard]] constexpr SceneResources planar4(u16 width = 320, u16 height = 256,
						u8 planes = 4) {
	SceneResources r {};
	r.width = width;
	r.height = height;
	r.planes = planes;
	return r;
}

/// Etapa de **display**: BPLCON0, DIW/DDF y punteros BPLxPT (planos contiguos).
[[nodiscard]] inline auto display(u16 diwstrt, u16 diwstop, u16 ddfstrt, u16 ddfstop,
				  u16 bplcon0) {
	return [=](Scene& sc) {
		sc.scheduler().emit_planes_display(diwstrt, diwstop, ddfstrt, ddfstop,
						   sc.row_bytes(), bplcon0, sc.planes(),
						   sc.bitplanes(), sc.plane_bytes());
	};
}

/// Etapa de **paleta**: carga `count` colores desde `first`.
[[nodiscard]] inline auto palette(eng::PaletteWords colors, u8 first = 0, u8 count = 32) {
	return [=](Scene& sc) { sc.scheduler().emit_palette(colors, first, count); };
}

/// Compone: inicializa la escena con `res` y ejecuta las etapas en orden, cierra la lista.
template <class... Stages>
bool compose(Scene& scene, MemorySystem& memory, const SceneResources& res, Stages... stages) {
	if (!scene.init(memory, res)) {
		return false;
	}
	(stages(scene), ...);
	scene.scheduler().end();
	return scene.ok();
}

} // namespace eng::graphics::scene
