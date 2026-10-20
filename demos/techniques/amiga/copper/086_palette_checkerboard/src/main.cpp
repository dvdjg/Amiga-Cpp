// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/copper/086_palette_checkerboard --debug --clean
//   Capturar  : bash ./tools/run/run-demo.sh demos/techniques/amiga/copper/086_palette_checkerboard --sequence-frames 6 --sequence-interval-ms 1000

#include <eng/api/api.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/limits.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/mode_switch.hpp>
#include <eng/graphics/palette_generator.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/setup/coherence.hpp>

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0u,
	0u,
};
}

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::usize;
using eng::s16;

constexpr u16 kScreenWidth = 320u;
constexpr u16 kScreenHeight = 256u;
constexpr u16 kBytesPerRow = kScreenWidth / 8u;
constexpr u16 kRasterOrigin = 0x2cu; // DIWSTRT vertical origin for the 320x256 PAL lores window.
constexpr u16 kFetchStart = 0x0038u; // Standard aligned DDFSTRT for 320 px lores fetch.
constexpr u16 kFetchStop = 0x00d0u;  // Standard aligned DDFSTOP for 320 px lores fetch.
constexpr u16 kCopperBytes = 4096u;  // Holds initial setup, 3 mode switches, and all palette MOVEs.
constexpr u16 kSwatchWidth = 4u;     // Four display pixels per palette sample.
constexpr u16 kGridGap = 16u;        // Separates swatches from the matrix and leaves an alignment gutter.
constexpr u16 kGridX = kSwatchWidth + kGridGap;
constexpr s16 kLabelX3 = 48;         // 16-pixel word boundary after the 8×8 matrix.
constexpr s16 kLabelX4 = 64;         // 16-pixel word boundary after the 16×16 matrix.
constexpr s16 kLabelX5 = 96;         // 16-pixel word boundary after the 32×32 matrix.
constexpr s16 kLabelXEhb = 160;      // 16-pixel word boundary after the 64×64 matrix.
constexpr s16 kFrameCounterY = 120;  // Final 8 rows of the EHB band, outside the checker matrix.
constexpr u8 kHudInk = 1u; // COLOR01 is white in every generated display palette.
constexpr eng::debug::Version kVersion {1u, 0u}; // The build injects ENG_BUILD_NUMBER.

constexpr u16 kBand3Top = 0u;
constexpr u16 kBand3Height = 32u; // 16 matrix rows plus room for four 8-pixel text rows.
constexpr u16 kBand4Top = kBand3Top + kBand3Height;
constexpr u16 kBand4Height = 32u; // 16×16 cells, each checker cell is 2 pixels high.
constexpr u16 kBand5Top = kBand4Top + kBand4Height;
constexpr u16 kBand5Height = 64u; // 32×32 cells, each checker cell is 2 pixels high.
constexpr u16 kBandEhbTop = kBand5Top + kBand5Height;
constexpr u16 kBandEhbHeight = 128u; // 64×64 cells, each checker cell is 2 pixels high.
static_assert(kBandEhbTop + kBandEhbHeight == kScreenHeight);

/// Bytes almacenados en un plano contiguo de una franja con `height` filas PAL lores.
constexpr u32 plane_bytes(u16 height) noexcept {
	return static_cast<u32>(kBytesPerRow) * height;
}
constexpr u32 kPlaneBytes3 = plane_bytes(kBand3Height);
constexpr u32 kPlaneBytes4 = plane_bytes(kBand4Height);
constexpr u32 kPlaneBytes5 = plane_bytes(kBand5Height);
constexpr u32 kPlaneBytesEhb = plane_bytes(kBandEhbHeight);
constexpr u32 kBand3Bytes = kPlaneBytes3 * 3u;
constexpr u32 kBand4Bytes = kPlaneBytes4 * 4u;
constexpr u32 kBand5Bytes = kPlaneBytes5 * 5u;
constexpr u32 kBandEhbBytes = kPlaneBytesEhb * 6u;

/// Incluye los extremos y algunos grises; COLOR00 es negro y COLOR01 se reserva para el HUD.
constexpr eng::graphics::PaletteGeneratorParams kPaletteParams {
	.transparent_index_zero = false,
	.include_black = true,
	.include_white = true,
	.extra_grays = 3u,
	.green_weight = 6u,
};

/// Mueve el blanco a COLOR01 para que el texto de depuración use solo el bitplane 0.
template <usize N>
constexpr eng::util::Array<u16, N> reserve_white_ink(eng::util::Array<u16, N> palette) noexcept {
	usize white = 0u;
	for (usize i = 1u; i < N; ++i) {
		if (palette[i] == 0x0fffu) { white = i; }
	}
	const u16 displaced = palette[1u];
	palette[1u] = palette[white];
	palette[white] = displaced;
	return palette;
}

