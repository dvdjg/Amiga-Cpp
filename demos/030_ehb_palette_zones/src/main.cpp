#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/graphics/copper/copper.hpp>
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

constexpr amg::u16 screen_width = 320;
constexpr amg::u16 screen_height = 256;
constexpr amg::u16 bytes_per_row = screen_width / 8;
constexpr amg::u16 plane_count = 6;
constexpr amg::u32 plane_bytes = static_cast<amg::u32>(bytes_per_row) * screen_height;
constexpr amg::u32 bitplane_bytes = plane_bytes * plane_count;

/// Paleta RGB444 de 32 colores base para la zona superior.
///
/// En EHB, los indices 0..31 usan estos registros directamente y los indices
/// 32..63 muestran una version a media intensidad de esos mismos 32 colores. No
/// existe una segunda paleta fisica: el sexto bitplane activa el modo half-brite.
constexpr amg::u16 top_palette[32] = {
	0x000, 0xf00, 0x0f0, 0x00f, 0xff0, 0xf0f, 0x0ff, 0xfff,
	0x800, 0x080, 0x008, 0x880, 0x808, 0x088, 0xaaa, 0x444,
	0xf80, 0x8f0, 0x08f, 0xf08, 0x80f, 0x0f8, 0xc44, 0x4c4,
	0x44c, 0xcc4, 0xc4c, 0x4cc, 0xe86, 0x6e8, 0x86e, 0x222,
};

/// Segunda zona: colores mas calidos para demostrar que el Copper puede cambiar
/// totalmente el significado de los mismos indices graficos a media pantalla.
constexpr amg::u16 middle_palette[32] = {
	0x000, 0xf40, 0xe60, 0xc80, 0xfa0, 0xfca, 0xa42, 0xfff,
	0x520, 0x730, 0x950, 0xb70, 0xd90, 0xeb0, 0xfd0, 0x321,
	0x600, 0x810, 0xa20, 0xc30, 0xe40, 0xf62, 0xf84, 0xfa6,
	0x642, 0x864, 0xa86, 0xca8, 0xeca, 0xfec, 0x986, 0x210,
};

/// Tercera zona: colores frios. La misma memoria de bitplanes produce otra
/// lectura visual porque los COLORxx cambian durante el barrido.
constexpr amg::u16 bottom_palette[32] = {
	0x000, 0x04f, 0x06e, 0x08c, 0x0af, 0x2cf, 0x4ef, 0xfff,
	0x014, 0x026, 0x038, 0x04a, 0x05c, 0x06e, 0x08f, 0x123,
	0x008, 0x119, 0x22a, 0x33b, 0x44c, 0x55d, 0x66e, 0x88f,
	0x224, 0x446, 0x668, 0x88a, 0xaac, 0xcce, 0xeef, 0x112,
};

/// Genera una imagen de prueba pensada para analisis automatico.
///
/// La pantalla se divide en una reticula de 8 columnas x 8 filas. Cada celda usa
/// un indice distinto de 0..63; las cuatro primeras filas son colores normales y
/// las cuatro ultimas son half-brite. Al repetir la reticula bajo tres paletas
/// Copper diferentes, comprobamos dos cosas a la vez: 6 bitplanes EHB y cambios
/// completos de paleta por zonas.
void build_ehb_test_pattern(amg::u8* planes) {
	// Cada celda mide 40 pixeles de ancho, exactamente 5 bytes lowres. Eso nos
	// permite escribir bytes completos en cada bitplane: 0xff si el bit de color
	// esta activo para los 8 pixeles de ese byte, 0x00 si no lo esta. Esta version
	// es mucho mas fiel a como cargaremos assets reales desde UAF-R: el conversor
	// de PC ya entregara datos planares listos para DMA, y el Amiga solo tendra
	// que copiarlos o instalarlos.
	for (amg::u16 y = 0; y < screen_height; ++y) {
		const amg::u8 cell_y = static_cast<amg::u8>((y & 0x7fu) / 16u);
		const amg::u8 half_brite_bit = (cell_y >= 4u) ? 32u : 0u;
		const amg::u32 row_offset = static_cast<amg::u32>(y) * bytes_per_row;

		for (amg::u16 byte_x = 0; byte_x < bytes_per_row; ++byte_x) {
			const amg::u8 cell_x = static_cast<amg::u8>(byte_x / 5u);
			const amg::u8 base = static_cast<amg::u8>((cell_y & 3u) * 8u + cell_x);
			const amg::u8 index = static_cast<amg::u8>(base | half_brite_bit);
			const amg::u32 byte_index = row_offset + byte_x;

			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				amg::u8* plane_base = planes + static_cast<amg::u32>(plane) * plane_bytes;
				plane_base[byte_index] = (index & (1u << plane)) ? 0xffu : 0x00u;
			}
		}
	}
}

