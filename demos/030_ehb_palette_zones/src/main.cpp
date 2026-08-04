#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

namespace ehb = eng::graphics::drivers;

constexpr eng::u16 screen_height = ehb::StaticEhbScene::height;
constexpr eng::u16 bytes_per_row = ehb::StaticEhbScene::bytes_per_row;
constexpr eng::u8 plane_count = ehb::StaticEhbScene::plane_count;
constexpr eng::u32 plane_bytes = ehb::StaticEhbScene::plane_bytes;

/// Paleta RGB444 de 32 colores base para la zona superior.
///
/// En EHB, los indices 0..31 usan estos registros directamente y los indices
/// 32..63 muestran una version a media intensidad de esos mismos 32 colores. No
/// existe una segunda paleta fisica: el sexto bitplane activa el modo half-brite.
constexpr ehb::EhbPalette top_palette {{
	0x000, 0xf00, 0x0f0, 0x00f, 0xff0, 0xf0f, 0x0ff, 0xfff,
	0x800, 0x080, 0x008, 0x880, 0x808, 0x088, 0xaaa, 0x444,
	0xf80, 0x8f0, 0x08f, 0xf08, 0x80f, 0x0f8, 0xc44, 0x4c4,
	0x44c, 0xcc4, 0xc4c, 0x4cc, 0xe86, 0x6e8, 0x86e, 0x222,
}};

/// Segunda zona: colores mas calidos para demostrar que el Copper puede cambiar
/// totalmente el significado de los mismos indices graficos a media pantalla.
constexpr ehb::EhbPalette middle_palette {{
	0x000, 0xf40, 0xe60, 0xc80, 0xfa0, 0xfca, 0xa42, 0xfff,
	0x520, 0x730, 0x950, 0xb70, 0xd90, 0xeb0, 0xfd0, 0x321,
	0x600, 0x810, 0xa20, 0xc30, 0xe40, 0xf62, 0xf84, 0xfa6,
	0x642, 0x864, 0xa86, 0xca8, 0xeca, 0xfec, 0x986, 0x210,
}};

/// Tercera zona: colores frios. La misma memoria de bitplanes produce otra
/// lectura visual porque los COLORxx cambian durante el barrido.
constexpr ehb::EhbPalette bottom_palette {{
	0x000, 0x04f, 0x06e, 0x08c, 0x0af, 0x2cf, 0x4ef, 0xfff,
	0x014, 0x026, 0x038, 0x04a, 0x05c, 0x06e, 0x08f, 0x123,
	0x008, 0x119, 0x22a, 0x33b, 0x44c, 0x55d, 0x66e, 0x88f,
	0x224, 0x446, 0x668, 0x88a, 0xaac, 0xcce, 0xeef, 0x112,
}};

/// Zonas de paleta de alto nivel.
///
/// La demo ya no escribe instrucciones `WAIT/MOVE` directamente. El driver copia
/// estas intenciones a una copperlist real mediante el `CopperScheduler`, que es
/// el punto donde se mezclaran varios sistemas: fondo, color cycling, luces, agua
/// o UI.
constexpr ehb::EhbPaletteZone palette_zones[] {
	{0x70, &middle_palette},
	{0xb8, &bottom_palette},
};

/// Genera una imagen de prueba pensada para analisis automatico.
///
/// La pantalla se divide en una reticula de 8 columnas x 8 filas. Cada celda usa
/// un indice distinto de 0..63; las cuatro primeras filas son colores normales y
/// las cuatro ultimas son half-brite. Al repetir la reticula bajo tres paletas
/// Copper diferentes, comprobamos dos cosas a la vez: 6 bitplanes EHB y cambios
/// completos de paleta por zonas.
void build_ehb_test_pattern(eng::u8* planes) {
	// Cada celda mide 40 pixeles de ancho, exactamente 5 bytes lowres. Eso nos
	// permite escribir bytes completos en cada bitplane: 0xff si el bit de color
	// esta activo para los 8 pixeles de ese byte, 0x00 si no lo esta. Esta version
	// es mucho mas fiel a como cargaremos assets reales desde UAF-R: el conversor
	// de PC ya entregara datos planares listos para DMA, y el Amiga solo tendra
	// que copiarlos o instalarlos.
	for (eng::u16 y = 0; y < screen_height; ++y) {
		const eng::u8 cell_y = static_cast<eng::u8>((y & 0x7fu) / 16u);
		const eng::u8 half_brite_bit = (cell_y >= 4u) ? 32u : 0u;
		const eng::u32 row_offset = static_cast<eng::u32>(y) * bytes_per_row;

		for (eng::u16 byte_x = 0; byte_x < bytes_per_row; ++byte_x) {
			const eng::u8 cell_x = static_cast<eng::u8>(byte_x / 5u);
			const eng::u8 base = static_cast<eng::u8>((cell_y & 3u) * 8u + cell_x);
			const eng::u8 index = static_cast<eng::u8>(base | half_brite_bit);
			const eng::u32 byte_index = row_offset + byte_x;

			for (eng::u8 plane = 0; plane < plane_count; ++plane) {
				eng::u8* plane_base = planes + static_cast<eng::u32>(plane) * plane_bytes;
				plane_base[byte_index] = (index & (1u << plane)) ? 0xffu : 0x00u;
			}
		}
	}
}

/// Demo EHB con zonas de paleta Copper.
///
/// Esta demo ya usa `StaticEhbScene`, el primer driver reutilizable del engine. El
/// juego sigue cocinando un patron didactico porque aun no existe loader UAF-R, pero
/// ya no sabe nada de BPLCON0, BPLxPTH/PTL, DIW/DDF ni COLORxx. Esa traduccion vive
/// en el driver, como ocurrira con futuros drivers `DualPlayfield`, `FakeDPF` o
/// `CopperHeavy`.
struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			68u * 1024u, // Chip: 6 bitplanes EHB + copperlist, sin pedir margen inutil.
			8u * 1024u,  // Slow: metadatos futuros del driver.
			4u * 1024u,  // Frame scratch.
		});

		const ehb::StaticEhbSceneConfig scene_config {
			&top_palette,
			palette_zones,
			static_cast<eng::u8>(sizeof(palette_zones) / sizeof(palette_zones[0])),
			1024,
		};

		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (m_scene.ok()) {
			build_ehb_test_pattern(m_scene.bitplanes());
		}

		if (m_memory_ok && m_scene_ok) {
			m_scene.install(backend);
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_scene.copper_words()));
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000030u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// No dibujamos overlay: el analizador debe leer solo pixeles producidos por
		// bitplanes EHB y cambios de paleta Copper.
		if (m_scene.ok() && m_scene.copper_words() > 0) {
			m_scene.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	ehb::StaticEhbScene m_scene {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
