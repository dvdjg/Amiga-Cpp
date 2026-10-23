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
//   - el HUD de depuración del host (`HostHud`): nombre + número + versión + frame + fps,
//     con la carga y el número de piezas como línea de telemetría.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/119_proc_world --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/119_proc_world --warp
//
// (El build tarda ~1 min de más: el compilador evalúa la generación completa.)
#include <eng/api/api.hpp>
#include <eng/core/math/minifloat.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
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
// variedad y transiciones); debajo, la banda de los 32 colores (16 px) y una franja negra
// de 16 px que mantiene el viewport histórico. El HUD de depuración vive en el overlay
// del host (`HostHud`), no en los bitplanes.
constexpr u16 kScreenW = 320u;
constexpr u16 kScreenH = 256u;
constexpr u16 kWorldTile = 16u;
constexpr u16 kMapW = 20u;
constexpr u16 kMapH = 14u;
constexpr u16 kPaletteBandY = kMapH * kWorldTile;        // 224
constexpr u16 kVersionStripY = u16(kPaletteBandY + 16u); // 240

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
/// `-DENG_BUILD_NUMBER` y el HUD del host lo muestra: `v<major>.<minor>.<build>`).
constexpr eng::debug::Version kVersion {1u, 0u};

/// Códigos de fallo de la demo 119 (byte alto = demo, bajo = etapa).
enum class Fail : eng::u32 {
	Memory = 0x00011901u,        ///< `configure_memory`
	WorldInvariant = 0x00011902u,///< conectividad del mundo generado
	Framebuffer = 0x00011903u,   ///< `IndexedDisplay::init`
};

/// `detail` de `mark_ready`: identifica la demo 119 en el canal lateral.
constexpr eng::u32 kReadyDetail = 0x00000119u;

struct ProcWorldDemo {
	eng::IndexedDisplay<5u, 1u> m_fb {}; // 5 planos (paleta de 32 colores), buffer único
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
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}
		const u32 t0 = eng::debug::DebugPeripheral::cycle_counter();

		// 1) El mundo ya está generado EN COMPILACIÓN (`kWorld`); en carga solo se
		//    verifica el contrato sobre los datos incrustados (~cientos de comparaciones).
		if (!terr::biome_world_connected(kWorld.layout, kWorld.set)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::WorldInvariant);
			return;
		}
		stage_mark(1u, eng::debug::DebugPeripheral::cycle_counter() - t0);

		// 2) Framebuffer indexado con la paleta del atlas (32 colores RGB444 → 5 planos).
		eng::Palette32 pal {};
		for (u16 i = 0; i < 32u; ++i) pal.color[i] = kWorldAtlas.palette.color[i];
		if (!m_fb.init(backend, kScreenW, kScreenH, pal.words(), 32u)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Framebuffer);
			return;
		}
		compose_world();
		stage_mark(2u, eng::debug::DebugPeripheral::cycle_counter() - t0);

		m_load_cycles = eng::debug::DebugPeripheral::cycle_counter() - t0;
		m_fb.present(); // C2P + publicación: una sola vez (contenido ya generado)
		stage_mark(3u, eng::debug::DebugPeripheral::cycle_counter() - m_load_cycles - t0);
		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, kReadyDetail);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		// El mundo se montó y publicó en `init`; por frame solo se actualiza el HUD del
		// host (nombre/versión/frame/fps) con la telemetría de carga y piezas.
		if (!m_init_ok) {
			return;
		}
		eng::util::TextLine<64> stats;
		stats << "LOAD " << u16((m_load_cycles + 500u) / 1000u) << "KC  PIECES "
		      << world_set().count;
		m_host_hud.draw(backend.debug(), "119 PROC WORLD", kVersion,
				context.frame.frame_index, eng::debug::prof_clock(), stats.c_str());
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