/// La salida EHB contiene primero las bases y luego sus derivados half-bright; al mover el
/// blanco de base a COLOR01 hay que mover también su pareja de media intensidad.
constexpr eng::util::Array<u16, 64u> make_ehb_palette() noexcept {
	auto palette = eng::graphics::generate_palette<5u, eng::graphics::PaletteMode::Ehb>(kPaletteParams);
	usize white = 0u;
	for (usize i = 1u; i < 32u; ++i) {
		if (palette[i] == 0x0fffu) { white = i; }
	}
	const u16 displaced = palette[1u];
	palette[1u] = palette[white];
	palette[white] = displaced;
	const u16 displaced_half = palette[33u];
	palette[33u] = palette[32u + white];
	palette[32u + white] = displaced_half;
	return palette;
}

constexpr auto kPalette3 = reserve_white_ink(eng::graphics::generate_palette<3u>(kPaletteParams));
constexpr auto kPalette4 = reserve_white_ink(eng::graphics::generate_palette<4u>(kPaletteParams));
constexpr auto kPalette5 = reserve_white_ink(eng::graphics::generate_palette<5u>(kPaletteParams));
constexpr auto kPaletteEhb = make_ehb_palette();
static_assert(kPalette3[0u] == 0u && kPalette3[1u] == 0x0fffu);
static_assert(kPalette4[0u] == 0u && kPalette4[1u] == 0x0fffu);
static_assert(kPalette5[0u] == 0u && kPalette5[1u] == 0x0fffu);
static_assert(kPaletteEhb[0u] == 0u && kPaletteEhb[1u] == 0x0fffu);

/// Peso de luminancia RGB444 para ordenar los ejes; la paleta emitida conserva sus índices.
constexpr u16 luminance(u16 color) noexcept {
	return static_cast<u16>(3u * ((color >> 8u) & 15u) + 6u * ((color >> 4u) & 15u) + (color & 15u));
}

/// Ordena los índices por brillo, con desempate estable por índice, para que la distancia a la
/// diagonal de la matriz codifique directamente la diferencia de luminancia de cada pareja.
template <usize N>
constexpr eng::util::Array<u8, N> luminance_order(const eng::util::Array<u16, N>& palette) noexcept {
	eng::util::Array<u8, N> order {};
	for (usize i = 0u; i < N; ++i) { order[i] = static_cast<u8>(i); }
	for (usize i = 1u; i < N; ++i) {
		const u8 index = order[i];
		usize j = i;
		while (j > 0u) {
			const u8 previous = order[j - 1u];
			const u16 current_luma = luminance(palette[index]);
			const u16 previous_luma = luminance(palette[previous]);
			if (previous_luma < current_luma ||
			    (previous_luma == current_luma && previous < index)) { break; }
			order[j] = previous;
			--j;
		}
		order[j] = index;
	}
	return order;
}

constexpr auto kOrder3 = luminance_order(kPalette3);
constexpr auto kOrder4 = luminance_order(kPalette4);
constexpr auto kOrder5 = luminance_order(kPalette5);
constexpr auto kOrderEhb = luminance_order(kPaletteEhb);

/// Escribe un píxel indexado en un bitmap contiguo; el bit de más peso es el píxel izquierdo,
/// como exige el orden de bits de los bitplanes Amiga.
void write_pixel(u8* planes, u32 plane_bytes, u16 x, u16 y, u8 index, u8 depth) noexcept {
	const u32 offset = static_cast<u32>(y) * kBytesPerRow + (x >> 3u);
	const u8 mask = static_cast<u8>(0x80u >> (x & 7u));
	for (u8 plane = 0u; plane < depth; ++plane) {
		u8& destination = planes[static_cast<u32>(plane) * plane_bytes + offset];
		if ((index & static_cast<u8>(1u << plane)) != 0u) {
			destination = static_cast<u8>(destination | mask);
		} else {
			destination = static_cast<u8>(destination & static_cast<u8>(~mask));
		}
	}
}

