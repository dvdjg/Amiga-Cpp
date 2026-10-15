// ============================================================================
// 223 SPRITE HUD — HUD de texto por SPRITES con armado único (banda inferior).
// ============================================================================
//
// **HUD óptimo** de la norma de versión: el texto (nombre + versión + fps) se dibuja con
// **sprites** (canales DMA), NO con escrituras al bitmap. La copperlist queda **estática**
// tras el setup: el coste por frame del HUD es **CERO** (el DMA de sprites la ejecuta sola;
// los bitplanes solo llevan un fondo pintado una vez).
//
// Mecánica (verificada contra WinUAE, `winuae/sprite-dma.md` §Armado de un canal):
//  - cada canal se **arma una vez** por registro (POS+CTL+**PT** → su tabla de DATA; patrón
//    `arm_object`/053). El DMA hace fetch de 2 palabras por línea y **avanza el walk** por
//    la tabla: la DATA del HUD vive ahí, porque el registro `SPRxDATA` lo pisa el fetch;
//  - el **secuenciador de sprites auto-arma** el canal al llegar a `VSTART`
//    (`custom.cpp:10088-10090`), así que basta con dejar los registros escritos antes de la
//    banda (el armado usa `wait_line_safe`, que maneja vpos ≥ 256 con el par de overflow de
//    `wait_line_pal`); la **reposición** POS-only del rearmado por línea solo llega a
//    vpos ≤ 255 (`vline: u8`), así que la banda inferior usa **armado único**
//    (`per_line_rearm=false`), sin rearmado por línea;
//  - cada texto usa canales propios (3+3+2 = 8, sin reutilización dentro de la banda);
//  - el primer canal empieza en x de pantalla 32 (> fetch de la línea, DDFSTRT/2 ≈ 28 px);
//    la X de la API va en **HSTART absoluto** (pantalla = HSTART − 0x81, `drawing.cpp:2706`).
//
// El único valor dinámico es la estimación de fps: **≤1 actualización por segundo** y solo
// cuando el número cambia, reconstruyendo la lista (con el HUD en la franja inferior la
// reconstrucción tras el VBlank termina mucho antes de que el haz llegue allí).
//
// Coherencia: `SpriteLineLayer::configure` valida `display_planes` con
// `setup::check_sprite_line_data` (≤5 planos: con 6 no hay hueco de DMA para el parcheo de
// DATA a media línea; `custom.cpp:10111`/`10130-10131`).

#include <eng/api/api.hpp>
#include <eng/core/data/utf8.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/font5x7.hpp>
#include <eng/graphics/sprite_limits.hpp>
#include <eng/graphics/sprite_line_layer.hpp>
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

namespace amiga = eng::amiga;
namespace copper = eng::copper;

using eng::u8;
using eng::u16;
using eng::u32;
using eng::s16;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kBytesPerRow = kWidth / 8u; // 40
constexpr u8 kPlanes = 3u;
constexpr u32 kPlaneBytes = static_cast<u32>(kBytesPerRow) * kHeight;

/// HUD en la franja inferior: tres textos apilados (7 filas de glifo; 8 líneas de banda).
/// Las líneas van en **vpos crudo**: la pantalla empieza en `DIWSTRT` vpos 0x2C, así que la
/// fila de pantalla 232 (franja inferior) es vpos `0x2C + 232 = 276`. El armado se escribe
/// con el Copper antes de la banda (con el par de overflow de `wait_line_pal` para vpos ≥
/// 256) y el **secuenciador auto-arma** cada canal al llegar a su `VSTART`
/// (`custom.cpp:10088-10090`); la reposición POS-only del rearmado por línea no expresa
/// vpos ≥ 256 (`vline: u8`), así que no hay rearmado por línea (`per_line_rearm=false`) y
/// cada texto usa canales propios (3+3+2 = 8).
constexpr u16 kDiwVstart = 0x2cu;  // vpos del borde superior de la pantalla (DIWSTRT)
constexpr u16 kNameLine = kDiwVstart + 232u;    // "223 HUD" (7 chars = 35 px -> 3 canales)
constexpr u16 kVersionLine = kDiwVstart + 240u; // "v1.0.NNNN" (9 chars = 45 px -> 3 canales)
constexpr u16 kFpsLine = kDiwVstart + 248u;     // "FPS nn" (6 chars = 30 px -> 2 canales)
constexpr u16 kHpos0 = 0x81u + 32u; // HSTART del primer canal: x de pantalla 32 (> fetch de la
				    // línea, DDFSTRT/2 ≈ 28 px); el registro va en HSTART absoluto
				    // (`drawing.cpp:2706`: pantalla = HSTART − 0x81)
constexpr u16 kHposStep = 16u;      // 1 canal = 16 px
constexpr u8 kNameChannels = 3u;
constexpr u8 kVersionChannels = 3u;
constexpr u8 kFpsChannels = 2u;
constexpr u8 kNameFirst = 0u;    // canales por texto (sin reutilización: armado único)
constexpr u8 kVersionFirst = 3u;
constexpr u8 kFpsFirst = 6u;

