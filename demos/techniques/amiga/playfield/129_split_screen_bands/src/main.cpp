// Demo 129 - split_screen_bands
// ----------------------------------------------------------------------------
// Tutorial (etapa 4 del planner, `ROADMAP_GAME_API.md` §7): **split-screen por bandas** con el
// mismo `RasterLayout` que el DPF, y **dibujo split-aware**: una barra que cruza la línea de corte
// se **reparte** con `eng::scene::for_each_band_part` —la mitad superior va al bitmap de la banda 0
// y la inferior al de la banda 1— sin que el dibujo sepa cuál es cuál.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/129_split_screen_bands --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/129_split_screen_bands --warp

#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/display.hpp>
#include <eng/scene/banded_target.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

namespace copper = eng::copper;

using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kScreenBpr = kWidth / 8u; // 40
constexpr u8 kPlanes = 3u;
constexpr u16 kPitch = 80u; // bytes/fila/plano (320 px + 320 de margen X)
constexpr u32 kFieldBytes = static_cast<u32>(kPitch) * kPlanes * kHeight;
constexpr u16 kSplitLine = 128u;

constexpr eng::Palette32 kPalette {{
	0x000, 0xf00, 0x00f, 0x0f0, 0xff0, 0xfff, 0x0ff, 0xf0f,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0xaaa,
}};

constexpr u16 kCopperWords = 1024u;

struct SplitScreenDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({128u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012901u);
			return;
		}
		m_top = backend.memory_manager().chip().reserve<eng::PlaneTag>(kFieldBytes, 16u);
		m_bottom = backend.memory_manager().chip().reserve<eng::PlaneTag>(kFieldBytes, 16u);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCopperWords, 16u);
		if (!m_top.valid() || !m_bottom.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012902u);
			return;
		}
		// Fondo de cada banda (dos "vistas" distintas: colores sólidos de referencia).
		fill_field(m_top.view.data(), 1u);
		fill_field(m_bottom.view.data(), 2u);

		configure_view(m_v_top, m_top.view.data());
		configure_view(m_v_bottom, m_bottom.view.data());

		// Una banda por vista: la 0 = display completo; la 1 conmuta en `kSplitLine`.
		eng::scene::RasterLayout layout {};
		(void)layout.add(eng::scene::band_from_view(m_v_top, 0u));
		(void)layout.add(eng::scene::band_from_view(m_v_bottom, kSplitLine));
		layout[0].palette = kPalette.words();
		layout[0].palette_colors = 16u;

		const eng::Bytes<eng::CopperTag> slice = m_copper.view;
		copper::SchedulerT<false> sched {eng::Block<eng::CopperTag> {slice, m_copper.kind}};
		if (!layout.materialize(sched)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012903u);
			return;
		}
		sched.end();
		if (!sched.ok()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012903u);
			return;
		}
		backend.takeover_display(sched.data());
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x12900000u);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) {
			return;
		}
		const u32 f = context.frame.frame_index;
		// Barra que barre la pantalla en X (rebota) y cruza la línea de corte.
		const u32 period = 2u * kWidth;
		const u32 px = f % period;
		const u16 x = static_cast<u16>(px < kWidth ? px : period - 1u - px);

		// Borra la barra anterior (columna de 4 px) en cada banda (restaura su color de fondo).
		draw_bar(m_last_x, 1u, kTopBg);
		draw_bar(m_last_x, 2u, kBottomBg);
		draw_bar(m_last_x + 1u, 1u, kTopBg);
		draw_bar(m_last_x + 1u, 2u, kBottomBg);
		// Dibuja la nueva barra (color 5) **repartida por banda** (arriba→banda 0, abajo→banda 1).
		for (u16 i = 0u; i < 2u; ++i) {
			draw_bar(x + i, 1u, 5u);
			draw_bar(x + i, 2u, 5u);
		}
		m_last_x = x;
		g_eng_run_status.detail = 0x12900000u | (x & 0xffffu);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static constexpr u8 kTopBg = 1u;
	static constexpr u8 kBottomBg = 2u;
	static constexpr eng::scene::BandSpan kBands[2] = {
		{0u, kSplitLine, eng::scene::LayerRole::Foreground},
		{kSplitLine, static_cast<eng::u16>(kHeight - kSplitLine), eng::scene::LayerRole::Foreground}};

	static void configure_view(eng::playfield::PlayfieldHardwareView& v, u8* base) {
		v.planes = kPlanes;
		v.bitmap_bytes_per_row = kPitch;
		v.bitmap_height = kHeight;
		v.viewport_w = kWidth;
		v.viewport_h = kHeight;
		v.display_height = kHeight;
		v.plane_bytes = kFieldBytes;
		v.real_base = eng::Address<eng::MemoryKind::Chip>::from_storage(base);
		v.bpl1mod = static_cast<u16>(static_cast<u32>(kPitch) * kPlanes - kScreenBpr);
		v.bpl2mod = v.bpl1mod;
	}

	static void fill_field(u8* data, u8 color) {
		for (u32 i = 0u; i < kFieldBytes; ++i) {
			data[i] = 0u;
		}
		// Pinta las filas del color pedido (3 planos, interleaved por fila).
		for (u16 y = 0u; y < kHeight; ++y) {
			for (u8 p = 0u; p < kPlanes; ++p) {
				if ((color & (1u << p)) == 0u) {
					continue;
				}
				u8* row = data + (static_cast<u32>(y) * kPlanes + p) * kPitch;
				for (u16 b = 0u; b < kPitch; ++b) {
					row[b] = 0xffu;
				}
			}
		}
	}

	/// Escribe la columna `x` (1 px de ancho) del `band`-ésimo bitmap con `color`, repartiendo el
	/// rango de líneas por banda con `for_each_band_part` (draw split-aware).
	void draw_bar(u16 x, u16 band, u8 color) {
		if (band > 2u) {
			return;
		}
		u8* data = band == 1u ? m_top.view.data() : m_bottom.view.data();
		const s32 band_top = band == 1u ? 0 : static_cast<s32>(kSplitLine);
		const eng::Box column {static_cast<s16>(x), 0, 1u, kHeight};
		eng::scene::for_each_band_part(
			column, eng::Span<const eng::scene::BandSpan> {kBands, 2u},
			[&](u16 bi, const eng::Box& part) {
				// El trozo `part` (líneas de pantalla) va a ESTA banda solo si es la suya.
				if (bi + 1u != band) {
					return;
				}
				const u16 y0 = static_cast<u16>(band == 1u ? part.y : (part.y - band_top));
				for (u16 k = 0u; k < part.h; ++k) {
					for (u8 p = 0u; p < kPlanes; ++p) {
						u8* px = data + ((static_cast<u32>(y0 + k) * kPlanes + p) * kPitch) +
							 (x >> 3u);
						const u8 bit = static_cast<u8>(0x80u >> (x & 7u));
						if ((color & (1u << p)) != 0u) {
							*px = static_cast<u8>(*px | bit);
						} else {
							*px = static_cast<u8>(*px & ~bit);
						}
					}
				}
			});
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_top {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bottom {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::playfield::PlayfieldHardwareView m_v_top {};
	eng::playfield::PlayfieldHardwareView m_v_bottom {};
	u16 m_last_x = 0u;
	bool ready = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	SplitScreenDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffu);
	return 0;
}