/// Precocina las muestras de paleta y el tablero completo N×N: cada celda de 2×2 usa el patrón
/// A B / B A, y las parejas de luminancia cercana quedan visualmente junto a la diagonal.
template <usize N>
void build_checker_band(u8* planes, u32 plane_bytes, u16 band_height, u16 depth,
			const eng::util::Array<u16, N>& palette,
			const eng::util::Array<u8, N>& order) noexcept {
	__builtin_memset(planes, 0, static_cast<usize>(plane_bytes) * depth);
	const u16 grid_height = static_cast<u16>(2u * N);
	const u16 grid_top = static_cast<u16>((band_height - grid_height) / 2u);
	for (usize row = 0u; row < N; ++row) {
		const u8 row_index = order[row];
		const u16 y = static_cast<u16>(grid_top + 2u * row);
		const u8 swatch = row_index;
		for (u16 dy = 0u; dy < 2u; ++dy) {
			for (u16 x = 0u; x < kSwatchWidth; ++x) {
				write_pixel(planes, plane_bytes, x, static_cast<u16>(y + dy), swatch,
					    static_cast<u8>(depth));
			}
		}
		for (usize column = 0u; column < N; ++column) {
			const u8 column_index = order[column];
			const u16 x = static_cast<u16>(kGridX + 2u * column);
			write_pixel(planes, plane_bytes, x, y, row_index, static_cast<u8>(depth));
			write_pixel(planes, plane_bytes, static_cast<u16>(x + 1u), y, column_index,
				    static_cast<u8>(depth));
			write_pixel(planes, plane_bytes, x, static_cast<u16>(y + 1u), column_index,
				    static_cast<u8>(depth));
			write_pixel(planes, plane_bytes, static_cast<u16>(x + 1u), static_cast<u16>(y + 1u),
				    row_index, static_cast<u8>(depth));
		}
	}
}

/// Convierte el contador de frame a ocho dígitos hexadecimales sin división en el bucle de juego.
void format_frame(u32 frame, char (&text)[12]) noexcept {
	constexpr char kHex[] = "0123456789ABCDEF";
	text[0] = 'F';
	text[1] = 'R';
	text[2] = '=';
	for (u8 digit = 0u; digit < 8u; ++digit) {
		const u8 shift = static_cast<u8>(28u - 4u * digit);
		text[3u + digit] = kHex[(frame >> shift) & 15u];
	}
	text[11] = '\0';
}

/// Construye el contexto de texto acotado a una banda; el blanco de COLOR01 permite que el HUD
/// escriba solo el bitplane 0, sin alterar la imagen de los otros planos.
bool draw_words(eng::playfield::ContiguousPlayfield& target, u16 height, s16 x, s16 y,
		const char* text) {
	eng::playfield::Surface surface {target, {0, 0, kScreenWidth, height}};
	return surface.draw_text_words(x, y, text, kHudInk);
}

/// Configura los rótulos del panel lateral para identificar profundidad, tamaño completo de la
/// matriz y el criterio visual de parejas de brillo cercano/lejos de la diagonal.
bool draw_mode_labels(eng::playfield::ContiguousPlayfield& target, u16 height, s16 x,
		      const char* depth, const char* cells) {
	if (!draw_words(target, height, x, 0, depth) ||
	    !draw_words(target, height, x, 8, cells) ||
	    !draw_words(target, height, x, 16, "DIAG: SIM LIGHT") ||
	    !draw_words(target, height, x, 24, "FAR: BRIGHT GAP")) {
		return false;
	}
	return true;
}

/// Dibuja el rótulo ampliado EHB, donde hay sitio para la versión y para explicar la fase 2×2.
bool draw_ehb_labels(eng::playfield::ContiguousPlayfield& target, u16 height, s16 x) {
	if (!draw_words(target, height, x, 0, "COLOR PAIRS") ||
	    !draw_words(target, height, x, 8, "EHB 64 COLORS") ||
	    !draw_words(target, height, x, 16, "DIAG: SIM LIGHT") ||
	    !draw_words(target, height, x, 24, "FAR: BRIGHT GAP")) {
		return false;
	}
	char version[16] {};
	const u8 version_size = eng::debug::version_label(kVersion, version, static_cast<u8>(sizeof(version)));
	if (version_size == 0u || !draw_words(target, height, x, 40, version) ||
	    !draw_words(target, height, x, 48, "ALL 64X64 PAIRS") ||
	    !draw_words(target, height, x, 56, "2X2 A/B CHECKER")) {
		return false;
	}
	return true;
}

