// Demo 119 — PROC WORLD: mundo de biomas procedural de `eng::gfx::proc` en el Amiga real.
// ----------------------------------------------------------------------------
// Tutorial: el engine genera un mundo completo **en carga** con el sistema de biomas
// (`terrain_biome.hpp`) — campos de río/lago/arena/tierra/sierra con **ruido entero**
// (`mulu.w`), gramática de adyacencia (el agua no toca tierra/roca sin arena), camino en
// L con puente hasta la puerta del castillo y **set por máscaras**: cada celda pide la
// pieza de sus cuatro lados (material del vecino) y el set la pinta/memoiza al momento
// (canónica por rotación) — y lo **muestra** con `eng::IndexedDisplay<5>` (32 colores,
// C2P por el engine en carga).
//
// **Generación EN COMPILACIÓN**: el atlas (incluido el ruido escalar de sus texturas) y
// el mundo completo se evalúan con el **sistema `constexpr` de C++23 al compilar la
// demo** — `make_world_atlas()`/`make_world()` corren en el compilador (los límites de
// evaluación se suben en `build.args`: `-fconstexpr-ops-limit`), y el binario m68k incrusta
// el resultado ya cocinado (rodata). En el 68000 no se genera NADA: la carga solo compone
// el chunky y publica por C2P. (La generación en runtime sigue disponible — es la que
// usa el host en los tests de visión, `HOST-445` — y en el 68000 costaba ~100 M de ciclos.)
//
// La coherencia es **por construcción**: los lados de cada pieza son el material del
// vecino, la gramática repara los contactos prohibidos y el mundo se verifica con
// `biome_world_connected` (en compilación vía `static_assert` del test host y en carga
// con una comprobación de bajo coste sobre los datos incrustados).
//
// Qué enseña:
//   - la fachada de generación (`eng::gfx::proc`): atlas → campos → máscaras → mundo,
//     toda ella evaluable en compilación (`constexpr`) y en runtime (misma fuente);
//   - el flujo «metaprogramación C++23»: cero coste de generación en el 68000 y assets
//     deterministas byte a byte (`HOST-443`/`HOST-445` anclan el mismo resultado);
//   - la franja inferior de 16 px con nombre + versión + ciclos de carga + contador de
//     frames vivo por parcheo planar.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/119_proc_world --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/119_proc_world --warp
//
// (El build tarda ~1 min de más: el compilador evalúa la generación completa.)
#include <eng/api/api.hpp>
#include <eng/core/math/minifloat.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/terrain_biome.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

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

using eng::u8;
using eng::u16;
using eng::u32;
using MF = eng::math::MiniFloat16;
namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;

// Geometría: pantalla 320×256; mundo 20×14 tiles de 16 px = 320×224 (más celdas = más
// variedad y transiciones); debajo, la banda de los 32 colores (16 px) y la franja de
// versión (16 px, norma del repo).
constexpr u16 kScreenW = 320u;
constexpr u16 kScreenH = 256u;
constexpr u16 kWorldTile = 16u;
constexpr u16 kMapW = 20u;
constexpr u16 kMapH = 14u;
constexpr u16 kPaletteBandY = kMapH * kWorldTile;        // 224
constexpr u16 kVersionStripY = u16(kPaletteBandY + 16u); // 240

// Contador de frames de la franja: prefijo «F » + 6 dígitos parcheados por planos.
constexpr u16 kFrameLabelX = 224u;
constexpr u16 kFrameDigitsX = u16(kFrameLabelX + 8u);
// Colores de la franja (31 = nieve/luz de la familia «frío»; 0 = transparente/negro).
constexpr u8 kStripFg = 31u;
constexpr u8 kStripBg = 0u;

// Semillas del host (mismo mundo que los dumps de evidencia: `HOST-445`/`v4`).
constexpr u32 kAtlasSeed = 0x7E5A1u;
constexpr u32 kMapSeed = 0xBEE5u;

/// **Atlas generado en compilación**: el sistema `constexpr` evalúa las 66 texturas de
/// 16×16 (ruido escalar incluido) durante el build; el resultado queda incrustado.
[[nodiscard]] constexpr proc::TextureAtlas<kWorldTile, kWorldTile, MF> make_world_atlas() {
	proc::TextureAtlas<kWorldTile, kWorldTile, MF> a {};
	a.generate(kAtlasSeed);
	return a;
}
inline constexpr proc::TextureAtlas<kWorldTile, kWorldTile, MF> kWorldAtlas = make_world_atlas();

/// Datos del mundo incrustados (layout + set de piezas), generados en compilación.
struct WorldData {
	terr::TerrainLayout<kMapW, kMapH> layout {};
	terr::BiomeTileSet<kWorldTile, kWorldTile, 224u> set {};
};

