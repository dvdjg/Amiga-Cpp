#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/graphics/drivers/ehb_scene.hpp>
#include <amg/graphics/effects/palette_cycle.hpp>
#include <amg/graphics/frame_plan.hpp>
#include <amg/platform/amiga_minimal.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile amg::debug::RunStatus g_amg_run_status {
	amg::debug::run_status_magic,
	amg::debug::run_status_version,
	static_cast<amg::u16>(amg::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

namespace drivers = amg::graphics::drivers;
namespace effects = amg::graphics::effects;

constexpr amg::u16 screen_height = drivers::StaticEhbScene::height;
constexpr amg::u16 bytes_per_row = drivers::StaticEhbScene::bytes_per_row;
constexpr amg::u8 plane_count = drivers::StaticEhbScene::plane_count;
constexpr amg::u32 plane_bytes = drivers::StaticEhbScene::plane_bytes;

/// Paleta base pensada para ciclar los colores 1..7.
///
/// El color 0 se deja negro para bordes/fondo. Los colores 1..7 forman una cinta
/// brillante que la demo rota cada frame sin tocar los bitplanes. En una aventura
/// EHB real, este mismo mecanismo serviria para agua, lamparas, paneles, fuegos o
/// reflejos.
constexpr drivers::EhbPalette source_palette {{
	0x000, 0xf00, 0xf80, 0xff0, 0x0f0, 0x0ff, 0x08f, 0xf0f,
	0x333, 0x555, 0x777, 0x999, 0xbbb, 0xddd, 0xfff, 0x222,
	0x024, 0x046, 0x068, 0x08a, 0x0ac, 0x0ce, 0x0ef, 0x124,
	0x246, 0x468, 0x68a, 0x8ac, 0xace, 0xcdf, 0xeef, 0x111,
}};

/// Zona inferior fija para comprobar que el scheduler mezcla una paleta animada
/// de base con una zona Copper estatica posterior.
constexpr drivers::EhbPalette lower_palette {{
	0x000, 0x04f, 0x06e, 0x08c, 0x0af, 0x2cf, 0x4ef, 0xfff,
	0x014, 0x026, 0x038, 0x04a, 0x05c, 0x06e, 0x08f, 0x123,
	0x008, 0x119, 0x22a, 0x33b, 0x44c, 0x55d, 0x66e, 0x88f,
	0x224, 0x446, 0x668, 0x88a, 0xaac, 0xcce, 0xeef, 0x112,
}};

constexpr drivers::EhbPaletteZone palette_zones[] {
	{0x98, &lower_palette},
};

/// Escribe un patron planar estable que hace visible el ciclo de paleta.
///
/// La zona superior usa principalmente indices 1..7, justo el tramo que rota
/// `PaletteCycleEffect`. La zona inferior mantiene una reticula EHB parecida a la
/// demo 030 para demostrar que las zonas Copper siguen activas bajo el efecto.
void build_cycle_test_pattern(amg::u8* planes) {
	for (amg::u16 y = 0; y < screen_height; ++y) {
		const amg::u32 row_offset = static_cast<amg::u32>(y) * bytes_per_row;

		for (amg::u16 byte_x = 0; byte_x < bytes_per_row; ++byte_x) {
			amg::u8 index = 0;
			if (y < 136u) {
				// Bandas verticales anchas. Como cada byte son 8 pixels lowres,
				// dividir por 5 da celdas de 40 pixels: visibles y faciles de
				// analizar en una captura escalada por WinUAE.
				index = static_cast<amg::u8>(1u + ((byte_x / 5u) % 7u));
			} else {
				const amg::u8 cell_x = static_cast<amg::u8>(byte_x / 5u);
				const amg::u8 cell_y = static_cast<amg::u8>(((y - 136u) / 15u) & 7u);
				const amg::u8 half_brite = (cell_y >= 4u) ? 32u : 0u;
				index = static_cast<amg::u8>(((cell_y & 3u) * 8u + cell_x) | half_brite);
			}

			const amg::u32 byte_index = row_offset + byte_x;
			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				amg::u8* plane_base = planes + static_cast<amg::u32>(plane) * plane_bytes;
				plane_base[byte_index] = (index & (1u << plane)) ? 0xffu : 0x00u;
			}
		}
	}
}

/// Demo del primer efecto reutilizable.
///
/// La demo no redibuja pixels ni recompila toda la copperlist cada frame. Modifica
/// una paleta runtime, escribe una intencion en `FramePlan` y deja que
/// `StaticEhbScene` parchee solo las words de valor de los MOVEs `COLOR01..07`.
/// Ese es el patron que luego usaremos en drivers mas ambiciosos: el juego pide un
/// efecto, el plan describe el cambio y el driver decide como escribirlo en el
/// hardware.
struct DemoGame {
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		m_memory_ok = backend.configure_memory({
			68u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});

		m_cycle.configure({1, 7, 1});
		m_cycle.apply(source_palette, m_runtime_palette);

		const drivers::StaticEhbSceneConfig scene_config {
			&m_runtime_palette,
			palette_zones,
			static_cast<amg::u8>(sizeof(palette_zones) / sizeof(palette_zones[0])),
			1024,
		};

		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (m_scene.ok()) {
			build_cycle_test_pattern(m_scene.bitplanes());
			m_scene.install(backend);
		} else {
			amg::debug::mark_failed(g_amg_run_status, 0x00000040u);
		}
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		if (!m_memory_ok || !m_scene_ok) {
			return;
		}

		m_cycle.update(context.frame.frame_index);
		m_cycle.apply(source_palette, m_runtime_palette);

		m_frame_plan.clear();
		if (
			!m_frame_plan.add_base_palette_patch(m_runtime_palette.color, 1, 7) ||
			!m_scene.apply_frame_plan(m_frame_plan)
		) {
			m_scene_ok = false;
			amg::debug::mark_failed(g_amg_run_status, 0x00000042u);
			return;
		}

		m_scene.install(backend);

		// Esperamos varias fases antes de declarar READY para que el runner capture
		// una escena que ya ha demostrado animacion de paleta, no solo setup inicial.
		if (context.frame.frame_index >= 8u) {
			const amg::u32 detail =
				0x04000000u |
				(static_cast<amg::u32>(m_cycle.phase()) << 16u) |
				m_scene.copper_words();
			amg::debug::mark_ready(g_amg_run_status, detail);
		}
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index, 9);
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	drivers::StaticEhbScene m_scene {};
	drivers::EhbPalette m_runtime_palette {};
	amg::graphics::FramePlan m_frame_plan {};
	effects::PaletteCycleEffect m_cycle {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	amg::debug::reset(g_amg_run_status);

	amg::amiga::MinimalBackend backend {};
	DemoGame game {};
	amg::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
