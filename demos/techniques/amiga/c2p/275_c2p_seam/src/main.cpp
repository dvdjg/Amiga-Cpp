// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/c2p/275_c2p_seam --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/c2p/275_c2p_seam
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/c2p/275_c2p_seam --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/c2p/275_c2p_seam

// ============================================================================
// Demo 275: C2P por el seam (`Scene::c2p`) — valida el arreglo de `DrawTarget::c2p`.
// ============================================================================
//
// Mismo efecto que la 061 (rotozoom chunky 4bpp + row_repeat + doble buffer), pero el
// chunky→planar va por el **seam** (`scene.c2p(request, &plan)`), que con el rasterizador
// Blitter y un `FramePlan` debe **encolar** `BlitJobKind::C2P` (13 fases que ejecuta el
// backend) en vez de convertir por CPU.
//
// Bug corregido (2026-09): `Scene::c2p(req, plan)` -> `DrawTarget::c2p(req)` usaba el
// `rasterizer()` del playfield (CPU) e **ignoraba el `plan`**: devolvía `true` pero la
// conversion nunca llegaba al display. Ahora `DrawTarget::c2p` enruta a `kBlitterRaster`
// cuando hay `plan`.
//
// Evidencia: `detail` = 0 (tras la conversion por el seam, el plano escrito coincide con
// la referencia C++ byte a byte). Se compara el resultado del seam contra `c2p_1x1_4` CPU.
//
// Build/run:
//   bash tools/build/build-demo.sh demos/techniques/amiga/c2p/275_c2p_seam --debug
//   WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/techniques/amiga/c2p/275_c2p_seam --warp

#include <eng/api/api.hpp>
#include <eng/core/data/ct_array.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/graphics/c2p.hpp>
#include <eng/graphics/effects/rotozoom.hpp>
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

namespace scene = eng::graphics::composition;

constexpr eng::u16 kChunkyW = 320;
constexpr eng::u16 kChunkyH = 64;
constexpr eng::u8 kRepeat = 4;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kChunkyW / 8u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kChunkyH;

constexpr eng::u16 kColors[16] = {
	0x000, 0x008, 0x00f, 0x08f, 0x0ff, 0x0f8, 0x0f0, 0x8f0,
	0xff0, 0xf80, 0xf00, 0xf08, 0xf0f, 0x80f, 0x408, 0x004,
};

constexpr eng::SineTable<64, 64> kTexSin {};
constexpr eng::u8 texel_at(eng::u32 r, eng::u32 c) {
	const eng::s32 v = kTexSin[static_cast<eng::u8>(r)] + kTexSin[static_cast<eng::u8>(c)] +
			   kTexSin[static_cast<eng::u8>(r + c)];
	return static_cast<eng::u8>((v >> 2) & 15);
}
constexpr eng::ct_array<eng::u8, 64u * 64u> kTexture {[](eng::usize i) -> eng::u8 {
	return texel_at(static_cast<eng::u32>(i) / 64u, static_cast<eng::u32>(i) % 64u);
}};
constexpr eng::SineTable<32768, 256> kZoomSin {};

struct C2pSeamDemo {
	bool init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({192u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027501u);
			return false;
		}

		scene::SceneResources res = scene::planar(320u, 256u, kPlanes);
		res.rows = kChunkyH;
		res.buffers = 2u;
		res.copper_bytes = 8192u;
		if (!scene::compose(m_scene, backend.memory(), res, scene::ocs_a500,
				    scene::display(res),
				    scene::palette(eng::PaletteWords {kColors, 16u}, 0u, 16u),
				    scene::row_repeat(kRepeat, 0x2cu, 0u))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027502u);
			return false;
		}

		m_chunky = backend.memory().chip.allocate_block<eng::ChunkyTag>(kChunkyW * kChunkyH, 4);
		m_ref = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes * kPlanes, 4);
		if (!m_chunky.valid() || !m_ref.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027503u);
			return false;
		}

		// Chunky de referencia.
		const eng::graphics::Rotozoom id {0, 65536, static_cast<eng::s32>(kChunkyW / 2) << 16,
						  static_cast<eng::s32>(kChunkyH / 2) << 16};
		eng::graphics::rotozoom_into<64, 64>(texture(), id, m_chunky.view, kChunkyW, kChunkyH);

		// Referencia CPU directa y, aparte, por el **seam** (Blitter + plan): deben coincidir.
		eng::graphics::c2p_1x1_4(kChunkyW, kChunkyH, kPlaneBytes, m_chunky.view.as_const(),
					 m_ref.view);
		eng::graphics::FramePlan plan {};
		const bool queued = m_scene.c2p(eng::field::C2pRequest {
			.chunky = m_chunky.view.as_const(),
			.planes = m_scene.buffer(0),
			.width = kChunkyW,
			.height = kChunkyH,
			.plane_stride = m_scene.plane_bytes(),
			.plane_count = kPlanes}, &plan);
		if (!queued) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027504u);
			return false;
		}
		backend.execute_frame_plan(plan);

		eng::u32 diffs = 0;
		for (eng::u32 p = 0; p < kPlanes; ++p) {
			for (eng::u32 i = 0; i < kPlaneBytes; ++i) {
				if (m_ref.view.data()[p * kPlaneBytes + i] !=
				    m_scene.buffer(0)[p * m_scene.plane_bytes() + i]) {
					++diffs;
				}
			}
		}

		m_rot = eng::graphics::Rotozoom {0, 65536, 0, 0};
		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, diffs);
		return true;
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		m_angle = static_cast<eng::u16>((m_angle + 2u) & 0xffu);
		m_phase = static_cast<eng::u16>((m_phase + 3u) & 0xffu);
		m_rot.angle = m_angle;
		m_rot.zoom = 98304 + kZoomSin[static_cast<eng::u8>(m_phase)];
		m_rot.offset_x += 12288;
		m_rot.offset_y += 7168;

		eng::graphics::rotozoom_into<64, 64>(texture(), m_rot, m_chunky.view, kChunkyW, kChunkyH);
		eng::graphics::FramePlan plan {};
		m_scene.c2p(eng::field::C2pRequest {.chunky = m_chunky.view.as_const(),
						    .planes = m_scene.back(),
						    .width = kChunkyW,
						    .height = kChunkyH,
						    .plane_stride = m_scene.plane_bytes(),
						    .plane_count = kPlanes}, &plan);
		backend.execute_frame_plan(plan);
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		m_scene.commit();
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static eng::IndexedTexture texture() {
		return eng::IndexedTexture {kTexture.data(), kTexture.size()};
	}

	scene::Scene m_scene {};
	eng::Block<eng::ChunkyTag> m_chunky {};
	eng::Block<eng::PlaneTag> m_ref {};
	eng::graphics::Rotozoom m_rot {};
	eng::u16 m_angle = 0;
	eng::u16 m_phase = 0;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	C2pSeamDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
