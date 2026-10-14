// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/features/engine/amiga/066_bob_showcase --debug   && bash ./tools/run/run-demo.sh demos/features/engine/amiga/066_bob_showcase --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/features/engine/amiga/066_bob_showcase --release && bash ./tools/run/run-demo.sh demos/features/engine/amiga/066_bob_showcase --keep-running

// ============================================================================
// Demo 066: showcase de BOBs + Sprites HW (matriz de la HOST-439 en vivo).
// ============================================================================
//
// Vitrina de **todos los usos de BOB** del motor, cada zona con su técnica. El bitmap es
// **interleaved** (planos por fila) y los BOB cubren los 5 planos: cada dibujado/borrado es
// **un solo blit** (`bitplane_count == 1`) — con target planar cada bob costaba 2-5 blits
// (uno por plano) y la demo no llegaba a 50 fps.
//
//   A "OR"     orbes rebotando: `BobDraw::Or` ($FC) + `bob_erase_box` (1 blit cada uno).
//   B "FAST"   deslizadores con **padding FastCopy**: copia opaca ($F0) que tapa por sí
//              sola la posición anterior (sin borrado); uno vertical y tres horizontales.
//   C "MASK"   caminantes **cookie-cut par** (imagen+máscara intercaladas, `$CA` en un
//              blit) sobre un damero estático: la máscara **preserva el fondo**; el
//              borrado previo limpia solo los planos 0-1.
//   D "SAVE"   patrullero cookie-cut par + **save-under** de un segundo objeto: al salir,
//              `bob_restore_box` devuelve el damero byte a byte.
//   E "ERASE"  barras que borran su caja previa (`bob_erase_box`) y se redibujan.
//   F "5PL"    joyas de **5 planos** (valores 16-31 de la paleta) con padding.
//   G (abajo)  **Sprites HW**: dos naves (cada una un **par adjunto** DAT/DATB = 15
//              colores) armadas por `SpriteManager::arm_object` en la copperlist.
//
// Fondo: degradado de `COLOR00` por bandas de Copper + arte estático (rejillas, dameros,
// iconos y estrellas) dibujado UNA vez en los planos 2-4 al arrancar.
//
// Referencia de la matriz por píxeles exactos: HOST-439 (`tests/host/graphics/
// 439_bob_render_matrix`).

#include <eng/api/api.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/core/data/utf8.hpp>
#include <eng/graphics/font5x7.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/bobs.hpp> // `scene::clear_box`

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

// ── Display (bitmap INTERLEAVED: 5 planos por fila) ─────────────────────────
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;
constexpr eng::u16 kDisplayTop = 0x2cu;
// Sprites: franja inferior (VSTART 210-244). El armado va tras las bandas y ANTES del
// VSTART: los WAIT del Copper deben ser estrictamente crecientes.
constexpr eng::u16 kArmLine = 200u;
constexpr eng::s16 kSpriteY = 216;

// ── Paleta (32 colores) ─────────────────────────────────────────────────────
constexpr eng::Palette32 kPalette {{
	0x012, 0xfff, 0x5ff, 0xf80, // 0 fondo (banda superior), 1 blanco, 2 cian, 3 naranja
	0x333, 0x555, 0x777, 0x999, // 4-7 trama de fondo
	0x0a8, 0x0c9, 0x2cf, 0x4af, // 8-11 damero A
	0x84f, 0xa6f, 0xc8f, 0xeaf, // 12-15 damero B
	0xf33, 0xfc6, 0xfff, 0xff0, // 16 rojo, 17-19 = nave HW (rojo/blanco/amarillo)
	0x3f3, 0x6f6, 0x9f9, 0xcfc, // 20 verde
	0x33f, 0x66f, 0x99f, 0xccf, // 24 azul
	0xf3f, 0xf6f, 0xf9f, 0xfcf, // 28 magenta
}};

/// Degradado del fondo (COLOR00 por bandas de 24 líneas desde la 76).
constexpr eng::u16 kBgBands[7] = {0x012, 0x023, 0x034, 0x134, 0x245, 0x356, 0x467};

// ── Zonas (x, y, w, h) ──────────────────────────────────────────────────────
constexpr eng::s16 kZoneAx = 8, kZoneAy = 44, kZoneAw = 144, kZoneAh = 48;   // OR
constexpr eng::s16 kZoneBx = 168, kZoneBy = 44, kZoneBw = 144, kZoneBh = 48;  // FAST
constexpr eng::s16 kZoneCx = 8, kZoneCy = 100, kZoneCw = 144, kZoneCh = 48;   // MASK
constexpr eng::s16 kZoneDx = 168, kZoneDy = 100, kZoneDw = 144, kZoneDh = 48; // SAVE
constexpr eng::s16 kZoneEx = 8, kZoneEy = 156, kZoneEw = 144, kZoneEh = 48;   // ERASE
constexpr eng::s16 kZoneFx = 168, kZoneFy = 156, kZoneFw = 144, kZoneFh = 48; // 5PL

// ── Arte: dimensiones (contenido + padding) ─────────────────────────────────
constexpr eng::u16 kOrbW = 10, kOrbH = 10;
constexpr eng::u16 kSliderW = 14, kSliderH = 8, kSliderPad = 4;
constexpr eng::u16 kVSlideW = 10, kVSlideH = 10, kVSlidePad = 4;
constexpr eng::u16 kWalkW = 16, kWalkH = 16;   // par cookie-cut (ancho múltiplo de 16)
constexpr eng::u16 kPatW = 16, kPatH = 16;     // par cookie-cut
constexpr eng::u16 kJewelW = 12, kJewelH = 12, kJewelPad = 4;
constexpr eng::u16 kBarW = 12, kBarH = 8;

// ── Sprites HW ──────────────────────────────────────────────────────────────
constexpr eng::u16 kShipW = 16, kShipH = 16;
constexpr eng::u16 kShipWords = static_cast<eng::u16>(kShipH * 2u + 2u); // DAT/DATB + fin
constexpr eng::u8 kShips = 2u;

/// Máscara de bits de un rango de píxeles [x0, x1] (bit 15 = píxel 0, izquierda).
[[nodiscard]] constexpr eng::u16 bits(eng::u16 x0, eng::u16 x1) {
	eng::u16 m = 0u;
	for (eng::u16 x = x0; x <= x1; ++x) {
		m = static_cast<eng::u16>(m | (0x8000u >> x));
	}
	return m;
}

/// Palabras por fila de una hoja (base + guarda).
[[nodiscard]] constexpr eng::u16 sheet_row_words(eng::u16 w) {
	return static_cast<eng::u16>((w + 15u) / 16u + 1u);
}

