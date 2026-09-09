// ============================================================================
// Demo 062: fuego 4bpl + c2p (port de demoscene-repo-orig effects/fire-rgb).
// ============================================================================
//
// Importa el efecto 05-fire-rgb adaptado a la nueva estructura del engine.
// FIEL AL ORIGINAL: el fuego se calcula a 80x64 (WIDTH=80, HEIGHT=64 en el
// original) y se muestra a 320x256, el doble de resolución en cada eje (escalado
// 4x4). El original consigue el escalado con fetch ancho + line-quadrupling por
// BPLMOD; esta demo lo reproduce con un `scale4x` por CPU equivalente (replica
// cada pixel 4x en horizontal y cada linea 4x en vertical), que produce la MISMA
// imagen pixelada.
//
// El buffer chunky (80x64) es el propio buffer de fuego (1 byte/pixel, indice
// 0..15); `c2p_1x1_4` lo convierte a 4 planos de 80x64 y `scale4x` lo expande a
// los 4 planos de 320x256 del StaticEhbScene (planos 5/6 a 0, sin half-brite).

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/c2p.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

namespace drivers = eng::graphics::drivers;

// Geometría del ORIGINAL (fire-rgb.c): WIDTH=80, HEIGHT=64, escalado 4x4 a 320x256.
constexpr eng::u16 kFireW = 80;   // ancho del fuego (multiplo de 16 para el c2p)
constexpr eng::u16 kFireH = 64;   // alto del fuego
constexpr eng::u32 kChunkyBytes = kFireW * kFireH;            // 5120
constexpr eng::u16 kPlanarRowBytes = kFireW / 8;              // 10 bytes/fila
constexpr eng::u32 kPlanarPlaneBytes = kPlanarRowBytes * kFireH; // 640 por plano
constexpr eng::u8  kScale = 4;    // factor de escalado 4x4 (como el original)

// Constantes del display EHB (320x256).
constexpr eng::u32 kDispPlaneBytes = drivers::StaticEhbScene::plane_bytes; // 10240
constexpr eng::u16 kDispRowBytes = drivers::StaticEhbScene::bytes_per_row; // 40

/// Paleta de fuego (indices 0..15): negro -> rojo -> naranja -> amarillo -> blanco.
void build_fire_palette(drivers::EhbPalette& pal) {
	const eng::u16 fire[16] = {
		0x000, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x710,
		0x820, 0xa30, 0xc40, 0xd50, 0xe60, 0xf70, 0xfc8, 0xfff,
	};
	for (eng::u8 i = 0; i < 16u; ++i) pal.color[i] = fire[i];
}

struct FireC2pDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,  // Chip: 6 planos EHB (61440) + chunky 80x64 (5120) + planar (2560).
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006201u);
			return;
		}

		drivers::EhbPalette palette {};
		build_fire_palette(palette);
		const drivers::StaticEhbSceneConfig scene_config { &palette, nullptr, 0, 1024 };
		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (!m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006202u);
			return;
		}

		// Buffer de fuego (chunky) + buffer planar temporal del c2p (80x64).
		const eng::MemoryBlock fire_block = backend.memory().chip.allocate(kChunkyBytes, 4);
		const eng::MemoryBlock planar_block = backend.memory().chip.allocate(kPlanarPlaneBytes * 4, 4);
		if (!fire_block.valid() || !planar_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006203u);
			return;
		}
		m_fire = static_cast<eng::u8*>(fire_block.data);
		m_planar = static_cast<eng::u8*>(planar_block.data);

		// Planos 5/6 del EHB a 0 (sin half-brite), una sola vez.
		eng::u8* planes = m_scene.bitplanes();
		for (eng::u32 i = 0; i < kDispPlaneBytes; ++i) {
			planes[4u * kDispPlaneBytes + i] = 0u;
			planes[5u * kDispPlaneBytes + i] = 0u;
		}

		m_scene.takeover(backend);
		// Pre-desarrolla el fuego (varias iteraciones) para que la captura muestre
		// llamas; el algoritmo del fuego es el cuello (no el c2p), pero a 80x64 es
		// rapido (~35 ms/iteracion).
		for (int i = 0; i < 32; ++i) {
			generate_fire();
			eng::graphics::c2p_1x1_4(kFireW, kFireH, kPlanarPlaneBytes, m_fire, m_planar);
			scale4x(m_planar, m_scene.bitplanes());
		}
		eng::debug::mark_ready(g_eng_run_status, 0x06200000u | kFireW);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		generate_fire();
		eng::graphics::c2p_1x1_4(kFireW, kFireH, kPlanarPlaneBytes, m_fire, m_planar);
		scale4x(m_planar, m_scene.bitplanes());
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Fuego: randomiza la fila inferior y promedia vecinos de abajo a arriba.
	/// El buffer `m_fire` es el propio chunky (0..15), sin copias intermedias.
	/// Fórmula clásica in-place (recorre de abajo hacia arriba, así la fila de
	/// abajo ya está calculada; el propio pixel aún no se tocó en esta pasada).
	void generate_fire() {
		const eng::u32 last = static_cast<eng::u32>(kFireH - 1u) * kFireW;
		for (eng::u16 x = 0; x < kFireW; ++x) {
			m_fire[last + x] = static_cast<eng::u8>(m_rng.next() & 15u);
		}

		for (eng::u16 y = kFireH - 2u; ; --y) {
			const eng::u32 row = static_cast<eng::u32>(y) * kFireW;
			const eng::u32 below = row + kFireW;
			for (eng::u16 x = 1; x < kFireW - 1u; ++x) {
				const eng::u32 v = (
					static_cast<eng::u32>(m_fire[below + x - 1u]) +
					static_cast<eng::u32>(m_fire[below + x]) +
					static_cast<eng::u32>(m_fire[below + x + 1u]) +
					static_cast<eng::u32>(m_fire[row + x])
				) >> 2u;
				m_fire[row + x] = static_cast<eng::u8>(v);
			}
			if (y == 0u) break;
		}
	}

	/// Expande el planar 80x64 (4 planos) a 320x256 (4 planos del display),
	/// replicando cada byte 4x en horizontal y cada linea 4x en vertical.
	/// Equivalente visual del line-quadrupling del original (BPLMOD + fetch ancho).
	void scale4x(const eng::u8* src, eng::u8* dst_planes) {
		for (eng::u8 p = 0; p < 4u; ++p) {
			const eng::u8* sp = src + static_cast<eng::u32>(p) * kPlanarPlaneBytes;
			eng::u8* dp = dst_planes + static_cast<eng::u32>(p) * kDispPlaneBytes;
			for (eng::u16 y = 0; y < kFireH; ++y) {
				const eng::u8* srow = sp + static_cast<eng::u32>(y) * kPlanarRowBytes;
				for (eng::u8 rep = 0; rep < kScale; ++rep) {
					eng::u8* drow = dp + (static_cast<eng::u32>(y) * kScale + rep) * kDispRowBytes;
					for (eng::u16 x = 0; x < kPlanarRowBytes; ++x) {
						const eng::u8 b = srow[x];
						drow[x * 4u + 0u] = b;
						drow[x * 4u + 1u] = b;
						drow[x * 4u + 2u] = b;
						drow[x * 4u + 3u] = b;
					}
				}
			}
		}
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	drivers::StaticEhbScene m_scene {};
	eng::u8* m_fire = nullptr;    // chunky 80x64 (0..15)
	eng::u8* m_planar = nullptr;  // planar temporal 4x(80x64)
	eng::Xoroshiro64pp m_rng { 0x12345678u, 0x9abcdef0u };
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	FireC2pDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}