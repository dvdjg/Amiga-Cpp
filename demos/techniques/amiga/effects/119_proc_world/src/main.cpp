// Demo 119 — PROC WORLD: terreno procedural de `eng::gfx::proc` en el Amiga real.
// ----------------------------------------------------------------------------
// Tutorial: el engine genera un mundo completo — atlas de materiales, piezas de terreno
// con bordes casados y un mapa colocado por reglas (río con meandros + puente, camino,
// castillo con torre y puerta, aldea, cueva, sierra, lago) — y esta demo lo **monta en
// carga** (colocador + composición) y lo **muestra** con el `eng::IndexedDisplay`
// (framebuffer indexado: el engine hace el C2P por Blitter y lo publica).
//
// **Por qué el atlas/set vienen precocinados del host**: la generación de los 66 tiles
// de material cuesta ~3,4 G de ciclos en un 68000 (medido con el periférico de
// depuración: el ruido paga `__mulsi3` en cada hash y una textura de 32×32 ronda 50 M
// de ciclos; ver `VALIDATION.md`). Los assets son deterministas byte a byte
// (`MiniFloat16` es aritmética entera; `HOST-443` los ancla con un golden), así que el
// pipeline host (`src/prebuild.sh` + `tools/gen_assets.cpp`, la MISMA librería del
// engine) cocina una vez el atlas, el catálogo **y las rotaciones de las piezas de
// relleno** (variedad visual: hierba, árboles, rocas… sin repetir textura), y la demo
// solo hace lo que debe ser de carga: **colocar** (`place_world`, que deriva únicamente
// las orientaciones de río/caminos con `add_variant`), **componer** el mapa en el
// framebuffer y **publicarlo** una sola vez. El frame es estático: todo el coste es de
// carga; el bucle solo late con el VBlank.
//
// Qué enseña:
//   - la fachada de generación (`eng::gfx::proc`): atlas → set → colocador por casación;
//   - el patrón del repo para assets de coste alto: cocinar en host + encajar en runtime
//     (igual que la demo 201 con su banco de tiles);
//   - la franja inferior de 16 px con nombre + versión + ciclos de carga (el contador de
//     frames/fps vive en la ventana de depuración de WinUAE / telemetría).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/119_proc_world --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/119_proc_world --warp
#include <eng/api/api.hpp>
#include <eng/core/math/minifloat.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/terrain.hpp>
#include <eng/graphics/proc/terrain_place.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

// Assets cocinados en host por src/prebuild.sh (MISMA librería; determinismo golden).
#include "../../../../../../out/assets/proc-world/proc_world_assets.h"

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

using eng::u8;
using eng::u16;
using eng::u32;
using MF = eng::math::MiniFloat16;
namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;

// Geometría: pantalla 320×256; mundo 10×7 tiles de 32 px = 320×224; debajo, la banda de
// los 32 colores de la paleta (16 px) y la franja de versión (16 px, norma del repo).
constexpr u16 kScreenW = 320u;
constexpr u16 kScreenH = 256u;
constexpr u16 kWorldTile = 32u;
constexpr u16 kMapW = 10u;
constexpr u16 kMapH = 7u;
constexpr u16 kPaletteBandY = kMapH * kWorldTile;        // 224
constexpr u16 kVersionStripY = u16(kPaletteBandY + 16u); // 240

// Contador de frames de la franja: prefijo «F » + 6 dígitos parcheados por planos.
constexpr u16 kFrameLabelX = 224u;
constexpr u16 kFrameDigitsX = u16(kFrameLabelX + 8u);
// Colores de la franja (31 = nieve/luz de la familia «frío»; 0 = transparente/negro).
constexpr u8 kStripFg = 31u;
constexpr u8 kStripBg = 0u;

// Semilla del colocador (la del host: mismo mundo que los dumps de evidencia).
constexpr u32 kMapSeed = 0xBEE5u;

/// Formato de la demo (major.minor; el `build` lo inyecta el toolchain como
/// `-DENG_BUILD_NUMBER` y la franja lo muestra: `v<major>.<minor>.<build>`).
constexpr eng::debug::Version kVersion {1u, 0u};

struct ProcWorldDemo {
	eng::IndexedDisplay<5u, 1u> m_fb {}; // 5 planos (paleta de 32 colores), buffer único
	terr::TerrainLayout<kMapW, kMapH> m_layout {};
	// Contador de frames mostrado en la franja (dígitos ASCII, incremento decimal).
	char m_frames[7] = "000000";
	u32 m_load_cycles = 0u;
	bool m_init_ok = false;

