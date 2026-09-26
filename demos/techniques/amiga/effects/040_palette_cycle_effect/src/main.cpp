#include <eng/api/api.hpp>
#include <eng/core/util/color.hpp>
#include <eng/graphics/effects/palette_cycle.hpp>
#include <eng/graphics/effects/palette_transition.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

namespace scene = eng::graphics::composition;
namespace effects = eng::graphics::effects;

constexpr eng::u16 screen_height = 256u;
constexpr eng::u16 bytes_per_row = 40u;
constexpr eng::u8 plane_count = 6u;
constexpr eng::u32 plane_bytes = 10240u;

/// Paleta base pensada para ciclar los colores 1..7.
///
/// El color 0 se deja negro para bordes/fondo. Los colores 1..7 forman una cinta
/// brillante que la demo rota cada frame sin tocar los bitplanes. En una aventura
/// EHB real, este mismo mecanismo serviria para agua, lamparas, paneles, fuegos o
/// reflejos.
constexpr eng::Palette32 source_palette {{
	0x000, 0xf00, 0xf80, 0xff0, 0x0f0, 0x0ff, 0x08f, 0xf0f,
	0x333, 0x555, 0x777, 0x999, 0xbbb, 0xddd, 0xfff, 0x222,
	0x024, 0x046, 0x068, 0x08a, 0x0ac, 0x0ce, 0x0ef, 0x124,
	0x246, 0x468, 0x68a, 0x8ac, 0xace, 0xcdf, 0xeef, 0x111,
}};

/// Version atenuada de una paleta (cada canal a `num/den`): destino del fundido del
/// segundo efecto. Usa `eng::Palette::fade_from` (la misma aritmetica `util::scale444`
/// que antes se hacia a mano): demuestra la paleta de juego como valor `constexpr`.
constexpr eng::Palette32 dim_palette_of(const eng::Palette32& src, eng::u16 num,
					     eng::u16 den) {
	eng::Palette p {};
	p.fade_from(src, num, den);
	return p.storage();
}
constexpr eng::Palette32 dim_source_palette = dim_palette_of(source_palette, 1u, 4u);

/// Zona inferior fija para comprobar que el scheduler mezcla una paleta animada
/// de base con una zona Copper estatica posterior.
constexpr eng::Palette32 lower_palette {{
	0x000, 0x04f, 0x06e, 0x08c, 0x0af, 0x2cf, 0x4ef, 0xfff,
	0x014, 0x026, 0x038, 0x04a, 0x05c, 0x06e, 0x08f, 0x123,
	0x008, 0x119, 0x22a, 0x33b, 0x44c, 0x55d, 0x66e, 0x88f,
	0x224, 0x446, 0x668, 0x88a, 0xaac, 0xcce, 0xeef, 0x112,
}};

constexpr eng::Palette32Zone palette_zones[] {
	{0x98, &lower_palette},
};

/// Escribe un patron planar estable que hace visible el ciclo de paleta.
///
/// La zona superior usa principalmente indices 1..7, justo el tramo que rota
/// `PaletteCycleEffect`. La zona inferior mantiene una reticula EHB parecida a la
/// demo 030 para demostrar que las zonas Copper siguen activas bajo el efecto.
void build_cycle_test_pattern(eng::PlaneBytes planes) {
	for (eng::u16 y = 0; y < screen_height; ++y) {
		const eng::u32 row_offset = static_cast<eng::u32>(y) * bytes_per_row;

		for (eng::u16 byte_x = 0; byte_x < bytes_per_row; ++byte_x) {
			eng::u8 index = 0;
			if (y < 136u) {
				// Bandas verticales anchas. Como cada byte son 8 pixels lowres,
				// dividir por 5 da celdas de 40 pixels: visibles y faciles de
				// analizar en una captura escalada por WinUAE.
				index = static_cast<eng::u8>(1u + ((byte_x / 5u) % 7u));
			} else {
				const eng::u8 cell_x = static_cast<eng::u8>(byte_x / 5u);
				const eng::u8 cell_y = static_cast<eng::u8>(((y - 136u) / 15u) & 7u);
				const eng::u8 half_brite = (cell_y >= 4u) ? 32u : 0u;
				index = static_cast<eng::u8>(((cell_y & 3u) * 8u + cell_x) | half_brite);
			}

			const eng::u32 byte_index = row_offset + byte_x;
			for (eng::u8 plane = 0; plane < plane_count; ++plane) {
				eng::u8* plane_base = planes.data() + static_cast<eng::u32>(plane) * plane_bytes;
				plane_base[byte_index] = (index & (1u << plane)) ? 0xffu : 0x00u;
			}
		}
	}
}