/// Palabras de la tabla HUD en Chip (`[channel][line][2]` por capa): la estructura que
/// recibe `SPRxPT` (el DMA la lee por fetch, una fila por línea).
constexpr u16 kNameWords = kNameChannels * 8u * 2u;       // 48
constexpr u16 kVersionWords = kVersionChannels * 8u * 2u; // 48
constexpr u16 kFpsWords = kFpsChannels * 8u * 2u;         // 32
constexpr u16 kHudWords = kNameWords + kVersionWords + kFpsWords;

/// Paleta: COLOR00..07 = fondo (3 planos); COLOR17 = blanco del HUD (color 1 de sprite).
constexpr eng::Palette32 kPalette {{
	0x013, 0x124, 0x135, 0x246, 0x357, 0x468, 0x579, 0x68a,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0xfffu, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

/// Renderiza `text` en la tabla `data` (`[channels][lines][2]`, 16 px por canal, avance
/// 5 px). Solo pinta los píxeles encendidos; el resto queda transparente (00).
/// El valor de color del píxel es `DATB*2 + DAT` (DAT = bit BAJO, DATB = bit ALTO, AHRM
/// cap. 4): para color 1 (COLOR17) el bit va en **DAT**.
void render_text(u16* data, u8 lines, u8 channels, const char* text) {
	for (u8 ch = 0u; ch < channels; ++ch) {
		for (u8 l = 0u; l < lines; ++l) {
			data[(static_cast<u32>(ch) * lines + l) * 2u + 0u] = 0u;
			data[(static_cast<u32>(ch) * lines + l) * 2u + 1u] = 0u;
		}
	}
	const u8* p = reinterpret_cast<const u8*>(text);
	u16 ci = 0u;
	for (;;) {
		const u32 cp = eng::utf8::decode(p);
		if (cp == 0u) {
			break;
		}
		if (cp >= 32u) {
			for (u8 r = 0u; r < eng::Font5x7::kRows && r < lines; ++r) {
				const u8 bits = eng::Font5x7::row(static_cast<u16>(cp), r);
				for (u8 c = 0u; c < 5u; ++c) {
					if ((bits & (0x10u >> c)) == 0u) {
						continue;
					}
					const u16 px = static_cast<u16>(ci * 5u + c);
					const u8 ch = static_cast<u8>(px / 16u);
					if (ch >= channels) {
						continue;
					}
					const u16 bit = static_cast<u16>(px & 15u);
					u16* const cell = &data[(static_cast<u32>(ch) * lines + r) * 2u];
					cell[0] = static_cast<u16>(cell[0] | (0x8000u >> bit)); // DAT -> color 1
				}
			}
		}
		++ci;
	}
}

struct SpriteHudDemo {
	void init(amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({64u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022301u);
			return;
		}
		m_bitplanes = backend.memory_manager().chip().reserve<eng::PlaneTag>(
			kPlaneBytes * kPlanes, 16u);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(2048u, 16u);
		m_hud = backend.memory_manager().chip().reserve<eng::SpriteTag>(kHudWords * 2u, 16u);
		if (!m_bitplanes.valid() || !m_copper.valid() || !m_hud.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022302u);
			return;
		}
		build_background();

		// Tablas de DATA en Chip (`[channel][line][2]`), vistas tipadas por capa: es la
		// estructura que recibe `SPRxPT` (el fetch del DMA entrega las filas).
		u16* const hud = m_hud.view.as_words().data();
		const eng::ChipView<eng::SpriteTag> name_data =
			m_hud.mem_view_chip().subview(0u, kNameWords * 2u);
		const eng::ChipView<eng::SpriteTag> version_data = m_hud.mem_view_chip().subview(
			kNameWords * 2u, kVersionWords * 2u);
		const eng::ChipView<eng::SpriteTag> fps_data = m_hud.mem_view_chip().subview(
			(kNameWords + kVersionWords) * 2u, kFpsWords * 2u);
		render_text(hud, 8u, kNameChannels, "223 HUD");
		char label[16];
		(void)eng::debug::version_label(eng::debug::Version {1u, 0u}, label,
						static_cast<eng::u8>(sizeof(label)));
		render_text(hud + kNameWords, 8u, kVersionChannels, label);
		render_text(hud + kNameWords + kVersionWords, 8u, kFpsChannels, "FPS --");

		// Capas de armado único (`per_line_rearm=false`: la banda está en vpos > 255, donde
		// el Copper no puede esperar; el secuenciador auto-arma en `VSTART`). Canales
		// disjuntos por texto (3+3+2). `display_planes` activa el validador de coherencia
		// (`check_sprite_line_data`): con >5 planos configure devuelve false.
		if (!m_name.configure({kNameLine, 8u, kNameFirst, kNameChannels, kHpos0, kHposStep,
				       0u, name_data, false, false, kPlanes}) ||
		    !m_version.configure({kVersionLine, 8u, kVersionFirst, kVersionChannels, kHpos0,
					  kHposStep, 0u, version_data, false, false, kPlanes}) ||
		    !m_fps.configure({kFpsLine, 8u, kFpsFirst, kFpsChannels, kHpos0, kHposStep, 0u,
				      fps_data, false, false, kPlanes})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022303u);
			return;
		}
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022304u);
			return;
		}
		backend.takeover_display(m_copper_view);
		eng::debug::mark_ready(g_eng_run_status, kNameLine);
	}

	void update(amiga::AmigaBackend& backend, eng::GameContext& context) {
		(void)backend;
		// **Único valor dinámico**: fps estimados, ≤1 actualización por segundo y solo si el
		// número cambia (reconstruye la lista; el resto del frame no toca nada).
		const u32 now = eng::debug::DebugPeripheral::cycle_counter();
		if (!m_fps_armed) {
			m_fps_armed = true;
			m_cycle_ref = now;
			m_frame_ref = context.frame.frame_index;
			return;
		}
		const u32 elapsed = now - m_cycle_ref;
		if (elapsed < 7093790u) {
			return;
		}
		const u32 frames = context.frame.frame_index - m_frame_ref;
		const u32 fps = (frames * 7093790u) / elapsed;
		m_cycle_ref = now;
		m_frame_ref = context.frame.frame_index;
		if (fps == m_fps_value) {
			return;
		}
		m_fps_value = fps;
		char buf[8];
		buf[0] = 'F';
		buf[1] = 'P';
		buf[2] = 'S';
		buf[3] = ' ';
		buf[4] = static_cast<char>('0' + ((fps > 99u ? 99u : fps) / 10u));
		buf[5] = static_cast<char>('0' + ((fps > 99u ? 99u : fps) % 10u));
		buf[6] = '\0';
		render_text(m_hud.view.as_words().data() + kNameWords + kVersionWords, 8u,
			    kFpsChannels, buf);
		(void)build_copper();
	}

	void render(amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Fondo de 3 planos pintado UNA vez (bandas horizontales; el HUD va encima).
	void build_background() {
		u16* const planes = m_bitplanes.view.as_words().data();
		const u16 wpr = static_cast<u16>(kBytesPerRow / 2u);
		for (u16 y = 0u; y < kHeight; ++y) {
			const u8 band = static_cast<u8>((y / 32u) & 0x07u);
			for (u8 p = 0u; p < kPlanes; ++p) {
				const u16 word = (band & (1u << p)) != 0u ? 0xffffu : 0x0000u;
				u16* const row = planes + static_cast<u32>(p) * (kPlaneBytes / 2u) +
						 static_cast<u32>(y) * wpr;
				for (u16 i = 0u; i < wpr; ++i) {
					row[i] = word;
				}
			}
		}
	}

	/// Emite la copperlist completa (display + armado/reposición del HUD + cola). Se llama
	/// en el setup y, como mucho, 1 vez por segundo (cambio de fps); el frame no la toca.
	bool build_copper() {
		copper::SchedulerT<false> sched { m_copper };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow,
					  eng::graphics::composition::bplcon0_for(
						  eng::graphics::composition::SceneMode::Standard,
						  kPlanes),
					  kPlanes, m_bitplanes.mem_view_chip(), kPlaneBytes);
		// Reset de los 8 canales antes de habilitar el DMA de sprites (mismo orden que la
		// 053): PT a 0 y POS/CTL a 0 mientras el DMA está limpio. El bloque de punteros usa
		// stride 4 (`SPR0PTH=0x120`, `SPR1PTH=0x124`...); POS/CTL, stride 8
		// (`SPR0POS=0x140`, `SPR1POS=0x148`...).
		for (u8 ch = 0u; ch < eng::graphics::kSpriteChannels; ++ch) {
			const u16 pt = static_cast<u16>(0x120u + ch * 4u);
			sched.move(pt, 0x0000u);                             // SPRxPTH
			sched.move(static_cast<u16>(pt + 2u), 0x0000u);      // SPRxPTL
			const u16 ctl = static_cast<u16>(0x142u + ch * 8u);
			sched.move(ctl, 0x0000u);                            // SPRxCTL
			sched.move(static_cast<u16>(ctl - 2u), 0x0000u);     // SPRxPOS
		}
		sched.move(copper::Register::DMACON,
			   static_cast<u16>(copper::DmaSetClear | copper::DmaMaster | copper::DmaCopper |
					    copper::DmaBitplane | copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		m_name.emit_into(sched);
		m_version.emit_into(sched);
		m_fps.emit_into(sched);
		sched.wait_line(0xf8);
		sched.move(copper::Register::COLOR00, 0x0000);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_view = sched.chip_view();
		return m_copper_ok;
	}

	bool m_copper_ok = false;
	eng::ChipView<eng::CopperTag> m_copper_view {};
	eng::Block<eng::PlaneTag> m_bitplanes {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::Block<eng::SpriteTag> m_hud {};
	eng::graphics::SpriteLineLayer m_name {};
	eng::graphics::SpriteLineLayer m_version {};
	eng::graphics::SpriteLineLayer m_fps {};
	u32 m_cycle_ref = 0u;
	u32 m_frame_ref = 0u;
	u32 m_fps_value = 0u;
	bool m_fps_armed = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	amiga::AmigaBackend backend {};
	SpriteHudDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
