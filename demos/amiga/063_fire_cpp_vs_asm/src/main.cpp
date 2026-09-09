// ============================================================================
// Demo 063: benchmark del fuego — C++ vs asm (mide ciclos de CPU).
// ============================================================================
//
// Compara dos implementaciones del MISMO algoritmo de fuego (promedio de 4
// vecinos de abajo, sobre un buffer u16 de 80x64):
//   - fire_cpp(): bucle C++ (el compilador genera cálculo de offsets y*W y acceso
//     indexado).
//   - fire_asm(): rutina asm en `support/fire_asm.s` (punteros en registros y
//     post-incremento, sin recalcular offsets).
//
// Mide los ciclos de CPU de cada versión con el periférico de depuración
// (base 0xB70000) por CHECKPOINTS (`debugperiph checkpoints`), que es el patrón
// e9k de perfilado por segmentos: slot 11 = fire_cpp×32, slot 12 = fire_asm×32.
// (La lectura del contador de ciclos 0xB7E928 no está disponible en contexto de
// SO en este build, por eso se usan checkpoints, que son escrituras.)
//
// Visualización: dibuja el fuego (usando `fire_cpp` + c2p + scale4x de la demo
// 062) para tener una referencia visual; el interés real es el benchmark.

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/peripheral.hpp>
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

extern "C" {
// Inicializador no-cero: fuerza el símbolo a .data (no .bss) para que el runner
// resuelva su dirección runtime igual que g_eng_run_status (el mapeo de secciones
// del canal lateral no es 1:1 en .bss).
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry = { 0xFFFFFFFFu, 0, 0, 0, 0, {0, 0, 0} };
}

extern "C" void fire_asm(unsigned short* fire, unsigned short width, unsigned short height);

