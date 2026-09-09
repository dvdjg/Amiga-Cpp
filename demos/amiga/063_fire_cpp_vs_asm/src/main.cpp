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
// (`DebugPeripheral::cycle_counter`, base 0xB70000) y publica el resultado en
// contadores (visibles con `debugperiph counters` desde el host) y en
// `g_eng_run_status.detail`.
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
void fire_cpp(eng::u16* fire) {
	for (eng::u16 x = 0; x < kFireW; ++x) {
		fire[(kFireH - 1u) * kFireW + x] = 0u;
		fire[(kFireH - 2u) * kFireW + x] = 0u;
	}
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

		// Buffer de fuego (u16 por pixel) + planar temporal.
		const eng::MemoryBlock fire_block = backend.memory().chip.allocate(kFireW * kFireH * 2, 4);
		const eng::MemoryBlock planar_block = backend.memory().chip.allocate(kPlaneBytes * kPlanes, 4);
		if (!fire_block.valid() || !planar_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006303u);
			return;
		}
		m_fire = static_cast<eng::u16*>(fire_block.data);
		m_planar = static_cast<eng::u8*>(planar_block.data);

		eng::u8* planes = m_scene.bitplanes();
		for (eng::u32 i = 0; i < kDispPlaneBytes; ++i) planes[5u * kDispPlaneBytes + i] = 0u;

		m_scene.takeover(backend);
		// READY inmediato; el benchmark se ejecuta en el primer update.
		eng::debug::mark_ready(g_eng_run_status, 0x06300000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		// Benchmark en el primer frame (una sola vez), tras READY.
		if (!m_benchmarked) {
			m_benchmarked = true;
			const eng::u32 t0 = eng::debug::DebugPeripheral::cycle_counter();
			for (int i = 0; i < 32; ++i) fire_cpp(m_fire);
			const eng::u32 t1 = eng::debug::DebugPeripheral::cycle_counter();
			for (int i = 0; i < 32; ++i) fire_asm(m_fire, kFireW, kFireH);
			const eng::u32 t2 = eng::debug::DebugPeripheral::cycle_counter();
			m_cpp_cycles = t1 - t0;
			m_asm_cycles = t2 - t1;
			eng::debug::DebugPeripheral::counter_value(0, m_cpp_cycles);
			eng::debug::DebugPeripheral::counter_value(1, m_asm_cycles);
		}
		// Dibuja el fuego alternando cpp/asm (referencia visual en vivo).
		if ((context.frame.frame_index & 1u) == 0u) {
			fire_cpp(m_fire);
		} else {
			fire_asm(m_fire, kFireW, kFireH);
		}
		eng::graphics::c2p_1x1_naive(kFireW, kFireH, kPlanes, kPlaneBytes,
		                              reinterpret_cast<const void*>(m_fire), m_planar);
		scale4x(m_planar, m_scene.bitplanes());
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
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
	eng::u16* m_fire = nullptr;
	eng::u8* m_planar = nullptr;
	eng::u32 m_cpp_cycles = 0;
	eng::u32 m_asm_cycles = 0;
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