/// Palabras de una hoja **interleaved**: por fila lógica, la fila física de cada plano.
[[nodiscard]] constexpr eng::u16 sheet_words(eng::u16 w, eng::u16 h, eng::u8 planes) {
	return static_cast<eng::u16>(sheet_row_words(w) * h * planes);
}

struct BobShowcaseDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Variante 2 del 212: bajo un SO vivo, el Wait() de Exec mete ~0.5 campo de latencia
		// de despertar por frame (perfil: 88% de las muestras en ROM/SO). Espera ACTIVA.
		backend.set_vblank_sleep(false);
		// **Control absoluto** (procedimiento canónico del engine): Forbid + LoadView(NULL) +
		// 2×WaitTOF + OwnBlitter + guarda de INTENA/DMACON/ADKCON. Vuelta al SO:
		// `backend.restore_system()`. Ver
		// `docs/reference/amiga/hardware/takeover-y-restauracion-canonica.md`.
		(void)backend.takeover_system();
		if (!backend.configure_memory({ 160u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006601u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		if (!m_copper.begin(backend.memory_manager(), 2048u)) { eng::debug::mark_failed(g_eng_run_status, 0x00006605u); return; }
		m_bob_block = backend.memory_manager().chip().reserve<eng::BobTag>(12288u, 16);
		m_save_block = backend.memory_manager().chip().reserve<eng::BobTag>(1024u, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(512u, 16);
		if (!m_bitplane_block.valid() || !m_bob_block.valid() ||
		    !m_save_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006602u);
			return;
		}
		m_target = eng::graphics::make_bob_target(m_bitplane_block.mem_view_chip(), kBytesPerRow,
							  256u, kPlanes, eng::graphics::BobLayout::Planar);
		build_sheets();
		build_background_art();
		draw_version_strip(0u);
		build_sprite_art();
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006604u);
			return;
		}
		m_copper.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, 0x0000u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		const eng::u16 f = context.frame.frame_index;
		eng::debug::mark_frame(g_eng_run_status, f);
		// Cronómetro de depuración (franja): periodo de frame y duración del update, en
		// k ciclos (contador del periférico de debug; ver `eng/debug/peripheral.hpp`).
		const eng::u32 t0 = eng::debug::DebugPeripheral::cycle_counter();
		m_period_kcycles = (t0 - m_last_cycle) / 1000u;
		m_last_cycle = t0;
		// El plan del frame anterior no puede tocarse hasta que su cadena async termine.
		backend.frame_plan_async_wait();
		m_frame_plan.clear();

		update_orbs();
		update_sliders();
		update_walkers();
		update_patroller();
		update_saveunder();
		update_bars();
		update_jewels();

		// **Ejecución async** (cadena por IRQ de fin de blit; gate de INTENA en el motor para
		// una IRQ por job — bug 3 cerrado). Si el servicio no se pudo instalar, degradar al
		// camino síncrono (contrato de `execute_frame_plan_async`).
		if (!backend.execute_frame_plan_async(m_frame_plan)) {
			(void)backend.execute_frame_plan(m_frame_plan);
		}
		// Diagnóstico en la telemetría (`measure-fps --json`): nº de jobs si el plan es válido,
		// 0x6610 si `plan.ok()` es falso (el async no lanza nada).
		g_eng_run_status.detail =
			m_frame_plan.ok() ? m_frame_plan.blit_job_count() : 0x6610u;
		m_update_kcycles = (eng::debug::DebugPeripheral::cycle_counter() - t0) / 1000u;
		if ((f & 31u) == 0u) {
			draw_version_strip(f);
		}
		if (build_copper()) {
			m_copper.install(backend);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	// ── Helpers de arte ────────────────────────────────────────────────────
	[[nodiscard]] eng::graphics::Bob make_bob(eng::u16* sheet, eng::u16 w, eng::u16 h, eng::u8 planes,
						  eng::graphics::BobDraw draw,
						  eng::graphics::BobMaskPack pack =
							  eng::graphics::BobMaskPack::SeparatePlane) {
		eng::graphics::Bob b {};
		b.sheet = eng::ChipView<eng::BobTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(sheet)),
			static_cast<eng::u32>(sheet_words(w, h, planes) * 2u)};
		b.width = w;
		b.height = h;
		b.planes = planes;
		b.layout = eng::graphics::BobLayout::Interleaved;
		b.draw = draw;
		b.mask_pack = pack;
		return b;
	}

	/// Escribe una hoja **planar** (bloques de plano contiguos, guarda por fila).
	/// `fn(x, y) -> valor 0..2^planes-1`.
	template <class Fn>
	static void write_sheet(eng::u16* dst, eng::u16 w, eng::u16 h, eng::u8 planes, Fn fn) {
		const eng::u16 rw = sheet_row_words(w);
		const eng::u16 wpp = static_cast<eng::u16>(rw * h);
		const eng::u16 total = static_cast<eng::u16>(wpp * planes);
		for (eng::u16 i = 0u; i < total; ++i) {
			dst[i] = 0u; // la arena NO viene a cero: limpiar antes de escribir
		}
		for (eng::u8 p = 0u; p < planes; ++p) {
			for (eng::u16 y = 0u; y < h; ++y) {
				eng::u16* const row = dst + static_cast<eng::u32>(p) * wpp + y * rw;
				for (eng::u16 x = 0u; x < w; ++x) {
					if ((fn(x, y) & (1u << p)) == 0u) {
						continue;
					}
					row[x / 16u] = static_cast<eng::u16>(
						row[x / 16u] | (0x8000u >> (x & 15u)));
				}
			}
		}
	}

	/// Máscara de 1 plano (bloque contiguo, guarda por fila).
	template <class MaskFn>
	static void write_mask(eng::u16* dst, eng::u16 w, eng::u16 h, MaskFn mask) {
		const eng::u16 rw = sheet_row_words(w);
		const eng::u16 total = static_cast<eng::u16>(rw * h);
		for (eng::u16 i = 0u; i < total; ++i) {
			dst[i] = 0u;
		}
		for (eng::u16 y = 0u; y < h; ++y) {
			eng::u16* const row = dst + y * rw;
			for (eng::u16 x = 0u; x < w; ++x) {
				if (!mask(x, y)) {
					continue;
				}
				row[x / 16u] = static_cast<eng::u16>(row[x / 16u] | (0x8000u >> (x & 15u)));
			}
		}
	}

	/// Todas las hojas en `m_bob_block` (offsets calculados).
	void build_sheets() {
		eng::u16* const base = m_bob_block.view.as_words().data();
		eng::u16 off = 0u;

		// Orbe: disco (borde 1, núcleo 2) a 5 planos (los planos 2-4 a 0: el fondo se
		// conserva con `Or`).
		m_orb = make_bob(base + off, kOrbW, kOrbH, 2u, eng::graphics::BobDraw::Or);
		write_sheet(base + off, kOrbW, kOrbH, 2u, [](eng::u16 x, eng::u16 y) -> eng::u8 {
			const eng::s16 dx = static_cast<eng::s16>(x) - 4;
			const eng::s16 dy = static_cast<eng::s16>(y) - 4;
			const eng::s16 d2 = static_cast<eng::s16>(dx * dx + dy * dy);
			if (d2 > 20) return 0u;
			return d2 > 9 ? 1u : 2u;
		});
		off = static_cast<eng::u16>(off + sheet_words(kOrbW, kOrbH, 2u));

		// Deslizador horizontal con padding (cápsula 3 con brillo 1).
		const eng::u16 slider_w = static_cast<eng::u16>(kSliderW + 2u * kSliderPad);
		const eng::u16 slider_h = static_cast<eng::u16>(kSliderH + 2u * kSliderPad);
		m_slider = make_bob(base + off, slider_w, slider_h, 2u,
				    eng::graphics::BobDraw::Opaque);
		write_sheet(base + off, slider_w, slider_h, 2u, [](eng::u16 x, eng::u16 y) -> eng::u8 {
			const eng::u16 cx = static_cast<eng::u16>(x - kSliderPad);
			const eng::u16 cy = static_cast<eng::u16>(y - kSliderPad);
			if (x < kSliderPad || y < kSliderPad || cx >= kSliderW || cy >= kSliderH) {
				return 0u;
			}
			if (cy == 0u || cy + 1u == kSliderH) return 1u;
			return (cx >= 3u && cx <= 6u && cy >= 2u && cy <= 5u) ? 1u : 3u;
		});
		off = static_cast<eng::u16>(off + sheet_words(slider_w, slider_h, 2u));

		// Deslizador vertical con padding (damero 2/3).
		const eng::u16 vslide_w = static_cast<eng::u16>(kVSlideW + 2u * kVSlidePad);
		const eng::u16 vslide_h = static_cast<eng::u16>(kVSlideH + 2u * kVSlidePad);
		m_vslide = make_bob(base + off, vslide_w, vslide_h, 2u,
				    eng::graphics::BobDraw::Opaque);
		write_sheet(base + off, vslide_w, vslide_h, 2u, [](eng::u16 x, eng::u16 y) -> eng::u8 {
			const eng::u16 cx = static_cast<eng::u16>(x - kVSlidePad);
			const eng::u16 cy = static_cast<eng::u16>(y - kVSlidePad);
			if (x < kVSlidePad || y < kVSlidePad || cx >= kVSlideW || cy >= kVSlideH) {
				return 0u;
			}
			return (((cx >> 2) + (cy >> 2)) & 1u) != 0u ? 2u : 3u;
		});
		off = static_cast<eng::u16>(off + sheet_words(vslide_w, vslide_h, 2u));

		// Caminante cookie-cut planar (máscara en plano aparte): robot (cuerpo 2, remate 1).
		m_walker = make_bob(base + off, kWalkW, kWalkH, 2u,
				    eng::graphics::BobDraw::CookieCut);
		write_sheet(base + off, kWalkW, kWalkH, 2u, [](eng::u16 x, eng::u16 y) -> eng::u8 {
			const bool body = x >= 5u && x <= 10u && y >= 6u && y <= 14u;
			const bool head = x >= 6u && x <= 9u && y >= 2u && y <= 5u;
			const bool arm = (y >= 7u && y <= 9u) && (x == 4u || x == 11u);
			const bool foot = y == 15u && (x == 5u || x == 10u);
			if (!body && !head && !arm && !foot) return 0u;
			const bool edge = y == 6u || y == 14u || x == 5u || x == 10u;
			return edge ? 1u : 2u;
		});
		off = static_cast<eng::u16>(off + sheet_words(kWalkW, kWalkH, 2u));
		m_walker.mask = eng::ChipView<eng::BobTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(base + off)),
			static_cast<eng::u32>(sheet_words(kWalkW, kWalkH, 1u) * 2u)};
		write_mask(base + off, kWalkW, kWalkH, [](eng::u16 x, eng::u16 y) -> bool {
			const bool body = x >= 5u && x <= 10u && y >= 6u && y <= 14u;
			const bool head = x >= 6u && x <= 9u && y >= 2u && y <= 5u;
			const bool arm = (y >= 7u && y <= 9u) && (x == 4u || x == 11u);
			const bool foot = y == 15u && (x == 5u || x == 10u);
			return body || head || arm || foot;
		});
		off = static_cast<eng::u16>(off + sheet_words(kWalkW, kWalkH, 1u));

		// Patrullero cookie-cut planar: nave marciana (borde 3, cuerpo 1, cúpula 2).
		m_patroller = make_bob(base + off, kPatW, kPatH, 2u,
				       eng::graphics::BobDraw::CookieCut);
		write_sheet(base + off, kPatW, kPatH, 2u, [](eng::u16 x, eng::u16 y) -> eng::u8 {
			const eng::s16 dx = static_cast<eng::s16>(x) - 7;
			const eng::s16 dy = static_cast<eng::s16>(y) - 7;
			const eng::s16 r2 = static_cast<eng::s16>(dx * dx + dy * dy);
			if (r2 > 56) return 0u;
			if (y <= 5u) return 2u; // cúpula
			return r2 > 42 ? 3u : 1u;
		});
		off = static_cast<eng::u16>(off + sheet_words(kPatW, kPatH, 2u));
		m_patroller.mask = eng::ChipView<eng::BobTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(base + off)),
			static_cast<eng::u32>(sheet_words(kPatW, kPatH, 1u) * 2u)};
		write_mask(base + off, kPatW, kPatH, [](eng::u16 x, eng::u16 y) -> bool {
			const eng::s16 dx = static_cast<eng::s16>(x) - 7;
			const eng::s16 dy = static_cast<eng::s16>(y) - 7;
			return static_cast<eng::s16>(dx * dx + dy * dy) <= 56;
		});
		off = static_cast<eng::u16>(off + sheet_words(kPatW, kPatH, 1u));

		// Joya de 5 planos con padding (faceta por anillos 28/24/20).
		const eng::u16 jewel_w = static_cast<eng::u16>(kJewelW + 2u * kJewelPad);
		const eng::u16 jewel_h = static_cast<eng::u16>(kJewelH + 2u * kJewelPad);
		m_jewel = make_bob(base + off, jewel_w, jewel_h, 4u,
				   eng::graphics::BobDraw::Opaque);
		write_sheet(base + off, jewel_w, jewel_h, 4u, [](eng::u16 x, eng::u16 y) -> eng::u8 {
			const eng::u16 cx = static_cast<eng::u16>(x - kJewelPad);
			const eng::u16 cy = static_cast<eng::u16>(y - kJewelPad);
			if (x < kJewelPad || y < kJewelPad || cx >= kJewelW || cy >= kJewelH) {
				return 0u;
			}
			const eng::s16 dx = static_cast<eng::s16>(cx) - 5;
			const eng::s16 dy = static_cast<eng::s16>(cy) - 5;
			const eng::s16 man = static_cast<eng::s16>((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
			if (man > 8) return 0u;
			if (man > 5) return 12u;
			return man > 2 ? 11u : 10u;
		});
		off = static_cast<eng::u16>(off + sheet_words(jewel_w, jewel_h, 4u));

		// Barra (erase): cuerpo 2 con puntas 3.
		m_bar = make_bob(base + off, kBarW, kBarH, 2u, eng::graphics::BobDraw::Or);
		write_sheet(base + off, kBarW, kBarH, 2u, [](eng::u16 x, eng::u16 y) -> eng::u8 {
			if (y == 0u || y + 1u == kBarH) return 3u;
			return (x < 2u || x + 2u >= kBarW) ? 1u : 2u;
		});
		off = static_cast<eng::u16>(off + sheet_words(kBarW, kBarH, 2u));

		// Save-under: un "cofre" 2-planos con save/restore (técnica distinta al par).
		m_chest = make_bob(base + off, 16u, 16u, 2u, eng::graphics::BobDraw::Opaque);
		write_sheet(base + off, 16u, 16u, 2u, [](eng::u16 x, eng::u16 y) -> eng::u8 {
			if (x < 2u || x > 13u || y < 5u || y > 13u) return 0u;
			if (y == 5u || y == 13u || x == 2u || x == 13u) return 1u;
			return (y == 9u && (x == 7u || x == 8u)) ? 1u : 3u;
		});
		// Borrador de 2 planos (solo usa `planes`): limpia los planos 0-1 de los bobs
		// cookie-cut sin tocar el damero (planos 2-3).
		m_eraser2 = make_bob(base + off, 16u, 16u, 2u, eng::graphics::BobDraw::Or);
		off = static_cast<eng::u16>(off + sheet_words(16u, 16u, 2u));

		m_sheets_words = off;
		(void)m_sheets_words;
	}

	// ── Arte de fondo estático (planos 2-4, dibujado una vez) ──────────────
	void bg_set(eng::u8 plane, eng::s16 x, eng::s16 y) {
		if (x < 0 || x >= 320 || y < 0 || y >= 256) {
			return;
		}
		eng::u16* const planes = m_bitplane_block.view.as_words().data();
		const eng::u16 wpr = static_cast<eng::u16>(kBytesPerRow / 2u); // 20 palabras/fila
		eng::u16* const row = planes + static_cast<eng::u32>(plane) * (kPlaneBytes / 2u) +
				      static_cast<eng::u32>(y) * wpr;
		row[x / 16] = static_cast<eng::u16>(row[x / 16] | (0x8000u >> (x & 15)));
	}

	void bg_frame(eng::s16 x, eng::s16 y, eng::s16 w, eng::s16 h) {
		for (eng::s16 i = 0; i < w; i += 2) {
			bg_set(2u, static_cast<eng::s16>(x + i), y);
			bg_set(2u, static_cast<eng::s16>(x + i), static_cast<eng::s16>(y + h - 1));
		}
		for (eng::s16 j = 0; j < h; j += 2) {
			bg_set(2u, x, static_cast<eng::s16>(y + j));
			bg_set(2u, static_cast<eng::s16>(x + w - 1), static_cast<eng::s16>(y + j));
		}
	}

	void bg_checker(eng::s16 x, eng::s16 y, eng::s16 w, eng::s16 h) {
		for (eng::s16 j = 0; j < h; ++j) {
			for (eng::s16 i = 0; i < w; ++i) {
				const bool a = (((x + i) >> 3) + ((y + j) >> 3)) & 1;
				bg_set(a ? 2u : 3u, static_cast<eng::s16>(x + i), static_cast<eng::s16>(y + j));
			}
		}
	}

	void bg_icon(eng::u8 kind, eng::s16 cx, eng::s16 cy) {
		switch (kind) {
			case 0u: // círculo
				for (eng::s16 j = -6; j <= 6; ++j) {
					for (eng::s16 i = -6; i <= 6; ++i) {
						const eng::s16 d2 = static_cast<eng::s16>(i * i + j * j);
						if (d2 <= 36 && d2 >= 25) bg_set(3u, static_cast<eng::s16>(cx + i),
										 static_cast<eng::s16>(cy + j));
					}
				}
				break;
			case 1u: // flechas
				for (eng::s16 i = -6; i <= 6; ++i) {
					bg_set(3u, static_cast<eng::s16>(cx + i), static_cast<eng::s16>(cy - i / 3));
					bg_set(3u, static_cast<eng::s16>(cx + i), static_cast<eng::s16>(cy + i / 3));
				}
				break;
			case 2u: // damero
				bg_checker(static_cast<eng::s16>(cx - 6), static_cast<eng::s16>(cy - 6), 12, 12);
				break;
			case 3u: // disco
				for (eng::s16 j = -5; j <= 5; ++j) {
					for (eng::s16 i = -5; i <= 5; ++i) {
						if (i * i + j * j <= 25) {
							bg_set(3u, static_cast<eng::s16>(cx + i),
							       static_cast<eng::s16>(cy + j));
						}
					}
				}
				break;
			case 4u: // aspa
				for (eng::s16 i = -5; i <= 5; ++i) {
					bg_set(3u, static_cast<eng::s16>(cx + i), static_cast<eng::s16>(cy + i));
					bg_set(3u, static_cast<eng::s16>(cx + i), static_cast<eng::s16>(cy - i));
				}
				break;
			default: // estrella
				for (eng::s16 i = -6; i <= 6; ++i) {
					const eng::s16 j = static_cast<eng::s16>(6 - (i < 0 ? -i : i));
					bg_set(3u, static_cast<eng::s16>(cx + i), static_cast<eng::s16>(cy - j));
					bg_set(3u, static_cast<eng::s16>(cx + i), static_cast<eng::s16>(cy + j / 2));
				}
				break;
		}
	}

	// ── Franja de versión (norma: 16 px de alto, 4 bpp, parte inferior) ────
	/// Rellena la franja y=240..255 con `value` (planos 0-3) escribiendo las palabras.
	void strip_fill(eng::u8 value) {
		eng::u16* const planes = m_bitplane_block.view.as_words().data();
		const eng::u16 wpr = static_cast<eng::u16>(kBytesPerRow / 2u);
		for (eng::u8 p = 0u; p < 4u; ++p) {
			const eng::u16 word = (value & (1u << p)) != 0u ? 0xffffu : 0x0000u;
			for (eng::s16 y = 240; y < 256; ++y) {
				eng::u16* const row = planes +
						     static_cast<eng::u32>(p) * (kPlaneBytes / 2u) +
						     static_cast<eng::u32>(y) * wpr;
				for (eng::u16 i = 0u; i < wpr; ++i) {
					row[i] = word;
				}
			}
		}
	}

	/// Escribe `value` en el píxel (x, y) de la franja (planos 0-3).
	void strip_set(eng::s16 x, eng::s16 y, eng::u8 value) {
		if (x < 0 || x >= 320 || y < 240 || y >= 256) {
			return;
		}
		eng::u16* const planes = m_bitplane_block.view.as_words().data();
		const eng::u16 wpr = static_cast<eng::u16>(kBytesPerRow / 2u);
		for (eng::u8 p = 0u; p < 4u; ++p) {
			if ((value & (1u << p)) == 0u) {
				continue;
			}
			eng::u16* const row = planes + static_cast<eng::u32>(p) * (kPlaneBytes / 2u) +
					      static_cast<eng::u32>(y) * wpr;
			row[x / 16] = static_cast<eng::u16>(row[x / 16] | (0x8000u >> (x & 15)));
		}
	}

	/// Texto con la fuente 5x7 del engine dentro de la franja (avance 5 px).
	void strip_text(eng::s16 x, eng::s16 y, const char* text, eng::u8 color) {
		const eng::u8* p = reinterpret_cast<const eng::u8*>(text);
		for (;;) {
			const eng::u32 cp = eng::utf8::decode(p);
			if (cp == 0u) {
				break;
			}
			if (cp >= 32u) {
				for (eng::u8 r = 0u; r < eng::Font5x7::kRows; ++r) {
					const eng::u8 bits =
						eng::Font5x7::row(static_cast<eng::u16>(cp), r);
					for (eng::u8 c = 0u; c < 5u; ++c) {
						if ((bits & (0x10u >> c)) != 0u) {
							strip_set(static_cast<eng::s16>(x + c),
								  static_cast<eng::s16>(y + r), color);
						}
					}
				}
			}
			x = static_cast<eng::s16>(x + 5);
		}
	}

	/// Texto decimal con prefijo en la franja (sin stdlib).
	void strip_num(eng::s16 x, eng::s16 y, const char* prefix, eng::u32 value, eng::u8 color) {
		char buf[16];
		eng::u8 n = 0u;
		while (prefix[n] != '\0' && n + 1u < sizeof(buf)) {
			buf[n] = prefix[n];
			++n;
		}
		char tmp[10];
		eng::u8 t = 0u;
		do {
			tmp[t++] = static_cast<char>('0' + (value % 10u));
			value /= 10u;
		} while (value != 0u && t < 10u);
		while (t > 0u && n + 1u < sizeof(buf)) {
			buf[n++] = tmp[--t];
		}
		buf[n] = '\0';
		strip_text(x, y, buf, color);
	}

	/// Texto hex de 6 dígitos con prefijo (diagnóstico de direcciones en la franja).
	void strip_hex(eng::s16 x, eng::s16 y, char prefix, eng::uintptr value, eng::u8 color) {
		char buf[8];
		buf[0] = prefix;
		for (eng::u8 i = 0u; i < 6u; ++i) {
			const eng::u8 nib = static_cast<eng::u8>((value >> ((5u - i) * 4u)) & 0x0fu);
			buf[1u + i] = static_cast<char>(nib < 10u ? '0' + nib : 'A' + (nib - 10u));
		}
		buf[7] = '\0';
		strip_text(x, y, buf, color);
	}

	/// Franja de **nombre + versión (major.minor.build) + depuración** (norma del repo):
	/// 16 px de alto y 4 bpp en la parte inferior. Se repinta cada 32 frames (coste bajo).
	/// Depuración: frames, CPU del update (`U`) y periodo de frame (`T`), en k ciclos.
	void draw_version_strip(eng::u16 frame) {
		strip_fill(4u); // fondo oscuro (COLOR04)
		strip_text(4, 241, "066 BOB SHOWCASE", 1u);
		strip_hex(100, 241, 'B', reinterpret_cast<eng::uintptr>(m_bitplane_block.view.data()), 1u);
		strip_hex(160, 241, 'C', reinterpret_cast<eng::uintptr>(m_copper.active_words()), 1u);
		char label[16];
		(void)eng::debug::version_label(eng::debug::Version {1u, 0u}, label,
						static_cast<eng::u8>(sizeof(label)));
		strip_text(4, 249, label, 2u);
		strip_num(56, 249, "F", frame, 2u);
		strip_num(120, 249, "U", m_update_kcycles, 2u);
		strip_num(176, 249, "T", m_period_kcycles, 2u);
		strip_num(232, 249, "L", eng::amiga::AmigaBackend::blit_async_launches(), 1u);
		strip_num(268, 249, "S", eng::amiga::AmigaBackend::blit_async_submits(), 1u);
	}

	void build_background_art() {
		eng::u16 seed = 0x1234u;
		for (eng::u16 i = 0u; i < 90u; ++i) {
			seed = static_cast<eng::u16>(seed * 75u + 74u);
			const eng::s16 sx = static_cast<eng::s16>(seed % 320u);
			seed = static_cast<eng::u16>(seed * 75u + 74u);
			const eng::s16 sy = static_cast<eng::s16>(6u + seed % 30u);
			bg_set((i & 1u) != 0u ? 3u : 2u, sx, sy);
		}
		bg_frame(kZoneAx, kZoneAy, kZoneAw, kZoneAh);
		bg_frame(kZoneBx, kZoneBy, kZoneBw, kZoneBh);
		bg_frame(kZoneCx, kZoneCy, kZoneCw, kZoneCh);
		bg_frame(kZoneDx, kZoneDy, kZoneDw, kZoneDh);
		bg_frame(kZoneEx, kZoneEy, kZoneEw, kZoneEh);
		bg_frame(kZoneFx, kZoneFy, kZoneFw, kZoneFh);
		bg_frame(4, kSpriteY - 8, 312, 40);
		bg_checker(static_cast<eng::s16>(kZoneCx + 3), static_cast<eng::s16>(kZoneCy + 3),
			   static_cast<eng::s16>(kZoneCw - 6), static_cast<eng::s16>(kZoneCh - 6));
		bg_checker(static_cast<eng::s16>(kZoneDx + 3), static_cast<eng::s16>(kZoneDy + 3),
			   static_cast<eng::s16>(kZoneDw - 6), static_cast<eng::s16>(kZoneDh - 6));
		bg_icon(0u, static_cast<eng::s16>(kZoneAx + kZoneAw / 2), static_cast<eng::s16>(kZoneAy - 10));
		bg_icon(1u, static_cast<eng::s16>(kZoneBx + kZoneBw / 2), static_cast<eng::s16>(kZoneBy - 10));
		bg_icon(2u, static_cast<eng::s16>(kZoneCx + kZoneCw / 2), static_cast<eng::s16>(kZoneCy - 10));
		bg_icon(3u, static_cast<eng::s16>(kZoneDx + kZoneDw / 2), static_cast<eng::s16>(kZoneDy - 10));
		bg_icon(4u, static_cast<eng::s16>(kZoneEx + kZoneEw / 2), static_cast<eng::s16>(kZoneEy - 10));
		bg_icon(5u, static_cast<eng::s16>(kZoneFx + kZoneFw / 2), static_cast<eng::s16>(kZoneFy - 10));
	}

	// ── Sprites HW (camino de bajo nivel: `arm_object`, sin `SpriteScene`) ──
	void build_sprite_art() {
		eng::u16* const spr = m_sprite_block.view.as_words().data();
		// Nave 16x16 (DAT/DATB): casco (1), cabina (2), alas/llama (3).
		for (eng::u16 y = 0u; y < kShipH; ++y) {
			eng::u16 dat = 0u;
			eng::u16 datb = 0u;
			if (y >= 3u && y <= 7u) {
				dat = bits(6u, 9u); // cabina
				datb = bits(7u, 8u);
			}
			if (y >= 8u && y <= 12u) {
				dat = bits(4u, 11u); // casco
				datb = bits(6u, 9u);
			}
			if (y >= 10u && y <= 12u) {
				dat = bits(1u, 14u); // alas
				datb = bits(1u, 3u) | bits(12u, 14u);
			}
			if (y >= 13u && y <= 15u) {
				dat = bits(6u, 9u); // llama
				datb = bits(6u, 9u);
			}
			spr[y * 2u + 0u] = dat;
			spr[y * 2u + 1u] = datb;
		}
		spr[kShipWords - 2u] = 0u;
		spr[kShipWords - 1u] = 0u;
		m_ship_data = eng::ChipView<eng::SpriteTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(spr)),
			static_cast<eng::u32>(kShipWords * 2u)};
	}

	void update_sprites_arm(eng::copper::SchedulerT<false>& sched, eng::u16 frame) {
		sched.wait_line(static_cast<eng::u8>(kArmLine & 0xffu));
		for (eng::u8 s = 0u; s < kShips; ++s) {
			const eng::u16 span = static_cast<eng::u16>(200u - s * 60u);
			const eng::u16 phase = static_cast<eng::u16>((frame * (2u + s)) % (2u * span));
			const eng::u16 x = static_cast<eng::u16>(60u + s * 40u +
								 (phase < span ? phase : 2u * span - phase));
			const eng::u16 y = static_cast<eng::u16>(kSpriteY + (s == 0u ? 0 : 18));
			const eng::u8 ch = static_cast<eng::u8>(s * 2u);
			eng::graphics::SpriteManager::arm_object(sched, ch, m_ship_data, x, y, kShipH, false);
			eng::graphics::SpriteManager::arm_object(sched, static_cast<eng::u8>(ch + 1u),
								 m_ship_data, x, y, kShipH, true);
		}
	}

	// ── Simulación por zonas ───────────────────────────────────────────────
	void update_orbs() {
		for (eng::u8 i = 0u; i < kOrbs; ++i) {
			Orb& o = m_orbs[i];
			if (m_orb_prev_valid[i]) {
				(void)eng::graphics::bob_erase_box(
					m_frame_plan, m_orb, static_cast<eng::u16>(kOrbW + 6u),
					static_cast<eng::u16>(kOrbH + 6u),
					static_cast<eng::s16>(o.x - 3), static_cast<eng::s16>(o.y - 3),
					m_target);
			}
			o.vy = static_cast<eng::s16>(o.vy + 1);
			if (o.vy > 3) o.vy = 3;
			o.x = static_cast<eng::s16>(o.x + o.vx);
			o.y = static_cast<eng::s16>(o.y + o.vy);
			if (o.x < kZoneAx + 2 || o.x > kZoneAx + kZoneAw - kOrbW - 2) {
				o.vx = static_cast<eng::s16>(-o.vx);
				o.x = static_cast<eng::s16>(o.x + o.vx);
			}
			if (o.y > kZoneAy + kZoneAh - kOrbH - 2) {
				o.y = static_cast<eng::s16>(kZoneAy + kZoneAh - kOrbH - 2);
				o.vy = (i & 1u) != 0u ? -2 : -3;
			}
			(void)eng::graphics::bob_draw(m_frame_plan, m_orb, 0u, o.x, o.y, m_target);
			m_orb_prev_valid[i] = true;
		}
	}

	void update_sliders() {
		for (eng::u8 i = 0u; i < kSliders; ++i) {
			Slider& s = m_sliders[i];
			s.x = static_cast<eng::s16>(s.x + s.vx);
			const eng::s16 minx = static_cast<eng::s16>(kZoneBx + kSliderPad + 1);
			const eng::s16 maxx =
				static_cast<eng::s16>(kZoneBx + kZoneBw - kSliderW - kSliderPad - 1);
			if (s.x < minx || s.x > maxx) {
				s.vx = static_cast<eng::s16>(-s.vx);
				s.x = static_cast<eng::s16>(s.x + s.vx);
			}
			(void)eng::graphics::bob_draw(m_frame_plan, m_slider, 0u,
						      static_cast<eng::s16>(s.x - kSliderPad),
						      static_cast<eng::s16>(s.y - kSliderPad), m_target);
		}
		m_vslide_y = static_cast<eng::s16>(m_vslide_y + m_vslide_vy);
		if (m_vslide_y < kZoneBy + kVSlidePad + 1) {
			m_vslide_y = static_cast<eng::s16>(kZoneBy + kVSlidePad + 1);
			m_vslide_vy = static_cast<eng::s16>(-m_vslide_vy);
		}
		if (m_vslide_y > kZoneBy + kZoneBh - kVSlideH - kVSlidePad - 1) {
			m_vslide_y = static_cast<eng::s16>(kZoneBy + kZoneBh - kVSlideH - kVSlidePad - 1);
			m_vslide_vy = static_cast<eng::s16>(-m_vslide_vy);
		}
		(void)eng::graphics::bob_draw(m_frame_plan, m_vslide, 0u,
					      static_cast<eng::s16>(kZoneBx + 118 - kVSlidePad),
					      static_cast<eng::s16>(m_vslide_y - kVSlidePad), m_target);
	}

	void update_walkers() {
		for (eng::u8 i = 0u; i < kWalkers; ++i) {
			Walker& w = m_walkers[i];
			// Borra la caja previa SOLO en los planos 0-1 (bob de 2 planos): el damero
			// (planos 2-3) queda intacto.
			if (m_walk_prev_valid[i]) {
				(void)eng::graphics::bob_erase_box(
					m_frame_plan, m_eraser2, static_cast<eng::u16>(kWalkW + 2u),
					static_cast<eng::u16>(kWalkH + 2u),
					static_cast<eng::s16>(w.x - 1), static_cast<eng::s16>(w.y - 1),
					m_target);
			}
			w.x = static_cast<eng::s16>(w.x + w.vx);
			const eng::s16 minx = static_cast<eng::s16>(kZoneCx + 3);
			const eng::s16 maxx = static_cast<eng::s16>(kZoneCx + kZoneCw - kWalkW - 3);
			if (w.x < minx || w.x > maxx) {
				w.vx = static_cast<eng::s16>(-w.vx);
				w.x = static_cast<eng::s16>(w.x + w.vx);
			}
			(void)eng::graphics::bob_draw(m_frame_plan, m_walker, 0u, w.x, w.y, m_target);
			m_walk_prev_valid[i] = true;
		}
	}

	void update_patroller() {
		if (m_pat_prev_valid) {
			(void)eng::graphics::bob_erase_box(
				m_frame_plan, m_eraser2, static_cast<eng::u16>(kPatW + 4u),
				static_cast<eng::u16>(kPatH + 4u),
				static_cast<eng::s16>(m_pat_x - 2), static_cast<eng::s16>(m_pat_y - 2),
				m_target);
		}
		m_pat_x = static_cast<eng::s16>(m_pat_x + m_pat_vx);
		const eng::s16 minx = static_cast<eng::s16>(kZoneDx + 3);
		const eng::s16 maxx = static_cast<eng::s16>(kZoneDx + kZoneDw - kPatW - 3);
		if (m_pat_x < minx || m_pat_x > maxx) {
			m_pat_vx = static_cast<eng::s16>(-m_pat_vx);
			m_pat_x = static_cast<eng::s16>(m_pat_x + m_pat_vx);
		}
		(void)eng::graphics::bob_draw(m_frame_plan, m_patroller, 0u, m_pat_x, m_pat_y, m_target);
		m_pat_prev_valid = true;
	}

	/// Save-under: `bob_save_box` al llegar + `bob_restore_box` al salir (2 planos).
	void update_saveunder() {
		if (m_chest_prev_valid) {
			(void)eng::graphics::bob_restore_box(
				m_frame_plan, m_chest, 16u, 16u, m_chest_x, m_chest_y, m_target,
				eng::Span<eng::u16> {
					reinterpret_cast<eng::u16*>(m_save_block.view.as_words().data()),
					static_cast<eng::usize>(kChestSaveWords)},
				kChestSaveRowWords, 16u);
		}
		m_chest_x = static_cast<eng::s16>(m_chest_x + m_chest_vx);
		const eng::s16 minx = static_cast<eng::s16>(kZoneDx + 60);
		const eng::s16 maxx = static_cast<eng::s16>(kZoneDx + kZoneDw - 16 - 3);
		if (m_chest_x < minx || m_chest_x > maxx) {
			m_chest_vx = static_cast<eng::s16>(-m_chest_vx);
			m_chest_x = static_cast<eng::s16>(m_chest_x + m_chest_vx);
		}
		(void)eng::graphics::bob_save_box(
			m_frame_plan, m_chest, 16u, 16u, m_chest_x, m_chest_y, m_target,
			eng::Span<eng::u16> {
				reinterpret_cast<eng::u16*>(m_save_block.view.as_words().data()),
				static_cast<eng::usize>(kChestSaveWords)},
			kChestSaveRowWords, 16u);
		(void)eng::graphics::bob_draw(m_frame_plan, m_chest, 0u, m_chest_x, m_chest_y, m_target);
		m_chest_prev_valid = true;
	}

	void update_bars() {
		for (eng::u8 i = 0u; i < kBars; ++i) {
			Bar& b = m_bars[i];
			if (m_bar_prev_valid[i]) {
				(void)eng::graphics::bob_erase_box(
					m_frame_plan, m_bar, static_cast<eng::u16>(kBarW + 6u),
					static_cast<eng::u16>(kBarH + 6u),
					static_cast<eng::s16>(b.x - 3), static_cast<eng::s16>(b.y - 3),
					m_target);
			}
			b.x = static_cast<eng::s16>(b.x + b.vx);
			const eng::s16 minx = static_cast<eng::s16>(kZoneEx + 3);
			const eng::s16 maxx = static_cast<eng::s16>(kZoneEx + kZoneEw - kBarW - 3);
			if (b.x < minx || b.x > maxx) {
				b.vx = static_cast<eng::s16>(-b.vx);
				b.x = static_cast<eng::s16>(b.x + b.vx);
			}
			(void)eng::graphics::bob_draw(m_frame_plan, m_bar, 0u, b.x, b.y, m_target);
			m_bar_prev_valid[i] = true;
		}
	}

	void update_jewels() {
		for (eng::u8 i = 0u; i < kJewels; ++i) {
			Jewel& j = m_jewels[i];
			j.x = static_cast<eng::s16>(j.x + j.vx);
			j.y = static_cast<eng::s16>(j.y + j.vy);
			const eng::s16 minx = static_cast<eng::s16>(kZoneFx + kJewelPad + 1);
			const eng::s16 maxx =
				static_cast<eng::s16>(kZoneFx + kZoneFw - kJewelW - kJewelPad - 1);
			const eng::s16 miny = static_cast<eng::s16>(kZoneFy + kJewelPad + 1);
			const eng::s16 maxy =
				static_cast<eng::s16>(kZoneFy + kZoneFh - kJewelH - kJewelPad - 1);
			if (j.x < minx || j.x > maxx) {
				j.vx = static_cast<eng::s16>(-j.vx);
				j.x = static_cast<eng::s16>(j.x + j.vx);
			}
			if (j.y < miny || j.y > maxy) {
				j.vy = static_cast<eng::s16>(-j.vy);
				j.y = static_cast<eng::s16>(j.y + j.vy);
			}
			(void)eng::graphics::bob_draw(m_frame_plan, m_jewel, 0u,
						      static_cast<eng::s16>(j.x - kJewelPad),
						      static_cast<eng::s16>(j.y - kJewelPad), m_target);
		}
	}

	/// Copperlist del frame: display, DMACON, paleta, bandas del degradado y armado de
	/// sprites (orden de WAIT estrictamente creciente; ver notas de 066).
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper.inactive_block() };
		// BPLCON0 derivado del nº de planos: BPU (bits 12-14) = kPlanes; 0x0200 = color.
		// Hardcodearlo causó el bug del 5º plano fantasma (BPL5PT basura → valores 16-31).
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow,
					  static_cast<eng::u16>(0x4000u | (kPlanes << 12u)),
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		eng::u16 line = 0x4cu; // 76
		for (eng::u8 band = 1u; band < 7u; ++band) {
			sched.wait_line(static_cast<eng::u8>(line & 0xffu));
			sched.move(eng::copper::Register::COLOR00, kBgBands[band]);
			line = static_cast<eng::u16>(line + 24u);
		}
		update_sprites_arm(sched, m_arm_frame);
		++m_arm_frame;
		sched.wait_line(0xf8);
		sched.end();
		if (!sched.ok()) {
			return false;
		}
		m_copper.flip();
		return true;
	}

	// ── Estado ─────────────────────────────────────────────────────────────
	static constexpr eng::u8 kOrbs = 3u;
	static constexpr eng::u8 kSliders = 2u;
	static constexpr eng::u8 kWalkers = 1u;
	static constexpr eng::u8 kBars = 1u;
	static constexpr eng::u8 kJewels = 1u;
	// Save-under del cofre: 16 px + shift = 2 palabras/fila × 16 filas × 2 planos.
	static constexpr eng::u16 kChestSaveRowWords = 2u;
	static constexpr eng::u16 kChestSaveWords = static_cast<eng::u16>(kChestSaveRowWords * 16u * 2u);

	struct Orb {
		eng::s16 x;
		eng::s16 y;
		eng::s16 vx;
		eng::s16 vy;
	};
	struct Slider {
		eng::s16 x;
		eng::s16 y;
		eng::s16 vx;
	};
	struct Walker {
		eng::s16 x;
		eng::s16 y;
		eng::s16 vx;
	};
	struct Bar {
		eng::s16 x;
		eng::s16 y;
		eng::s16 vx;
	};
	struct Jewel {
		eng::s16 x;
		eng::s16 y;
		eng::s16 vx;
		eng::s16 vy;
	};

	Orb m_orbs[kOrbs] {
		{24, 60, 2, 0}, {52, 52, -3, 0}, {80, 66, 2, 0},
	};
	bool m_orb_prev_valid[kOrbs] {false, false, false};

	Slider m_sliders[kSliders] {
		{static_cast<eng::s16>(kZoneBx + 12), static_cast<eng::s16>(kZoneBy + 8), 3},
		{static_cast<eng::s16>(kZoneBx + 40), static_cast<eng::s16>(kZoneBy + 24), -3},
	};
	eng::s16 m_vslide_y = static_cast<eng::s16>(kZoneBy + 4);
	eng::s16 m_vslide_vy = 3;

	Walker m_walkers[kWalkers] {
		{static_cast<eng::s16>(kZoneCx + 10), static_cast<eng::s16>(kZoneCy + 24), 1},
	};
	bool m_walk_prev_valid[kWalkers] {false};

	eng::s16 m_pat_x = static_cast<eng::s16>(kZoneDx + 40);
	eng::s16 m_pat_y = static_cast<eng::s16>(kZoneDy + 24);
	eng::s16 m_pat_vx = 2;
	bool m_pat_prev_valid = false;

	eng::s16 m_chest_x = static_cast<eng::s16>(kZoneDx + 100);
	eng::s16 m_chest_y = static_cast<eng::s16>(kZoneDy + 22);
	eng::s16 m_chest_vx = 2;
	bool m_chest_prev_valid = false;

	Bar m_bars[kBars] {
		{static_cast<eng::s16>(kZoneEx + 10), static_cast<eng::s16>(kZoneEy + 10), 2},
	};
	bool m_bar_prev_valid[kBars] {false};

	Jewel m_jewels[kJewels] {
		{static_cast<eng::s16>(kZoneFx + 30), static_cast<eng::s16>(kZoneFy + 18), 2, 1},
	};

	// Sprites HW (armado directo por Copper).
	eng::ChipView<eng::SpriteTag> m_ship_data {};
	eng::u16 m_arm_frame = 0u;
	eng::u32 m_last_cycle = 0u;
	eng::u32 m_update_kcycles = 0u;
	eng::u32 m_period_kcycles = 0u;

	// Hojas y geometría.
	eng::graphics::Bob m_orb {};
	eng::graphics::Bob m_slider {};
	eng::graphics::Bob m_vslide {};
	eng::graphics::Bob m_walker {};
	eng::graphics::Bob m_patroller {};
	eng::graphics::Bob m_jewel {};
	eng::graphics::Bob m_bar {};
	eng::graphics::Bob m_chest {};
	eng::graphics::Bob m_eraser2 {}; ///< 2 planos: borra solo los planos de los bobs 0-1
	eng::u16 m_sheets_words = 0u;

	eng::graphics::FramePlan m_frame_plan {};
	eng::graphics::BobTarget m_target {};
	eng::copper::DoubleBuffer m_copper {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::BobTag> m_bob_block {};
	eng::Block<eng::BobTag> m_save_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	BobShowcaseDemo demo {};
	eng::Engine engine { backend, demo };
	// La demo usa el **encadenado async** del `FramePlan` (su propia IRQ de fin de blit):
	// `run_frames` instala por defecto el `BackgroundPump` como servicio de blit y
	// `install_blit_service` rechaza el segundo — sin esto, `execute_frame_plan_async`
	// fallaba en silencio y los BOBs no se dibujaban (bug 4 de la 066).
	engine.set_blit_service_enabled(false);
	(void)eng::os::init(engine, 0u);
	engine.run_frames(0xffff);

	return 0;
}