/// **Mundo generado en compilación** (`generate_biome_world` es `constexpr`): campos de
/// bioma, gramática, máscaras y pintado de las piezas, todo evaluado por el compilador.
[[nodiscard]] constexpr WorldData make_world() {
	WorldData w {};
	(void)terr::generate_biome_world<kMapW, kMapH>(w.layout, w.set, kWorldAtlas, kMapSeed);
	return w;
}
inline constexpr WorldData kWorld = make_world();

/// Formato de la demo (major.minor; el `build` lo inyecta el toolchain como
/// `-DENG_BUILD_NUMBER` y la franja lo muestra: `v<major>.<minor>.<build>`).
constexpr eng::debug::Version kVersion {1u, 0u};

struct ProcWorldDemo {
	eng::IndexedDisplay<5u, 1u> m_fb {}; // 5 planos (paleta de 32 colores), buffer único
	// Contador de frames mostrado en la franja (dígitos ASCII, incremento decimal).
	char m_frames[7] = "000000";
	u32 m_load_cycles = 0u;
	bool m_init_ok = false;
	eng::debug::HostHud m_host_hud {}; ///< HUD del host (nombre/número/versión/frame/fps)

	/// Deja fuera de `main` los objetos grandes: el atlas y el mundo viven en rodata
	/// (`kWorldAtlas`/`kWorld`), así que la demo en sí es pequeña.
	[[nodiscard]] static const terr::BiomeTileSet<kWorldTile, kWorldTile, 224u>& world_set() {
		return kWorld.set;
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// **Bajo un SO vivo**: el bucle usa el **modo de sondeo** validado del doc 212
		// (`run_frames_polling`: sin latido por IRQ — no se secuestra el vector de nivel 3
		// ni se depende de `Wait`) y **toma de control del sistema** (patrón canónico de la
		// 066: `Forbid` + `LoadView(NULL)` + `OwnBlitter`) para que el libro de display no
		// toque `COP1LC`/`INTENA`. Las vías de IRQ bajo SO vivo (vector propio con espera
		// activa, y variante 3 con `AddIntServer`) se probaron y **también mueren** por la
		// causa raíz abierta del 212 (ver el doc). Vuelta al SO: `backend.restore_system()`.
		(void)backend.takeover_system();
		// Chip: planos (40 KB) + chunky (80 KB) + copper (16 KB) + margen; el set vive en
		// BSS del ejecutable (no Chip).
		if (!backend.configure_memory({256u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011901u);
			return;
		}
		const u32 t0 = eng::debug::DebugPeripheral::cycle_counter();

		// 1) El mundo ya está generado EN COMPILACIÓN (`kWorld`); en carga solo se
		//    verifica el contrato sobre los datos incrustados (~cientos de comparaciones).
		if (!terr::biome_world_connected(kWorld.layout, kWorld.set)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011902u);
			return;
		}
		stage_mark(1u, eng::debug::DebugPeripheral::cycle_counter() - t0);

		// 2) Framebuffer indexado con la paleta del atlas (32 colores RGB444 → 5 planos).
		eng::Palette32 pal {};
		for (u16 i = 0; i < 32u; ++i) pal.color[i] = kWorldAtlas.palette.color[i];
		if (!m_fb.init(backend, kScreenW, kScreenH, pal.words(), 32u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011903u);
			return;
		}
		compose_world();
		stage_mark(2u, eng::debug::DebugPeripheral::cycle_counter() - t0);

		m_load_cycles = eng::debug::DebugPeripheral::cycle_counter() - t0;
		draw_info_strip();
		m_fb.present(); // C2P + publicación: una sola vez (contenido ya generado)
		stage_mark(3u, eng::debug::DebugPeripheral::cycle_counter() - m_load_cycles - t0);
		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, 0x00000119u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
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
		m_host_hud.draw(backend.debug(), "119 PROC WORLD", kVersion,
				context.frame.frame_index, eng::debug::prof_clock());
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
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
					world_set().tiles[kWorld.layout.at(cx, cy)];
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
		u16 count = world_set().count;
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
	ProcWorldDemo game {}; // el mundo generado en compilación vive en rodata (`kWorld`)
	eng::Engine engine {backend, game};
#if defined(K119_PROBE_IRQ)
	// Sonda del fallo `a5=0` (doc 212; scaffolding de diagnóstico, sin define no existe):
	// latido por IRQ con espera activa (variante 2) — reproduce la tormenta de vectores a
	// los pocos frames y permite capturarla con watchpoints desde GDB.
	backend.set_vblank_sleep(false);
	engine.run_frames(0xffff);
#else
	// **Bucle por SONDO** (`run_frames_polling`): bajo un SO vivo es el modo estable del
	// doc 212 — sondea `VPOSR` en `wait_vblank` sin depender de IRQs ni de `Wait` (el
	// coste es CPU ocupada; a cambio, la tormenta de vectores del `a5=0` no aplica).
	engine.run_frames_polling(0xffff);
#endif

	return 0;
}