/// Demo Copper de cuantización: los mismos tableros de pareja ocupan cuatro bandas con BPLCON0
/// distinto; cada bitmap se precocina una vez y el Copper conmuta geometría/paleta en sus cortes.
struct PaletteCheckerboardDemo {
	/// Reserva los cuatro bitmaps Chip, precocina sus celdas, construye las tres transiciones
	/// `ModeSwitchZone`, valida la copperlist y toma el control del display.
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// 80 KiB Chip cubren los 52.5 KiB de bitplanes y la copperlist; Slow/Scratch son metadatos.
		if (!backend.configure_memory({80u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008601u); // Falló el perfil de memoria.
			return;
		}
		// Cada bloque queda alineado a 16 bytes para los accesos word-aligned del DMA.
		m_band3 = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBand3Bytes, 16u);
		m_band4 = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBand4Bytes, 16u);
		m_band5 = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBand5Bytes, 16u);
		m_band_ehb = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBandEhbBytes, 16u);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCopperBytes, 16u);
		if (!m_band3.valid() || !m_band4.valid() || !m_band5.valid() ||
		    !m_band_ehb.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008602u); // Faltó una reserva Chip.
			return;
		}

		build_checker_band(m_band3.view.data(), kPlaneBytes3, kBand3Height, 3u, kPalette3, kOrder3);
		build_checker_band(m_band4.view.data(), kPlaneBytes4, kBand4Height, 4u, kPalette4, kOrder4);
		build_checker_band(m_band5.view.data(), kPlaneBytes5, kBand5Height, 5u, kPalette5, kOrder5);
		build_checker_band(m_band_ehb.view.data(), kPlaneBytesEhb, kBandEhbHeight, 6u,
				   kPaletteEhb, kOrderEhb);
		if (!m_pf3.bind(m_band3, kScreenWidth, kBand3Height, 3u, kPlaneBytes3) ||
		    !m_pf4.bind(m_band4, kScreenWidth, kBand4Height, 4u, kPlaneBytes4) ||
		    !m_pf5.bind(m_band5, kScreenWidth, kBand5Height, 5u, kPlaneBytes5) ||
		    !m_pf_ehb.bind(m_band_ehb, kScreenWidth, kBandEhbHeight, 6u, kPlaneBytesEhb)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008603u); // Un lienzo no pudo enlazarse.
			return;
		}

		// El área lateral empieza después de la columna de muestras y la matriz; se alinea a 16 px
		// porque `Surface::draw_text_words` escribe dos glifos por palabra.
		if (!draw_mode_labels(m_pf3, kBand3Height, kLabelX3, "3BPL 8 COLORS", "ALL 8X8 PAIRS") ||
		    !draw_mode_labels(m_pf4, kBand4Height, kLabelX4, "4BPL 16 COLORS", "ALL 16X16 PAIRS") ||
		    !draw_mode_labels(m_pf5, kBand5Height, kLabelX5, "5BPL 32 COLORS", "ALL 32X32 PAIRS") ||
		    !draw_ehb_labels(m_pf_ehb, kBandEhbHeight, kLabelXEhb)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008605u); // El panel de texto quedó fuera del clip.
			return;
		}

		eng::copper::SchedulerT<false> scheduler {m_copper};
		// DIWSTRT/STOP $2c81/$2cc1 definen el viewport lores estándar de 320×256 PAL.
		scheduler.emit_planes_display(
			0x2c81u, 0x2cc1u, kFetchStart, kFetchStop, kBytesPerRow,
			eng::graphics::composition::bplcon0_for(
				eng::graphics::composition::SceneMode::Standard, 3u),
			3u, m_band3.mem_view_chip(), kPlaneBytes3);
		scheduler.emit_palette(eng::PaletteWords {kPalette3.data(), kPalette3.size()});

		bool zones_ok = emit_zone(scheduler, kBand4Top, eng::graphics::composition::SceneMode::Standard,
				  4u, m_band4, kPlaneBytes4,
				  eng::PaletteWords {kPalette4.data(), kPalette4.size()});
		zones_ok = emit_zone(scheduler, kBand5Top, eng::graphics::composition::SceneMode::Standard,
				     5u, m_band5, kPlaneBytes5,
				     eng::PaletteWords {kPalette5.data(), kPalette5.size()}) && zones_ok;
		// OCS/ECS EHB: seis planos con HAM y dual-playfield desactivados; AHRM 3.ª cap. 3
		// §Color Selection in EHB (líneas 2895–2897) y WinUAE-DBG `drawing.cpp:2741`.
		zones_ok = emit_zone(scheduler, kBandEhbTop, eng::graphics::composition::SceneMode::Ehb,
				     6u, m_band_ehb, kPlaneBytesEhb,
				     eng::PaletteWords {kPaletteEhb.data(), 32u}) && zones_ok;
		scheduler.end();
		m_copper_words = scheduler.words_used();
		m_copper_view = scheduler.chip_view();
		if (!zones_ok || !scheduler.ok() || !eng::setup::check_copper_list(scheduler).ok()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008604u); // La lista no pasó el validador Copper.
			return;
		}

		backend.takeover_display(m_copper_view);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, m_copper_words);
	}

	/// La matriz y las paletas son estáticas; el estado variable de depuración es el frame del HUD.
	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}

	/// Actualiza el contador hexadecimal cada ocho frames y deja que el engine sondee READY.
	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		if (!m_ready) { return; }
		const u32 frame = context.frame.frame_index;
		// El contador se actualiza cada 8 frames, tras la sincronía de render, con una palabra
		// de texto por cada dos glifos y solo en el bitplane 0 del panel EHB.
		if ((frame & 7u) == 0u && frame != m_last_hud_frame) {
			char counter[12] {};
			format_frame(frame, counter);
			eng::playfield::Surface surface {
				m_pf_ehb, {0, 0, kScreenWidth, kBandEhbHeight}};
			if (!surface.draw_text_words(kLabelXEhb, kFrameCounterY, counter, kHudInk)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00008606u); // La palabra HUD excedió su panel.
				m_ready = false;
				return;
			}
			m_last_hud_frame = frame;
		}
		eng::debug::probe_when_ready(g_eng_run_status, frame);
	}

