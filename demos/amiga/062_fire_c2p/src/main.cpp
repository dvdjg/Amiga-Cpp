// ============================================================================
// Demo 062: fuego 32 colores + c2p (port de demoscene-repo-orig effects/fire-rgb).
// ============================================================================
//
// Importa el efecto 05-fire-rgb adaptado a la nueva estructura del engine.
// Fiel al original en geometria: el fuego se calcula a 80x64 y se muestra a
// 320x256 (escalado 4x4 pixelado). A diferencia del original (que usa HAM para
// mas colores), esta replica usa 32 colores (5 bitplanes) con una paleta de
// degradado de fuego: visualmente suave y sin el coste ni los artefactos del HAM
// (el HAM del original usaba una LUT `dualtab` para mapear el valor de fuego a un
// patron HAM; 32 colores directos son mas simples y se ven igual de bien).
//
// Flujo por frame:
//   1. generate_fire(): buffer chunky 80x64, indice 0..31 (5 bits).
//   2. c2p_1x1_naive(..., planes=5): chunky -> planar 80x64 (5 planos).
//   3. scale4x(): expande a 320x256 (cada byte x4 horizontal, cada linea x4
//      vertical) escribiendo words nativas; equivalente al escalado del original.

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
constexpr eng::u16 kFireW = 80;
constexpr eng::u16 kFireH = 64;
constexpr eng::u8  kPlanes = 5;           // 32 colores (sin HAM)
constexpr eng::u16 kRowBytes = kFireW / 8; // 10 bytes/fila (planar 80x64)
constexpr eng::u32 kPlaneBytes = kRowBytes * kFireH; // 640 por plano (80x64)
constexpr eng::u8  kScale = 4;            // escalado 4x4

// Display EHB 320x256 (StaticEhbScene).
constexpr eng::u32 kDispPlaneBytes = drivers::StaticEhbScene::plane_bytes; // 10240
constexpr eng::u16 kDispRowBytes = drivers::StaticEhbScene::bytes_per_row; // 40

/// LUT de pixel-doubling 4x horizontal: un byte (8 píxeles, bit 7 = el de más a
/// la izquierda) se expande a un u32 (32 píxeles) repitiendo CADA BIT 4 veces.
/// Esto es lo que el `dualtab` del original resolvía para el color; aquí el
/// escalado horizontal se hace en el plano ya convertido, expandiendo bits.
/// Generada en compile-time (constexpr), sin coste en runtime.
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

/// Paleta de fuego de 32 colores: negro -> rojo -> naranja -> amarillo -> blanco.
void build_fire_palette(drivers::EhbPalette& pal) {
	const eng::u16 fire[32] = {
		0x000, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x710,
		0x820, 0x930, 0xa40, 0xb50, 0xc60, 0xd70, 0xe80, 0xf90,
		0xfa0, 0xfb0, 0xfc0, 0xfd0, 0xfe0, 0xff0, 0xff4, 0xff8,
		0xffc, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff,
	};
	for (eng::u8 i = 0; i < 32u; ++i) pal.color[i] = fire[i];
}

struct FireC2pDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,  // Chip: 6 planos EHB (61440) + chunky 80x64 (5120) + planar 5x640 (3200).
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

		// Buffer de fuego (chunky) + buffer planar temporal del c2p (5 planos 80x64).
		const eng::MemoryBlock fire_block = backend.memory().chip.allocate(kFireW * kFireH, 4);
		const eng::MemoryBlock planar_block = backend.memory().chip.allocate(kPlaneBytes * kPlanes, 4);
		if (!fire_block.valid() || !planar_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006203u);
			return;
		}
		m_fire = static_cast<eng::u8*>(fire_block.data);
		m_planar = static_cast<eng::u8*>(planar_block.data);

		// Planos 5 del EHB (half-brite) a 0, una sola vez; el fuego usa planos 0..4.
		eng::u8* planes = m_scene.bitplanes();
		for (eng::u32 i = 0; i < kDispPlaneBytes; ++i) {
			planes[5u * kDispPlaneBytes + i] = 0u;
		}

		m_scene.takeover(backend);
		// Pre-desarrolla el fuego para que la captura muestre llamas.
		for (int i = 0; i < 32; ++i) {
			generate_fire();
			eng::graphics::c2p_1x1_naive(kFireW, kFireH, kPlanes, kPlaneBytes, m_fire, m_planar);
			scale4x(m_planar, m_scene.bitplanes());
		}
		eng::debug::mark_ready(g_eng_run_status, 0x06200000u | kFireW);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		generate_fire();
		eng::graphics::c2p_1x1_naive(kFireW, kFireH, kPlanes, kPlaneBytes, m_fire, m_planar);
		scale4x(m_planar, m_scene.bitplanes());
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Fuego (índice 0..31): reproduce la algorítmica del original `fire-rgb`.
	///   - Randomiza las 2 filas inferiores (que no se muestran).
	///   - Recorre de ARRIBA a ABAJO y calcula cada píxel como el promedio de 4
	///     vecinos de ABAJO (dos-filas-arriba de su fila de destino, es decir
	///     fire[y+2][x], fire[y+1][x-1], fire[y+1][x+1], fire[y+1][x]) >> 2. Al
	///     recorrer de arriba a abajo, los vecinos de abajo son los del frame
	///     anterior (doble buffer implícito, sin copiar nada).
	void generate_fire() {
		// Dos filas inferiores aleatorias (chispas), no mostradas.
		for (eng::u16 x = 0; x < kFireW; ++x) {
			m_fire[(kFireH - 1u) * kFireW + x] = static_cast<eng::u8>(m_rng.next() & 31u);
			m_fire[(kFireH - 2u) * kFireW + x] = static_cast<eng::u8>(m_rng.next() & 31u);
		}

		// De arriba (y=0) a abajo (y=H-3): lee las filas de abajo (aún no sobrescritas).
		for (eng::u16 y = 0; y < kFireH - 2u; ++y) {
			const eng::u32 row = static_cast<eng::u32>(y) * kFireW;
			const eng::u32 r1 = row + kFireW;      // fila y+1
			const eng::u32 r2 = r1 + kFireW;       // fila y+2
			for (eng::u16 x = 1; x < kFireW - 1u; ++x) {
				const eng::u32 v = (
					static_cast<eng::u32>(m_fire[r2 + x]) +
					static_cast<eng::u32>(m_fire[r1 + x - 1u]) +
					static_cast<eng::u32>(m_fire[r1 + x + 1u]) +
					static_cast<eng::u32>(m_fire[r1 + x])
				) >> 2u;
				m_fire[row + x] = static_cast<eng::u8>(v & 31u);
			}
		}
	}

	/// Expande el planar 80x64 (kPlanes planos) a 320x256 (planos 0..kPlanes-1 del
	/// display EHB). Horizontal: pixel-doubling 4x (cada bit expandido a 4, via
	/// `kExpand4`). Vertical: line-quadrupling (cada linea repetida 4 veces).
	void scale4x(const eng::u8* src, eng::u8* dst_planes) {
		for (eng::u8 p = 0; p < kPlanes; ++p) {
			const eng::u8* sp = src + static_cast<eng::u32>(p) * kPlaneBytes;
			eng::u8* dp = dst_planes + static_cast<eng::u32>(p) * kDispPlaneBytes;
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
	drivers::StaticEhbScene m_scene {};
	eng::u8* m_fire = nullptr;    // chunky 80x64 (0..31)
	eng::u8* m_planar = nullptr;  // planar temporal 5x(80x64)
	eng::Xoroshiro64pp m_rng { 0x12345678u, 0x9abcdef0u };
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	FireC2pDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}