namespace {

namespace drivers = eng::graphics::drivers;

constexpr eng::u16 kFireW = 80;
constexpr eng::u16 kFireH = 64;
constexpr eng::u8  kPlanes = 5;            // 32 colores
constexpr eng::u16 kRowBytes = kFireW / 8;
constexpr eng::u32 kPlaneBytes = kRowBytes * kFireH; // 640
constexpr eng::u8  kScale = 4;
constexpr eng::u32 kDispPlaneBytes = drivers::StaticEhbScene::plane_bytes;
constexpr eng::u16 kDispRowBytes = drivers::StaticEhbScene::bytes_per_row;

template <eng::usize N>
struct Expand4Table {
	eng::u32 data[N] {};
	constexpr Expand4Table() {
		for (eng::usize i = 0; i < N; ++i) {
			eng::u32 r = 0;
			for (int k = 0; k < 8; ++k) {
				if (i & (0x80u >> k)) r |= 0xFu << (28 - 4 * k);
			}
			data[i] = r;
		}
	}
	constexpr eng::u32 operator[](eng::u8 b) const { return data[b]; }
};
constexpr Expand4Table<256> kExpand4 {};

constexpr eng::u16 kFirePalette[32] = {
	0x000, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x710,
	0x820, 0x930, 0xa40, 0xb50, 0xc60, 0xd70, 0xe80, 0xf90,
	0xfa0, 0xfb0, 0xfc0, 0xfd0, 0xfe0, 0xff0, 0xff4, 0xff8,
	0xffc, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff,
};

/// Versión C++ del fuego (promedio de 4 vecinos de abajo), igual que fire_asm.
/// Solo propaga: las dos filas inferiores (chispas) las siembra `seed_fire()`.
void fire_cpp(eng::u16* fire) {
	for (eng::u16 y = 0; y < kFireH - 2u; ++y) {
		const eng::u32 row = static_cast<eng::u32>(y) * kFireW;
		const eng::u32 r1 = row + kFireW;
		const eng::u32 r2 = r1 + kFireW;
		for (eng::u16 x = 1; x < kFireW - 1u; ++x) {
			const eng::u32 v = (
				static_cast<eng::u32>(fire[r2 + x]) +
				static_cast<eng::u32>(fire[r1 + x - 1u]) +
				static_cast<eng::u32>(fire[r1 + x + 1u]) +
				static_cast<eng::u32>(fire[r1 + x])
			) >> 2u;
			fire[row + x] = static_cast<eng::u16>(v);
		}
	}
}

struct FireBenchDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u, 8u * 1024u, 4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006301u);
			return;
		}

		drivers::EhbPalette palette {};
		for (eng::u8 i = 0; i < 32u; ++i) palette.color[i] = kFirePalette[i];
		const drivers::StaticEhbSceneConfig scene_config { &palette, nullptr, 0, 1024 };
		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (!m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006302u);
			return;
		}

		// Buffer de fuego (u16 por pixel, fiel al original) + chunky u8 (entrada del
		// c2p, 1 byte/pixel) + planar temporal (5 planos 80x64).
		const eng::MemoryBlock fire_block = backend.memory().chip.allocate(kFireW * kFireH * 2, 4);
		const eng::MemoryBlock chunky_block = backend.memory().chip.allocate(kFireW * kFireH, 4);
		const eng::MemoryBlock planar_block = backend.memory().chip.allocate(kPlaneBytes * kPlanes, 4);
		if (!fire_block.valid() || !chunky_block.valid() || !planar_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006303u);
			return;
		}
		m_fire = static_cast<eng::u16*>(fire_block.data);
		m_chunky = static_cast<eng::u8*>(chunky_block.data);
		m_planar = static_cast<eng::u8*>(planar_block.data);

		eng::u8* planes = m_scene.bitplanes();
		for (eng::u32 i = 0; i < kDispPlaneBytes; ++i) planes[5u * kDispPlaneBytes + i] = 0u;

		// Pre-desarrolla el fuego (siembra + propagación) para que la captura
		// muestre llamas en el primer frame.
		for (int i = 0; i < 32; ++i) {
			seed_fire();
			fire_cpp(m_fire);
			to_chunky();
			eng::graphics::c2p_1x1_naive(kFireW, kFireH, kPlanes, kPlaneBytes, m_chunky, m_planar);
			scale4x(m_planar, m_scene.bitplanes());
		}

		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, 0x06300000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		// Benchmark en el primer frame (una sola vez), tras READY.
		if (!m_benchmarked) {
			m_benchmarked = true;
			// Benchmark por checkpoints del periférico (escrituras, que sí funcionan
			// en contexto OS; la lectura de 0xB7E928 no está disponible aquí). El
			// profiler de segmentos de `debugperiph checkpoints` atribuye el coste de
			// cada tramo al slot final: slot 11 = fire_cpp, slot 12 = fire_asm.
			eng::debug::DebugPeripheral::checkpoint_description(10, reinterpret_cast<eng::u32>("benchmark_start"));
			eng::debug::DebugPeripheral::checkpoint_description(11, reinterpret_cast<eng::u32>("fire_cpp_32x"));
			eng::debug::DebugPeripheral::checkpoint_description(12, reinterpret_cast<eng::u32>("fire_asm_32x"));
			eng::debug::DebugPeripheral::checkpoint(10);
			for (int i = 0; i < 32; ++i) fire_cpp(m_fire);
			eng::debug::DebugPeripheral::checkpoint(11);
			for (int i = 0; i < 32; ++i) fire_asm(m_fire, kFireW, kFireH);
			eng::debug::DebugPeripheral::checkpoint(12);
		}
		// Siembra las chispas y propaga; el c2p consume la copia chunky u8.
		seed_fire();
		if ((context.frame.frame_index & 1u) == 0u) {
			fire_cpp(m_fire);
		} else {
			fire_asm(m_fire, kFireW, kFireH);
		}
		to_chunky();
		eng::graphics::c2p_1x1_naive(kFireW, kFireH, kPlanes, kPlaneBytes, m_chunky, m_planar);
		scale4x(m_planar, m_scene.bitplanes());
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Siembra las dos filas inferiores (chispas aleatorias 0..31), que no se
	/// muestran y actúan como fuente de calor del fuego.
	void seed_fire() {
		for (eng::u16 x = 0; x < kFireW; ++x) {
			m_fire[(kFireH - 1u) * kFireW + x] = static_cast<eng::u16>(m_rng.next() & 31u);
			m_fire[(kFireH - 2u) * kFireW + x] = static_cast<eng::u16>(m_rng.next() & 31u);
		}
	}

	/// Copia el fuego u16 (0..31) al buffer chunky u8 que espera el c2p
	/// (1 byte/pixel, nibble bajo = índice). El fuego sigue en u16 para que el
	/// benchmark compare contra el `fire_asm` u16 del original sin conversión.
	void to_chunky() {
		for (eng::u32 i = 0; i < static_cast<eng::u32>(kFireW) * kFireH; ++i) {
			m_chunky[i] = static_cast<eng::u8>(m_fire[i]);
		}
	}

	/// Expande planar 80x64 (5 planos) a 320x256 con pixel-doubling 4x.
	void scale4x(const eng::u8* src, eng::u8* dst) {
		for (eng::u8 p = 0; p < kPlanes; ++p) {
			const eng::u8* sp = src + static_cast<eng::u32>(p) * kPlaneBytes;
			eng::u8* dp = dst + static_cast<eng::u32>(p) * kDispPlaneBytes;
			for (eng::u16 y = 0; y < kFireH; ++y) {
				const eng::u8* srow = sp + static_cast<eng::u32>(y) * kRowBytes;
				for (eng::u8 rep = 0; rep < kScale; ++rep) {
					eng::u8* drow = dp + (static_cast<eng::u32>(y) * kScale + rep) * kDispRowBytes;
					for (eng::u16 x = 0; x < kRowBytes; ++x) {
						*reinterpret_cast<eng::u32*>(drow + static_cast<eng::u32>(x) * 4u) =
							kExpand4[srow[x]];
					}
				}
			}
		}
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	bool m_benchmarked = false;
	drivers::StaticEhbScene m_scene {};
	eng::u16* m_fire = nullptr;    // fuego u16 80x64 (0..31)
	eng::u8*  m_chunky = nullptr;  // chunky u8 80x64 (entrada del c2p)
	eng::u8*  m_planar = nullptr;  // planar temporal 5x(80x64)
	eng::Xoroshiro64pp m_rng { 0x12345678u, 0x9abcdef0u };
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	FireBenchDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}