void emit_palette(amg::copper::ListBuilder& copper, const amg::u16* palette) {
	for (amg::u8 i = 0; i < 32; ++i) {
		copper.move(amg::copper::color_register(i), palette[i]);
	}
}

/// Demo EHB con zonas de paleta Copper.
///
/// Este es el primer embrion del futuro driver `EhbScene`: ya reserva bitplanes
/// reales en Chip RAM, programa BPL pointers, activa 6 planos y usa el Copper para
/// recontextualizar colores por bandas verticales. Todavia no hay loader UAF ni
/// scheduler de efectos; toda la escena se cocina a mano para que sea facil de
/// auditar.
struct DemoGame {
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		m_memory_ok = backend.configure_memory({
			68u * 1024u, // Chip: 6 bitplanes EHB + copperlist, sin pedir margen inutil.
			8u * 1024u,  // Slow: metadatos futuros del driver.
			4u * 1024u,  // Frame scratch.
		});

		const amg::MemoryBlock bitplanes = backend.memory().chip.allocate(bitplane_bytes, 16);
		const amg::MemoryBlock copper_memory = backend.memory().chip.allocate(1024, 16);
		m_bitplanes = static_cast<amg::u8*>(bitplanes.data);

		if (bitplanes.valid()) {
			build_ehb_test_pattern(m_bitplanes);
		}

		amg::copper::ListBuilder copper { copper_memory };

		// El display usa 6 bitplanes lowres. `0x6000` codifica BPU=6 y `0x0200`
		// mantiene el display color en OCS. HAM queda apagado, por tanto el sexto
		// plano se interpreta como Extra Half-Brite.
		copper.move(amg::copper::Register::BPLCON0, 0x6200);
		copper.move(amg::copper::Register::BPLCON1, 0x0000);
		copper.move(amg::copper::Register::BPLCON2, 0x0000);
		copper.move(amg::copper::Register::BPL1MOD, 0x0000);
		copper.move(amg::copper::Register::BPL2MOD, 0x0000);
		copper.move(amg::copper::Register::DIWSTRT, 0x2c81);
		copper.move(amg::copper::Register::DIWSTOP, 0x2cc1);
		copper.move(amg::copper::Register::DDFSTRT, 0x0038);
		copper.move(amg::copper::Register::DDFSTOP, 0x00d0);

		for (amg::u8 plane = 0; plane < plane_count; ++plane) {
			copper.move_bitplane_pointer(plane, m_bitplanes + static_cast<amg::u32>(plane) * plane_bytes);
		}

		emit_palette(copper, top_palette);
		copper.wait_line(0x70);
		emit_palette(copper, middle_palette);
		copper.wait_line(0xb8);
		emit_palette(copper, bottom_palette);
		copper.wait_line(0xf8);
		copper.move(amg::copper::Register::COLOR00, 0x0000);
		copper.end();

		m_copper_ok = copper.ok();
		m_copper_words = copper.words_used();
		m_copper_words_ptr = copper.data();

		if (m_memory_ok && bitplanes.valid() && m_copper_ok) {
			backend.install_copper_list(m_copper_words_ptr);
			amg::debug::mark_ready(g_amg_run_status, static_cast<amg::u32>(m_copper_words));
		} else {
			amg::debug::mark_failed(g_amg_run_status, 0x00000030u);
		}
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		if (m_copper_ok) {
			backend.install_copper_list(m_copper_words_ptr);
		}
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		// No dibujamos overlay: el analizador debe leer solo pixeles producidos por
		// bitplanes EHB y cambios de paleta Copper.
		if (m_copper_ok && m_copper_words > 0) {
			backend.install_copper_list(m_copper_words_ptr);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	amg::u16 m_copper_words = 0;
	amg::u8* m_bitplanes = nullptr;
	const amg::u16* m_copper_words_ptr = nullptr;
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