private:
	/// Programa el WAIT del inicio de una banda y todos los registros acoplados del modo.
	/// `top` usa coordenadas raster; la ventana visible empieza en `kRasterOrigin` (DIWSTRT 0x2c).
	bool emit_zone(eng::copper::SchedulerT<false>& scheduler, u16 top,
		       eng::graphics::composition::SceneMode mode, u8 planes,
		       const eng::Block<eng::PlaneTag>& bitmap, u32 plane_size,
		       eng::PaletteWords palette) {
		eng::graphics::ModeSwitchZone zone {};
		zone.top = static_cast<u16>(kRasterOrigin + top);
		zone.bplcon0 = eng::graphics::composition::bplcon0_for(mode, planes);
		zone.ddfstrt = kFetchStart;
		zone.ddfstop = kFetchStop;
		zone.planes = planes;
		zone.plane_bytes = plane_size;
		zone.bitplanes = bitmap.mem_view_chip();
		zone.palette = palette;
		zone.palette_colors = mode == eng::graphics::composition::SceneMode::Ehb
			? 32u : static_cast<u8>(palette.size());
		return scheduler.emit_mode_switch_zone(zone);
	}

	eng::Block<eng::PlaneTag> m_band3 {}; ///< Bitmap Chip contiguo de 3 planos para la franja superior.
	eng::Block<eng::PlaneTag> m_band4 {}; ///< Bitmap Chip contiguo de 4 planos para la segunda franja.
	eng::Block<eng::PlaneTag> m_band5 {}; ///< Bitmap Chip contiguo de 5 planos para la tercera franja.
	eng::Block<eng::PlaneTag> m_band_ehb {}; ///< Bitmap Chip contiguo de 6 planos para el tramo EHB.
	eng::Block<eng::CopperTag> m_copper {}; ///< Copperlist Chip estática con los tres cambios de modo.
	eng::ChipView<eng::CopperTag> m_copper_view {}; ///< Lista ya compilada que ejecuta el Copper.
	eng::playfield::ContiguousPlayfield m_pf3 {}; ///< Contexto de texto del bitmap de 3 planos.
	eng::playfield::ContiguousPlayfield m_pf4 {}; ///< Contexto de texto del bitmap de 4 planos.
	eng::playfield::ContiguousPlayfield m_pf5 {}; ///< Contexto de texto del bitmap de 5 planos.
	eng::playfield::ContiguousPlayfield m_pf_ehb {}; ///< Contexto de texto EHB y HUD de un solo plano.
	u16 m_copper_words = 0u; ///< Palabras utilizadas por la lista ya validada.
	u32 m_last_hud_frame = 0xffffffffu; ///< Último frame pintado en hexadecimal en la banda EHB.
	bool m_ready = false; ///< Setup/bitmaps/lista completos y visibles.
};

PaletteCheckerboardDemo g_game {};

} // namespace

int main() {
	// La demo programa un copperlist propio y necesita un backend vivo durante el loop.
	eng::amiga::ensure_boot();
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	eng::Engine engine {backend, g_game};
	// El takeover congela los servicios del SO, así que no queda señal Exec de VBlank para Wait();
	// el engine usa su fallback de sondeo mientras mantiene la copperlist propia (backend.hpp:308–318).
	// 0xFFFF mantiene el loop activo hasta que el runner finaliza la instancia.
	engine.run_frames_polling(0xffffu);
	return 0;
}
