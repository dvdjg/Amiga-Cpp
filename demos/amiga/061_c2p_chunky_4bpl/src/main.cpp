// ============================================================================
// Demo 061: c2p_1x1_4 (chunky 4bpp -> planar) sobre StaticEhbScene.
// ============================================================================
//
// Valida en hardware la primera pieza portada de la Oleada 1 de libgfx:
// `eng::graphics::c2p_1x1_4`, que convierte un buffer chunky (1 byte/pixel,
// 4 bits de color) al formato planar de 4 bitplanes que lee el DMA de Agnus.
//
// La demo genera un buffer chunky con un patron determinista (cuadros + gradiente
// en indices 0..15), lo convierte en `init()` y publica los 4 planos resultantes al
// primer plano del `StaticEhbScene` (6 planos EHB; los planos 5/6 quedan a 0 para
// no activar half-brite). La paleta base da RGB444 a los indices 0..15.
//
// Build/run (Windows nativo):
//   bash tools/build/build-demo.sh demos/amiga/061_c2p_chunky_4bpl --clean
//   <Node> dist/tools/run/run-demo.js demos/amiga/061_c2p_chunky_4bpl --warp
//
// Si el c2p falla, la imagen sale corrupta (planos descolocados/no-laterales);
// si funciona, se ve el patron chunky exacto en la zona superior-izquierda.

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

constexpr eng::u16 kScreenW = drivers::StaticEhbScene::width;          // 320
constexpr eng::u16 kScreenH = drivers::StaticEhbScene::height;         // 256
constexpr eng::u16 kBytesPerRow = drivers::StaticEhbScene::bytes_per_row;  // 40
constexpr eng::u8 kPlanes = drivers::StaticEhbScene::plane_count;      // 6
constexpr eng::u32 kPlaneBytes = drivers::StaticEhbScene::plane_bytes; // 40*256

// Tamano del area chunky que convertimos: multiplo de 16 en X (exigencia del c2p).
constexpr eng::u16 kChunkyW = 160;
constexpr eng::u16 kChunkyH = 128;

/// Paleta RGB444 para los indices 0..15 que produce el chunky. Cada índice i usa
/// i*0x111 (rampa de grises) para que el pixel-assert pueda verificar el c2p
/// comparando el valor del índice contra el color esperado.
void build_palette(drivers::EhbPalette& pal) {
	for (eng::u8 i = 0; i < 16u; ++i) {
		const eng::u16 g = static_cast<eng::u16>(i * 0x111u);
		pal.color[i] = g;
	}
}

/// Rellena el buffer chunky con un patron determinista:
/// - filas superiores: banda diagonal de 4 cols de ancho, índice = fila, para ver
///   que cada fila de chunky aterriza en su fila planar correcta;
/// - filas inferiores: cuadricula de 16x16, índice = (col+fil)&15, para validar
///   la conversion por píxel.
void fill_chunky(eng::u8* chunky, eng::u16 w, eng::u16 h) {
	for (eng::u16 y = 0; y < h; ++y) {
		for (eng::u16 x = 0; x < w; ++x) {
			eng::u8 index = 0;
			if (y < 64u) {
				// Banda horizontal; cada fila un índice distinto (0..15 por 4 cols).
				index = static_cast<eng::u8>((y / 4u) % 16u);
			} else {
				// Cuadricula 16x16 por bloques de 4 px.
				const eng::u16 cx = x / 4u;
				const eng::u16 cy = (y - 64u) / 4u;
				index = static_cast<eng::u8>((cx + cy) & 15u);
			}
			// El nibble bajo lleva el índice de color.
			chunky[static_cast<eng::u32>(y) * w + x] = index & 0xfu;
		}
	}
}

struct C2pDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,  // Chip: bitplanes EHB + copperlist + buffer chunky temporal.
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006101u);
			return;
		}

		drivers::EhbPalette palette {};
		build_palette(palette);
		const drivers::StaticEhbSceneConfig scene_config {
			&palette, nullptr, 0, 1024,
		};
		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (!m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006102u);
			return;
		}

		// Reserva del buffer chunky (temporal, en Chip RAM tambien para no tocar DMA).
		const eng::MemoryBlock chunky_block =
			backend.memory().chip.allocate(kChunkyW * kChunkyH, 4);
		if (!chunky_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006103u);
			return;
		}
		eng::u8* chunky = static_cast<eng::u8*>(chunky_block.data);
		fill_chunky(chunky, kChunkyW, kChunkyH);

		// Publica el resultado del c2p a los 4 primeros planos del escenario.
		// `bplsize` = bytes de un plano completo (plano p a planes + p*kPlaneBytes).
		eng::graphics::c2p_1x1_4(
			kChunkyW, kChunkyH,
			kPlaneBytes,
			chunky,
			m_scene.bitplanes()
		);

		// Plano 5/6: a cero, para no activar half-brite sobre los indices base.
		eng::u8* planes = m_scene.bitplanes();
		for (eng::u32 i = 0; i < kPlaneBytes; ++i) {
			planes[4u * kPlaneBytes + i] = 0u;
			planes[5u * kPlaneBytes + i] = 0u;
		}

		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, 0x06100000u | kChunkyW);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	drivers::StaticEhbScene m_scene {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	C2pDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}