	/// Set de piezas: los buffers pesados (80 KB) viven en BSS como `static` (trivial, sin
	/// `atexit`); el objeto de la demo se queda pequeño para ser un local de `main`.
	static terr::TerrainTileSet<kWorldTile, kWorldTile, 80u>& tileset() {
		static terr::TerrainTileSet<kWorldTile, kWorldTile, 80u> s {};
		return s;
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// **Bajo un SO vivo**: la espera de VBlank debe ser ACTIVA (`set_vblank_sleep(false)`,
		// variante 2 del 212). Con `Wait()` (defecto), el libro de display del SO reescribe
		// `INTENA` y **borra el VERTB** del engine → la tarea queda dormida para siempre
		// (frame congelado a los pocos frames; PC atrapado en el `STOP` de exec).
		// Ver `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`.
		backend.set_vblank_sleep(false);
		// Chip: planos (40 KB) + chunky (80 KB) + copper (16 KB) + margen; el set vive en
		// BSS del ejecutable (no Chip).
		if (!backend.configure_memory({256u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011901u);
			return;
		}
		const u32 t0 = eng::debug::DebugPeripheral::cycle_counter();

		// 1) Encaja los assets precocinados (piezas + especificaciones) en el set.
		load_assets();
		stage_mark(1u, eng::debug::DebugPeripheral::cycle_counter() - t0);
		const u32 t_assets = eng::debug::DebugPeripheral::cycle_counter();

		// 2) Coloca el mundo por casación de bordes (río+puente, camino, castillo,
		//    aldea, cueva...). El colocador deriva las curvas del río con `add_variant`.
		if (!terr::place_world(m_layout, tileset(), kMapSeed) ||
		    !terr::layout_is_connected(m_layout, tileset())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011902u);
			return;
		}
		stage_mark(2u, eng::debug::DebugPeripheral::cycle_counter() - t_assets);

		// 3) Framebuffer indexado con la paleta del atlas (32 colores RGB444 → 5 planos).
		eng::Palette32 pal {};
		for (u16 i = 0; i < 32u; ++i) pal.color[i] = kWorldPalette[i];
		if (!m_fb.init(backend, kScreenW, kScreenH, pal.words(), 32u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011903u);
			return;
		}
		compose_world();
		stage_mark(3u, eng::debug::DebugPeripheral::cycle_counter() - t_assets);

		m_load_cycles = eng::debug::DebugPeripheral::cycle_counter() - t0;
		draw_info_strip();
		m_fb.present(); // C2P + publicación: una sola vez (contenido generado en carga)
		stage_mark(4u, eng::debug::DebugPeripheral::cycle_counter() - m_load_cycles - t0);
		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, 0x00000119u);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {
		// El mundo se montó y publicó en `init`; por frame solo late la telemetría:
		// contador de frames por **parcheo planar** (8×6 px de bitplanes, sin C2P).
		if (!m_init_ok) {
			return;
		}
		// Incremento decimal de 6 dígitos sin división (disciplina 68000: sin `/` por frame).
		// Se recuerda el dígito más a la izquierda que cambió: parchear solo desde ahí
		// (lo normal es 1 celda por frame; el acarreo es raro) mantiene el coste mínimo.
		u8 first_changed = 5u;
		for (u8 i = 5u;; --i) {
			if (m_frames[i] != '9') {
				++m_frames[i];
				first_changed = i; // el más a la izquierda de esta pasada
				break;
			}
			m_frames[i] = '0';
			first_changed = i;
			if (i == 0u) {
				break; // desbordamiento de 999999: se queda a cero
			}
		}
		m_fb.patch_text(u16(kFrameDigitsX + 8u * first_changed), kVersionStripY,
				m_frames + first_changed, kStripFg, kStripBg);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Copia las piezas cocinadas (indexadas + especificaciones) al set de trabajo. La
	/// copia es a BSS (memcpy de 69 KB): el set de la demo es mutable porque el colocador
	/// añade variantes derivadas (`add_variant`).
	static void load_assets() {
		terr::TerrainTileSet<kWorldTile, kWorldTile, 80u>& set = tileset();
		for (u16 i = 0; i < kWorldTileCount; ++i) {
			for (u16 b = 0; b < kWorldTile * kWorldTile; ++b) {
				set.tiles[i].pixels[b] = kWorldTiles[i][b];
			}
			const WorldSpecData& d = kWorldSpecs[i];
			set.specs[i].edges = {terr::Edge(d.n), terr::Edge(d.e), terr::Edge(d.s),
					      terr::Edge(d.w)};
			set.specs[i].features = terr::Feature(d.features);
			set.specs[i].seed = 0u;
		}
		set.count = kWorldTileCount;
	}

	/// Marca de etapa + ciclos en `g_eng_run_status.detail` (`stage<<24 | ciclos>>8`): si el
	/// runner agota su espera o algo falla, el mensaje muestra la última etapa alcanzada.
	/// \param stage número de etapa (1..4). \param cycles ciclos de la etapa.
	static void stage_mark(u8 stage, u32 cycles) {
		g_eng_run_status.detail = (u32(stage) << 24u) | ((cycles >> 8u) & 0x00FFFFFFu);
	}

	/// Compone la pantalla: tiles del mundo arriba, banda de paleta y franja de versión.
	void compose_world() {
		eng::Span<u8> px = m_fb.framebuffer();
		for (u16 cy = 0; cy < kMapH; ++cy) {
			for (u16 cx = 0; cx < kMapW; ++cx) {
				const proc::BitmapBuffer<kWorldTile, kWorldTile>& t =
					tileset().tiles[m_layout.at(cx, cy)];
				for (u16 row = 0; row < kWorldTile; ++row) {
					u8* dst = px.data() + (cy * kWorldTile + row) * kScreenW +
						  cx * kWorldTile;
					for (u16 col = 0; col < kWorldTile; ++col) dst[col] = t.at(col, row);
				}
			}
		}
		// Banda de paleta: 32 muestras de 10 px (la paleta que usa el mundo).
		for (u16 i = 0; i < 32u; ++i) {
			for (u16 y = kPaletteBandY; y < kVersionStripY; ++y) {
				u8* dst = px.data() + y * kScreenW + i * (kScreenW / 32u);
				for (u16 x = 0; x < kScreenW / 32u; ++x) dst[x] = u8(i);
			}
		}
		for (u16 y = kVersionStripY; y < kScreenH; ++y) {
			u8* dst = px.data() + y * kScreenW;
			for (u16 x = 0; x < kScreenW; ++x) dst[x] = 0u;
		}
	}

	/// Dibuja texto 8×8 en el framebuffer chunky (bit `k` de `Font8::row` = columna `k`).
	/// \param x,y esquina superior izquierda. \param text texto ASCII acabado en NUL.
	/// \param fg índice del color del texto. \param bg índice del fondo.
	void draw_text(u16 x, u16 y, const char* text, u8 fg, u8 bg) {
		eng::Span<u8> px = m_fb.framebuffer();
		for (const char* c = text; *c != '\0'; ++c) {
			for (u8 row = 0; row < 8u; ++row) {
				const u8 bits = eng::Font8::row(
					static_cast<eng::u16>(static_cast<unsigned char>(*c)), row);
				u8* dst = px.data() + (y + row) * kScreenW + x;
				for (u8 k = 0; k < 8u; ++k) {
					dst[k] = ((bits >> k) & 1u) != 0u ? fg : bg;
				}
			}
			x = u16(x + 8u);
		}
	}

	/// Franja inferior (16 px): nombre + versión, coste de carga (ciclos) y nº de piezas;
	/// el contador de frames (a la derecha de la versión) se refresca por parcheo planar
	/// en `update`; los fps se leen en la ventana de depuración de WinUAE / telemetría.
	void draw_info_strip() {
		char label[24];
		const u8 vlen = eng::debug::version_label(kVersion, label, sizeof(label));
		char line1[40] = "PROC WORLD ";
		u8 n = 11u;
		for (u8 i = 0u; i < vlen && n + 1u < sizeof(line1); ++i) line1[n++] = label[i];
		line1[n] = '\0';
		draw_text(8u, u16(kVersionStripY + 0u), line1, kStripFg, kStripBg);
		// Contador de frames: «F » + los dígitos (se parchean por planos en cada update).
		draw_text(kFrameLabelX, u16(kVersionStripY + 0u), "F", kStripFg, kStripBg);
		draw_text(kFrameDigitsX, u16(kVersionStripY + 0u), m_frames, kStripFg, kStripBg);

		char line2[40] = "LOAD ";
		n = 5u;
		u32 kc = (m_load_cycles + 500u) / 1000u;
		char digits[10];
		u8 d = 0u;
		do {
			digits[d++] = char('0' + (kc % 10u));
			kc /= 10u;
		} while (kc != 0u && d < 10u);
		while (d > 0u && n + 1u < sizeof(line2)) line2[n++] = digits[--d];
		const char* tail = "KC  PIECES ";
		for (const char* c = tail; *c != '\0' && n + 1u < sizeof(line2); ++c) line2[n++] = *c;
		char num[8];
		u8 nd = 0u;
		u16 count = tileset().count;
		do {
			num[nd++] = char('0' + (count % 10u));
			count = u16(count / 10u);
		} while (count != 0u && nd < 8u);
		while (nd > 0u && n + 1u < sizeof(line2)) line2[n++] = num[--nd];
		line2[n] = '\0';
		draw_text(8u, u16(kVersionStripY + 8u), line2, kStripFg, kStripBg);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	ProcWorldDemo game {}; // pequeño: el set pesado vive en BSS (ver `tileset()`)
	eng::Engine engine {backend, game};
	engine.run_frames(0xffff);

	return 0;
}