/// Demo de los primeros efectos reutilizables, compuestos.
///
/// La demo no redibuja pixels ni recompila toda la copperlist cada frame. Dos efectos
/// aportan **parches de paleta distintos** al mismo `FramePlan`: el ciclo rota el tramo
/// `COLOR01..07` y la transicion funde `COLOR16..31` entre la paleta base y una atenuada.
/// La escena parchea solo las words de valor de los MOVEs correspondientes. Ese es
/// el patron que luego usaremos en drivers mas ambiciosos: el juego pide efectos, el plan
/// describe los cambios y el driver decide como escribirlos en el hardware.
struct DemoGame {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			scene::chip_bytes_for(scene::planar(320u, 256u, 6)),
			8u * 1024u,
			4u * 1024u,
		});

		m_cycle.configure({1, 7, 1});
		// Enlaza la paleta cocinada al efecto y usa su paleta runtime para la escena.
		m_cycle.bind_source(source_palette);

		// Segundo efecto: fundido del tramo `COLOR16..31` entre la paleta base y una
		// atenuada, en vaiven. Parche distinto del ciclo: se componen sin solaparse.
		m_trans.configure({16, 16, 48, true});
		m_trans.bind(source_palette, dim_source_palette);

		const eng::usize gfx_zone_count = sizeof(palette_zones) / sizeof(palette_zones[0]);
		scene::PaletteZone gfx_zones[sizeof(palette_zones) / sizeof(palette_zones[0])] {};
		for (eng::usize i = 0; i < gfx_zone_count; ++i) {
			gfx_zones[i] = scene::PaletteZone {palette_zones[i].line, eng::PaletteWords {palette_zones[i].palette->color, 32u}, 0u, 32u};
		}
		scene::SceneResources res = scene::planar(320u, 256u, 6);
		res.mode = scene::SceneMode::Ehb;
		m_scene_ok = scene::compose(m_scene, backend.memory(), res,
				    scene::ocs_a500,
				    scene::display(res),
				    scene::palette_patchable(eng::PaletteWords {source_palette.color, 32u}, 0u, 32u, &m_base_zone),
				    scene::palette_zones(eng::Span<const scene::PaletteZone> {gfx_zones, gfx_zone_count}));
		if (m_scene.ok()) {
			build_cycle_test_pattern(m_scene.bitplanes());
			m_scene.takeover(backend);
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000040u);
		}
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_memory_ok || !m_scene_ok) {
			return;
		}

		m_cycle.update(context.frame.frame_index);
		m_trans.update(context.frame.frame_index);

		// Recalcula las paletas runtime (sin FramePlan) y parchea las palabras COLOR de
		// la copperlist ya instalada: tramo del ciclo (COLOR01..07) y fundido (16..31).
		m_cycle.refresh();
		m_trans.refresh();
		for (eng::u8 i = 1u; i < 8u; ++i) {
			scene::zone_color(m_scene.scheduler(), m_base_zone, i).set(m_cycle.runtime_palette().color[i]);
		}
		for (eng::u8 i = 16u; i < 32u; ++i) {
			scene::zone_color(m_scene.scheduler(), m_base_zone, i).set(m_trans.runtime_palette().color[i]);
		}

		// Esperamos varias fases antes de declarar READY para que el runner capture
		// una escena que ya ha demostrado animacion de paleta, no solo setup inicial.
		if (context.frame.frame_index >= 8u) {
			const eng::u32 detail =
				0x04000000u |
				(static_cast<eng::u32>(m_cycle.phase()) << 16u) |
				m_scene.words();
			eng::debug::mark_ready(g_eng_run_status, detail);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index, 9);
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	scene::Scene m_scene {};
	scene::PatchZone m_base_zone {};
	effects::PaletteCycleEffect m_cycle {};
	effects::PaletteTransitionEffect m_trans {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
