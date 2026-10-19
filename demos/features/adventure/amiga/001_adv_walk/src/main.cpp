// ============================================================================
// Demo 001 (adventure): paseo por walkboxes con camara de mundo (A0)
// ============================================================================
//
// Tutorial de la capa de aventura del engine (`eng::adv`) sobre la fachada:
//
//   - El mundo (544x208 de bitmap con margen de fino) tiene TRES walkboxes convexas
//     contiguas en la playa; el actor anda por la ruta que calcula
//     `eng::adv::walkbox_route` (Dijkstra sobre cajas + suavizado por visibilidad) al
//     punto pedido.
//   - `eng::adv::Camera<Fixed<s32,16>>` sigue al actor con dead-zone y limites, y su
//     posicion se convierte en la **ventana de display**: `fine_scroll_coarse` (coarse de
//     los BPLxPT) + `fine_delay` (BPLCON1), la convencion de
//     `eng/graphics/playfield_scroll.hpp` (DDFSTRT adelantado una palabra).
//   - El mouse mueve un puntero software y al pulsar el boton izquierdo pide destino
//     (punto en coords de mundo); sin clicks, un guion determinista pasea al actor para
//     que la animacion no pare (y para las capturas de vision).
//   - Hotspots: la PUERTA y el CARTEL muestran su etiqueta en el HUD al pasar por encima.
//
// Dibujo: el fondo se pinta UNA vez (CPU, init) en un bitmap 4bpp interleaved por planos;
// el actor y el puntero son **bobs enmascarados** (cookie-cut `$CA`) con save-under en
// scratch (restore + stash + draw por plano): 8 blits pequenos por frame como mucho, y
// los planos nunca se repintan. HUD 1bpp en la franja inferior (wait del Copper a la
// linea 252 + BPLCON0 de 1 plano), texto por palabras (`Surface::draw_text_words`).
//
//   bash ./tools/build/build-demo.sh demos/features/adventure/amiga/001_adv_walk --debug
//   bash ./tools/run/run-demo.sh demos/features/adventure/amiga/001_adv_walk

#include <eng/adv/camera.hpp>
#include <eng/adv/navigation.hpp>
#include <eng/adv/types.hpp>
#include <eng/adv/walkbox.hpp>
#include <eng/api/api.hpp>
#include <eng/core/math/fixed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/input_poll.hpp>

#include <eng/platform/amiga/entry.hpp>
namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

// --- Geometria del display ----------------------------------------------------------
// Bitmap: 368 px (mundo 352 + 16 de margen delantero para el fino continuo; el limite del
// validador de pixeles visibles OCS es 368), 208 filas de mundo + 48 de HUD.
constexpr u16 kWorldW = 352u; // 368 (limite visible OCS) - 16 de margen
constexpr u16 kMarginX = 16u;
constexpr u16 kBitmapW = kWorldW + kMarginX;
constexpr u16 kFbH = 208u;
constexpr u16 kRowBytes = kBitmapW / 8u;      // 68 B por fila y plano (544 px)
constexpr u16 kViewportW = 320u;
constexpr u8 kPlanes = 4u;
constexpr u16 kSceneBytes = kRowBytes * kFbH;
constexpr u16 kDdfstrt = eng::graphics::fine_scroll_ddfstrt; // $30: una palabra de margen
constexpr u16 kFetchBytes = 2u + 40u;         // 21 palabras de fetch ($30..$d0)
// Modulo de display: la fila fisica del bitmap mide 68 B y el fetch consume 42 B.
constexpr s16 kPlaneMod = static_cast<s16>(kRowBytes - kFetchBytes);
constexpr u16 kDdfstop = 0x00d0u;
constexpr u16 kDiwstrt = 0x2c81u;
constexpr u16 kDiwstop = 0x2cc1u;

// HUD: franja de 48 lineas a partir de la 252 (44 + 208), 1 plano.
constexpr u16 kHudLines = 48u;
constexpr u16 kHudLine = 44u + kFbH;
constexpr u16 kHudBytes = 44u * kHudLines; // 352 px: filas de 44 B (fetch $30..$d8, como la 080)
constexpr u16 kHudColor0 = 0x011u;
constexpr u16 kHudColor1 = 0xfffu;
constexpr u8 kHudCols = 38u; // ancho fijo repintado (38x8 = 304 px desde x=16)

// Camara: limites del origen de la vista en mundo.
constexpr s16 kCamMaxX = static_cast<s16>(kWorldW - kViewportW); // 32
constexpr u8 kActorSpeed = 2u;   // px por frame
constexpr u8 kActorW = 32u;      // arte 16 + 16 de guarda (shift del cookie-cut)
constexpr u8 kActorH = 24u;
constexpr u8 kPtrW = 32u;
constexpr u8 kPtrH = 11u;

// --- Paleta (4 planos) --------------------------------------------------------------
// 0 negro · 1..3 cielo · 4..6 mar · 7 sol/espuma · 8..10 arena · 11 madera · 12 piedra ·
// 13 puerta · 14 cartel/cuerpo · 15 piel/claro.
constexpr eng::Palette32 kPalette {{
	0x000, 0x113, 0x225, 0x448, 0x036, 0x048, 0x26a, 0xfff,
	0xda7, 0xc96, 0xb85, 0x852, 0x999, 0x322, 0xc63, 0xfdb,
}};

constexpr eng::debug::Version kVersion {1u, 0u}; // el build lo inyecta build-demo.sh

/// Etapas de arranque propias (el runner las lee en el detail del RunStatus).
enum : eng::u32 {
	kBootMemory = 0x00000101u,  ///< reservas Chip (HUD/arte/save-under)
	kBootHud = 0x00000102u,     ///< tira del HUD
	kBootStash = 0x00000103u,   ///< scratch del save-under
	kBootCompose = 0x00000104u, ///< escena (base) — se le suma el motivo de config_error()
	kBootArt = 0x00000105u,     ///< copia del arte a Chip
};

enum : eng::u8 {
	kProfFrame = 0,
	kProfBlits = 1,
	kProfHud = 2,
	kProfDbg = 3,
};

// --- Backdrop procedural (se pinta una vez) -----------------------------------------

/// Color de fondo (indice 0..15) del pixel de BITMAP (x 0..543, y 0..207). El mundo es
/// [16, 16+512); fuera del mundo, cielo (el margen se ve durante el paneo).
[[nodiscard]] constexpr u8 backdrop(u16 bx, u16 y) noexcept {
	const s32 x = static_cast<s32>(bx) - static_cast<s32>(kMarginX);
	// Cielo con bandas.
	if (y < 60u) return (y < 24u) ? u8 {1u} : ((y < 44u) ? u8 {2u} : u8 {3u});
	if (y < 88u) {
		// Sol y nubes simples (solo dentro del mundo).
		if (x >= 250 && x < 290 && y >= 40 && y < 60) return 7u;
		if (y < 72u && x >= 60 && x < 220 && (y & 3u) < 2u) return 7u;
		return 3u;
	}
	if (y < 128u) {
		// Mar con lineas de espuma.
		if (((y + x / 3) & 7u) == 0u) return 7u;
		return ((y & 1u) != 0u) ? u8 {5u} : u8 {4u};
	}
	if (y < 196u) {
		if (x < -8 || x >= static_cast<s32>(kWorldW) + 8) return 6u;
		// Colinas lejanas.
		if (y < 140u && x > 240 && y - 128 > (x - 240) / 3) return ((x & 3u) == 0u) ? u8 {11u} : u8 {10u};
		// Muelle de madera.
		if (x >= 40 && x < 200 && y >= 148 && y < 168) {
			if ((x & 7u) == 0u) return 11u;
			return 10u;
		}
		// Pared de piedra con la PUERTA (hotspot 430..458 x 120..164) y el CARTEL.
		if (x >= 278 && x < 350 && y >= 104 && y < 168) {
			if (x >= 288 && x < 318 && y >= 112 && y < 164) {
				if (x == 302) return 13u;       // hendidura central de la puerta
				return ((x + y) & 1u) != 0u ? u8 {12u} : u8 {13u};
			}
			return ((x ^ y) & 2u) != 0u ? u8 {11u} : u8 {12u};
		}
		// Cartel de madera sobre el muelle (hotspot 120..150 x 140..166).
		if (x >= 118 && x < 152 && y >= 138 && y < 148) return 14u;
		if (x >= 132 && x < 138 && y >= 148 && y < 168) return 11u;
		return ((x + y) & 15u) == 0u ? u8 {10u} : u8 {8u};
	}
	return ((x & 3u) == 0u) ? u8 {9u} : u8 {8u};
}

// --- Bobs enmascarados (planar compacto por plano + mascara) -------------------------

/// Palabras por plano de un bob W x H (`W` multiplo de 16).
constexpr u16 bob_plane_words(u16 w, u16 h) noexcept { return static_cast<u16>(w / 16u) * h; }

/// Genera el arte planar compacto (por plano: H filas de W/16 palabras) de `pixel(y,x)`.
template <u16 W, u16 H, class Fn>
constexpr auto make_sheet(Fn pixel) {
	constexpr u16 kW = W / 16u;
	eng::util::Array<u16, 4u * kW * H> out {};
	for (u16 y = 0u; y < H; ++y) {
		for (u16 x = 0u; x < W; ++x) {
			const u8 color = pixel(y, x);
			if (color == 0u) continue;
			for (u8 p = 0u; p < 4u; ++p) {
				if ((color & static_cast<u8>(1u << p)) == 0u) continue;
				out[static_cast<eng::usize>(p) * kW * H + static_cast<eng::usize>(y) * kW + (x >> 4u)] =
					static_cast<u16>(
						out[static_cast<eng::usize>(p) * kW * H +
						    static_cast<eng::usize>(y) * kW + (x >> 4u)] |
						static_cast<u16>(0x8000u >> (x & 15u)));
			}
		}
	}
	return out;
}

/// Mascara de opacidad (1 plano compacto) de `pixel(y,x)`.
template <u16 W, u16 H, class Fn>
constexpr auto make_mask(Fn pixel) {
	constexpr u16 kW = W / 16u;
	eng::util::Array<u16, kW * H> out {};
	for (u16 y = 0u; y < H; ++y) {
		for (u16 x = 0u; x < W; ++x) {
			if (pixel(y, x) == 0u) continue;
			out[static_cast<eng::usize>(y) * kW + (x >> 4u)] = static_cast<u16>(
				out[static_cast<eng::usize>(y) * kW + (x >> 4u)] |
				static_cast<u16>(0x8000u >> (x & 15u)));
		}
	}
	return out;
}

/// Explorador de 16x24 (arte a la izquierda; 16 px de guarda a la derecha para el shift).
/// `step` alterna las piernas; `face_left` invierte el sombreado de la cara.
[[nodiscard]] constexpr u8 actor_pixel(u16 y, u16 x, bool step) noexcept {
	if (x >= 16u) return 0u;
	// Cabeza (filas 0..7).
	if (y < 8u) {
		if (x >= 5u && x < 11u) {
			if (y == 0u || y == 7u || x == 5u || x == 10u) return 13u; // pelo/sombrero
			return 15u;                                              // cara
		}
		return 0u;
	}
	// Torso (filas 8..16).
	if (y < 17u) {
		if (x >= 5u && x < 11u) return 14u; // camiseta
		if (x == 4u || x == 11u) return (y < 12u) ? u8 {15u} : u8 {14u}; // brazos
		return 0u;
	}
	// Piernas (filas 17..23): zancada alterna.
	const bool left_forward = step;
	if (y < 22u) {
		if (x >= 5u && x < 8u) return left_forward ? u8 {14u} : u8 {13u};
		if (x >= 8u && x < 11u) return left_forward ? u8 {13u} : u8 {14u};
		return 0u;
	}
	// Pies.
	if (y < 24u) {
		if ((x >= 4u && x < 8u)) return left_forward ? u8 {11u} : u8 {13u};
		if ((x >= 8u && x < 12u)) return left_forward ? u8 {13u} : u8 {11u};
		return 0u;
	}
	return 0u;
}

constexpr auto kActorIdle = make_sheet<kActorW, kActorH>([](u16 y, u16 x) {
	return actor_pixel(y, x, false);
});
constexpr auto kActorStepA = make_sheet<kActorW, kActorH>([](u16 y, u16 x) {
	return actor_pixel(y, x, true);
});
constexpr auto kActorMask = make_mask<kActorW, kActorH>([](u16 y, u16 x) {
	return actor_pixel(y, x, false);
});

/// Puntero del mouse (flecha 10x11, arte a la izquierda con guarda).
[[nodiscard]] constexpr u8 ptr_pixel(u16 y, u16 x) noexcept {
	static constexpr u8 kRows[11] = {0b10000000u, 0b11000000u, 0b11100000u, 0b11110000u, 0b11111000u,
					 0b11111100u, 0b11111110u, 0b11100000u, 0b11011000u, 0b00001100u,
					 0b00001100u};
	if (x >= 8u || y >= 11u) return 0u;
	const u8 row = kRows[y];
	return ((row >> (7u - x)) & 1u) != 0u ? static_cast<u8>((x < 1u || y < 1u) ? 0u : 15u) : 0u;
}
constexpr auto kPtrSheet = make_sheet<kPtrW, kPtrH>([](u16 y, u16 x) {
	return ptr_pixel(y, x);
});
constexpr auto kPtrMask = make_mask<kPtrW, kPtrH>([](u16 y, u16 x) {
	return ptr_pixel(y, x);
});

/// Decimal `v` en `out` (NUL incluido); devuelve caracteres escritos.
u8 u16_to_str(u16 v, char* out) {
	char tmp[6] {};
	u8 n = 0u;
	do {
		tmp[n++] = static_cast<char>('0' + static_cast<u8>(v % 10u));
		v = static_cast<u16>(v / 10u);
	} while (v != 0u && n < 5u);
	for (u8 i = 0u; i < n; ++i) out[i] = tmp[n - 1u - i];
	out[n] = '\0';
	return n;
}

u8 str_cat(char* dst, u8 len, const char* src) {
	for (u8 i = 0u; src[i] != '\0' && len < 60u; ++i) dst[len++] = src[i];
	dst[len] = '\0';
	return len;
}

void pad_to(char* text, u8 len, u8 width) {
	while (len < width && len < 62u) text[len++] = ' ';
	text[len] = '\0';
}

// --- Contenido (walkboxes y hotspots propios, sin material del juego) ----------------

using WbS = eng::adv::Walkbox<s16>;
using RouteScratchS = eng::adv::RouteScratch<s16>;

constexpr WbS kBoxes[3] = {
	WbS {{32, 150}, {160, 150}, {160, 196}, {32, 196}},   // playa oeste (muelle)
	WbS {{160, 140}, {272, 140}, {272, 196}, {160, 196}}, // playa central
	WbS {{272, 150}, {344, 150}, {344, 196}, {272, 196}}, // playa este (puerta)
};
constexpr eng::u16 kBoxCount = 3u;

struct Hotspot {
	s16 x0, y0, x1, y1;
	s16 target_x, target_y;
	const char* label;
};

constexpr Hotspot kHotspots[2] = {
	{288, 112, 318, 164, 302, 156, "PUERTA"},
	{118, 138, 152, 168, 135, 172, "CARTEL"},
};
constexpr eng::u8 kHotspotCount = 2u;

/// Guion determinista de paseo (destinos de mundo; se repite en bucle).
constexpr s16 kTour[][2] = {{302, 158}, {262, 172}, {133, 172}, {210, 186}, {332, 186}, {64, 162}};
constexpr eng::u8 kTourCount = static_cast<eng::u8>(sizeof(kTour) / sizeof(kTour[0]));

// --- Bob (job por plano) --------------------------------------------------------------

struct Bob {
	const u16* sheet = nullptr;
	const u16* mask = nullptr;
	u16 w = 0u;
	u16 h = 0u;
	s16 x = 0; // world (para el destino del blit se suma kMarginX)
	s16 y = 0;
	bool dirty = true; // hay que restaurar la region anterior y redibujar
	s16 px = -1;       // region dibujada (coordenada de bitmap, alineada a 16)
	s16 py = -1;

	[[nodiscard]] constexpr u16 words() const noexcept { return static_cast<u16>(w / 16u); }
	[[nodiscard]] constexpr u32 stash_bytes() const noexcept {
		return static_cast<u32>(words()) * 2u * h * kPlanes;
	}
};

struct AdvWalkDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
				if (!backend.configure_memory({160u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		m_hud = backend.memory_manager().chip().reserve<eng::PlaneTag>(kHudBytes + 2u, 16);
		if (!m_hud.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootHud);
			return;
		}
		(void)backend.blitter_clear(m_hud.view.subspan(2u, kHudBytes), 1u, 44u, kHudBytes, 352u,
					    kHudLines);
		(void)m_hud_pf.bind_raw(m_hud.view.data() + 2u, kHudBytes, 352u, kHudLines, 1u, kHudBytes);

		// El arte de los bobs vive en ROM (constexpr) pero el Blitter lo lee por el canal A/B:
		// hay que copiarlo a Chip (mismo motivo que la demo 050).
		m_art_idle = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kActorW / 16u) * 2u * kActorH * kPlanes, 16);
		m_art_step = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kActorW / 16u) * 2u * kActorH * kPlanes, 16);
		m_art_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kActorW / 16u) * 2u * kActorH, 16);
		m_ptr_art = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH * kPlanes, 16);
		m_ptr_art_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH, 16);
		if (!m_art_idle.valid() || !m_art_step.valid() || !m_art_mask.valid() || !m_ptr_art.valid() ||
		    !m_ptr_art_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootArt);
			return;
		}
		// Copia tipada (vista por palabras): el Blitter solo lee Chip, el arte vive en ROM.
		{
			const eng::Words<eng::MaskTag> idle = m_art_idle.view.as_words();
			const eng::Words<eng::MaskTag> step = m_art_step.view.as_words();
			const eng::Words<eng::MaskTag> mask = m_art_mask.view.as_words();
			const eng::Words<eng::MaskTag> part = m_ptr_art.view.as_words();
			const eng::Words<eng::MaskTag> pmask = m_ptr_art_mask.view.as_words();
			for (u32 i = 0u; i < kActorIdle.size(); ++i) {
				idle[i] = kActorIdle[i];
				step[i] = kActorStepA[i];
			}
			for (u32 i = 0u; i < kActorMask.size(); ++i) mask[i] = kActorMask[i];
			for (u32 i = 0u; i < kPtrSheet.size(); ++i) part[i] = kPtrSheet[i];
			for (u32 i = 0u; i < kPtrMask.size(); ++i) pmask[i] = kPtrMask[i];
		}
		m_actor.sheet = reinterpret_cast<const u16*>(m_art_idle.data());
		m_actor.mask = reinterpret_cast<const u16*>(m_art_mask.data());
		m_ptr.sheet = reinterpret_cast<const u16*>(m_ptr_art.data());
		m_ptr.mask = reinterpret_cast<const u16*>(m_ptr_art_mask.data());
		m_actor_stash = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kActorW / 16u) * 2u * kActorH * kPlanes, 16);
		m_ptr_stash = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH * kPlanes, 16);
		if (!m_actor_stash.valid() || !m_ptr_stash.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootStash);
			return;
		}

		scene::SceneResources res {};
		res.width = kBitmapW;
		res.height = kFbH + kHudLines;
		res.rows = kFbH;
		res.planes = kPlanes;
		res.buffers = 1u; // fondo estatico: un solo buffer
		res.copper_bytes = 4096u;
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::palette(kPalette, 0u, 16u),
				    [this](scene::Scene& sc) { emit_display(sc); })) {
			// El motivo del rechazo viaja en el detalle para el diagnostico (config_error).
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<eng::u32>(m_scene.config_error().code));
			return;
		}
		paint_backdrop();
		draw_hud_static();
		m_scene.takeover(backend);
		m_cam.set_bounds({0, 0}, {kCamMaxX, 0});
		m_cam.place({0, 0});
		m_ready = true;
		set_destination(kTour[0][0], kTour[0][1], true);
		// Secciones del perfilador (`ENG_PROF_BEGIN/END`): 4 (Frame/Blits/Hud/Dbg). Sin el
		// `INIT` las muestras se acumulan igual, pero el fichero de perfil no las atribuye.
		ENG_PROF_INIT(4u);
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready || !m_scene.ok()) return;
#if defined(ADVWALK_NO_UPDATE)
		(void)backend;
		(void)context;
		return;
#endif
		ENG_PROF_FRAME();
		ENG_PROF_BEGIN(kProfFrame);

		// --- Entrada: puntero software + click ---
		eng::amiga::MousePollState mps = m_mouse_poll;
		eng::input::MouseState mouse {};
		eng::amiga::poll_mouse(mouse, mps);
		m_mouse_poll = mps;
		s16 ptr_x = static_cast<s16>(m_ptr_x + mouse.dx);
		s16 ptr_y = static_cast<s16>(m_ptr_y + mouse.dy);
		if (ptr_x < 0) ptr_x = 0;
		if (ptr_x > static_cast<s16>(kViewportW - 1u)) ptr_x = static_cast<s16>(kViewportW - 1u);
		if (ptr_y < 0) ptr_y = 0;
		if (ptr_y > static_cast<s16>(kFbH - 1u)) ptr_y = static_cast<s16>(kFbH - 1u);
		if (ptr_x != m_ptr_x || ptr_y != m_ptr_y) {
			m_ptr_x = ptr_x;
			m_ptr_y = ptr_y;
			m_ptr.dirty = true;
		}

		// --- Guion determinista (si no hay clicks) ---
		const u32 frame = context.frame.frame_index;
		m_frame_now = frame;
		if (mouse.left_button && !m_prev_click) {
			const s16 wx = static_cast<s16>(m_cam_int + m_ptr_x);
			const s16 wy = m_ptr_y;
			if (set_destination(wx, wy, false)) m_wait_until = frame + 10u;
		}
		m_prev_click = mouse.left_button;

		// --- Avance del actor por la ruta ---
		ENG_PROF_BEGIN(kProfBlits);
		if (m_walking) {
			advance_actor();
			m_actor.dirty = true;
			m_walk_timer++;
		} else if (frame >= m_wait_until) {
			m_tour = static_cast<eng::u8>((m_tour + 1u) % kTourCount);
			set_destination(kTour[m_tour][0], kTour[m_tour][1], true);
		}

		// --- Camara de mundo (dead-zone) y ventana de display ---
		m_cam.follow({eng::math::Fixed<s16, 4> {static_cast<s16>(m_actor.x << 4)},
			      eng::math::Fixed<s16, 4> {static_cast<s16>(m_actor.y << 4)}});
		m_cam.set_soft_bounds(
			{eng::math::Fixed<s16, 4> {static_cast<s16>((m_cam_int + 60) << 4)},
			 eng::math::Fixed<s16, 4> {static_cast<s16>(40 << 4)}},
			{eng::math::Fixed<s16, 4> {static_cast<s16>((m_cam_int + 260) << 4)},
			 eng::math::Fixed<s16, 4> {static_cast<s16>(180 << 4)}});
		m_cam.update();
		const s16 cam_new = static_cast<s16>(m_cam.pos.v[0].v >> 4);
		if (cam_new != m_cam_int) {
			m_cam_int = cam_new;
			m_ptr.dirty = true; // el puntero vive en pantalla: la camara lo arrastra
		}
		// El puntero es un bob mas: su coord de bitmap es mundo = camara + pantalla.
		m_ptr.x = static_cast<s16>(m_cam_int + m_ptr_x);
		m_ptr.y = m_ptr_y;

		// --- Blits: restaurar + stash + dibujar (solo el bob que cambio) ---
#if !defined(ADVWALK_NO_BLITS)
		if (m_actor.dirty) {
			blit_restore(backend, m_actor, m_actor_stash);
			blit_stash(backend, m_actor, m_actor_stash);
			blit_draw(backend, m_actor,
				  (m_walking && (m_walk_timer & 8u) != 0u)
					  ? reinterpret_cast<const u16*>(m_art_step.data())
					  : reinterpret_cast<const u16*>(m_art_idle.data()));
			m_actor.dirty = false;
		}
		if (m_ptr.dirty) {
			blit_restore(backend, m_ptr, m_ptr_stash);
			blit_stash(backend, m_ptr, m_ptr_stash);
			blit_draw(backend, m_ptr, m_ptr.sheet);
			m_ptr.dirty = false;
		}
#endif
		ENG_PROF_END(kProfBlits);

		// --- Ventana de display (parche del Copper) ---
		patch_window(static_cast<u16>(m_cam_int + kMarginX));

		ENG_PROF_BEGIN(kProfHud);
#if !defined(ADVWALK_NO_HUD)
		draw_hud_dynamic(frame);
#endif
		ENG_PROF_END(kProfHud);
		ENG_PROF_END(kProfFrame);

		g_eng_run_status.detail = (static_cast<u32>(static_cast<u16>(m_cam_int)) << 16u) |
					  (static_cast<u32>(m_walking ? 1u : 0u) << 15u) |
					  static_cast<u32>(static_cast<eng::u8>(m_wp_count - m_wp));
		eng::debug::probe_when_ready(g_eng_run_status, frame);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	// --- Display (una vez) ---
	void emit_display(scene::Scene& sc) {
		auto& s = sc.scheduler();
		s.move(eng::copper::Register::DMACON,
		       static_cast<u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
					eng::copper::DmaCopper | eng::copper::DmaBitplane));
		s.move(eng::copper::Register::BPLCON0, scene::bplcon0_for(scene::SceneMode::Standard, kPlanes));
		m_bplcon1_slot = s.move_at(eng::copper::Register::BPLCON1, 0x0000u);
		s.move(eng::copper::Register::BPLCON2, 0x0000u);
#if defined(ADVWALK_PLAIN_DDF)
		// Variante de diagnostico: ventana de 320 estandar ($38..$d0) y MOD de fila (46-42).
		s.move(eng::copper::Register::BPL1MOD, 4u);
		s.move(eng::copper::Register::BPL2MOD, 4u);
#else
		s.move(eng::copper::Register::BPL1MOD, static_cast<u16>(kPlaneMod));
		s.move(eng::copper::Register::BPL2MOD, static_cast<u16>(kPlaneMod));
#endif
		s.move(eng::copper::Register::DIWSTRT, kDiwstrt);
		s.move(eng::copper::Register::DIWSTOP, kDiwstop);
#if defined(ADVWALK_PLAIN_DDF)
		s.move(eng::copper::Register::DDFSTRT, 0x0038u);
#else
		s.move(eng::copper::Register::DDFSTRT, kDdfstrt);
#endif
		s.move(eng::copper::Register::DDFSTOP, kDdfstop);
		const eng::ChipPlaneView planes = sc.chip_planes();
		for (u8 p = 0u; p < kPlanes; ++p) {
			const eng::Address<eng::MemoryKind::Chip> ip =
				planes.address(static_cast<s32>(p) * sc.plane_bytes());
			m_plane_hi[p] = s.move_at(eng::copper::bitplane_pointer_high_register(p),
						  static_cast<u16>(ip.value >> 16u));
			m_plane_lo[p] = s.move_at(eng::copper::bitplane_pointer_low_register(p),
						  static_cast<u16>(ip.value & 0xffffu));
			sc.set_plane_patch(p, scene::Patch32 {m_plane_hi[p], m_plane_lo[p]});
		}
#if !defined(ADVWALK_NO_BAND)
		s.wait_line(static_cast<u8>(kHudLine));
		// La banda es un playfield propio: MODs a 0 (heredar el del fino desliza las filas) y
		// BPLCON1 a 0 (el retardo fino del mundo desplazaria cada fila circularmente).
		s.move(eng::copper::Register::BPL1MOD, 0x0000u);
		s.move(eng::copper::Register::BPL2MOD, 0x0000u);
		s.move(eng::copper::Register::BPLCON1, 0x0000u);
		// La banda de 352 px (22 palabras) fija su propio DDFSTOP; el fetch de 42 B del
		// mundo dejaria la ultima palabra de cada fila sin refrescar.
		s.move(eng::copper::Register::DDFSTOP, 0x00d8u);
		s.move(eng::copper::Register::BPLCON0, scene::bplcon0_for(scene::SceneMode::Standard, 1u));
		s.move(eng::copper::Register::BPLCON1, 0x0000u);
		s.move(eng::copper::color_register(0u), kHudColor0);
		s.move(eng::copper::color_register(1u), kHudColor1);
		s.move_bitplane_pointer(0u, m_hud.mem_view().address(2));
#endif
	}

	/// Parchea BPLxPT (coarse) y BPLCON1 (fino) para mostrar la ventana en `s`.
	void patch_window(u16 s) {
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		const u32 coarse_bytes = static_cast<u32>(eng::graphics::fine_scroll_coarse(s)) >> 3u;
		eng::Span<u16> words = m_scene.plan().active_span();
		for (u8 p = 0u; p < kPlanes; ++p) {
			const u8* pc = planes.data() + static_cast<u32>(p) * kSceneBytes + coarse_bytes;
			const u32 addr = eng::Address<eng::MemoryKind::Chip>::from_storage(pc).value;
			eng::copper::patch_data(words, m_plane_hi[p], static_cast<u16>(addr >> 16u));
			eng::copper::patch_data(words, m_plane_lo[p], static_cast<u16>(addr & 0xffffu));
		}
		const u16 fine = eng::graphics::fine_delay(s);
		eng::copper::patch_data(words, m_bplcon1_slot, static_cast<u16>(fine | (fine << 4u)));
	}

	// --- Backdrop (CPU, una vez) ---
	void paint_backdrop() {
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		for (u16 y = 0u; y < kFbH; ++y) {
			for (u8 p = 0u; p < kPlanes; ++p) {
				u16* row = reinterpret_cast<u16*>(
					const_cast<u8*>(planes.data()) + static_cast<u32>(p) * kSceneBytes +
					static_cast<u32>(y) * kRowBytes);
				for (u16 wx = 0u; wx < kRowBytes / 2u; ++wx) {
					u16 bits = 0u;
					for (u16 i = 0u; i < 16u; ++i) {
						const u8 c = backdrop(static_cast<u16>(wx * 16u + i), y);
						if ((c & static_cast<u8>(1u << p)) != 0u) {
							bits = static_cast<u16>(bits | static_cast<u16>(0x8000u >> i));
						}
					}
					row[wx] = bits;
				}
			}
		}
	}

	// --- Bobs ---
	void blit_restore(eng::amiga::AmigaBackend& backend, Bob& b, eng::Block<eng::MaskTag, eng::MemoryKind::Chip>& stash) {
		if (b.px < 0) return;
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		const u16 words = b.words();
		eng::graphics::BlitJob j {};
		j.kind = eng::graphics::BlitJobKind::CopyRect;
		j.source = eng::graphics::BlitPtr::from_storage(stash.view.as_words().data());
		j.destination = eng::graphics::BlitPtr::from_storage(reinterpret_cast<u16*>(
			const_cast<u8*>(planes.data()) + static_cast<u32>(b.py) * kRowBytes +
			static_cast<u32>(b.px >> 3)));
		j.words_per_row = words;
		j.height = b.h;
		j.source_modulo_bytes = 0;
		j.destination_modulo_bytes = static_cast<s16>(kRowBytes - words * 2u);
		j.bitplane_count = kPlanes;
		j.source_plane_stride_bytes = static_cast<u32>(words) * 2u * b.h;
		j.destination_plane_stride_bytes = kSceneBytes;
		if (!backend.blitter_submit(j, true)) m_blit_fail++;
		b.px = -1;
	}

	void blit_stash(eng::amiga::AmigaBackend& backend, Bob& b, eng::Block<eng::MaskTag, eng::MemoryKind::Chip>& stash) {
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		const u16 words = b.words();
		// **Clamp de la ventana del bob al bitmap**: la ventana son `words` palabras (32 px
		// con la guarda) x `h` filas y el Blitter escribe el rectangulo COMPLETO (donde la
		// mascara vale 0 reescribe D); sin este clamp, cerca del borde inferior/derecho del
		// bitmap la ventana sale del plano (rebasa al plano siguiente / al bloque vecino):
		// corrupcion intermitente y bus error (CPU parada). El clamp se aplica a la region
		// de STASH, y `px/py` son los que usan restore y draw (misma ventana siempre).
		s16 px = static_cast<s16>((b.x + kMarginX) & ~15);
		const s16 px_max = static_cast<s16>(kBitmapW - words * 16u);
		if (px > px_max) px = px_max;
		if (px < 0) px = 0;
		s16 py = b.y;
		if (py < 0) py = 0;
		if (py + static_cast<s16>(b.h) > static_cast<s16>(kFbH)) {
			py = static_cast<s16>(kFbH - b.h);
		}
		eng::graphics::BlitJob j {};
		j.kind = eng::graphics::BlitJobKind::CopyRect;
		j.source = eng::graphics::BlitPtr::from_storage(reinterpret_cast<u16*>(
			const_cast<u8*>(planes.data()) + static_cast<u32>(py) * kRowBytes +
			static_cast<u32>(px >> 3)));
		j.destination = eng::graphics::BlitPtr::from_storage(stash.view.as_words().data());
		j.words_per_row = words;
		j.height = b.h;
		j.source_modulo_bytes = static_cast<s16>(kRowBytes - words * 2u);
		j.destination_modulo_bytes = 0;
		j.bitplane_count = kPlanes;
		j.source_plane_stride_bytes = kSceneBytes;
		j.destination_plane_stride_bytes = static_cast<u32>(words) * 2u * b.h;
		if (!backend.blitter_submit(j, true)) m_blit_fail++;
		b.px = px;
		b.py = py;
	}

	void blit_draw(eng::amiga::AmigaBackend& backend, const Bob& b, const u16* sheet) {
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		const u16 words = b.words();
		eng::graphics::BlitJob j {};
		j.kind = eng::graphics::BlitJobKind::MaskedBlobNoSave; // mascara explicita, sin cookie-cut BLTBDAT
		j.minterm = 0xCAu;
		j.mask = eng::graphics::BlitPtr::from_storage(b.mask);
		j.source = eng::graphics::BlitPtr::from_storage(sheet);
		j.destination = eng::graphics::BlitPtr::from_storage(reinterpret_cast<u16*>(
			const_cast<u8*>(planes.data()) + static_cast<u32>(b.py) * kRowBytes +
			static_cast<u32>(b.px >> 3)));
		j.words_per_row = words;
		j.height = b.h;
		j.source_modulo_bytes = 0;
		j.destination_modulo_bytes = static_cast<s16>(kRowBytes - words * 2u);
		j.bitplane_count = kPlanes;
		j.source_plane_stride_bytes = static_cast<u32>(words) * 2u * b.h;
		j.destination_plane_stride_bytes = kSceneBytes;
		j.source_shift = static_cast<u8>((b.x + kMarginX) & 15);
		if (!backend.blitter_submit(j, true)) m_blit_fail++;
	}

	// --- Ruta y avance ---
	bool set_destination(s16 wx, s16 wy, bool scripted) {
		const eng::math::Vec<2, s16> from {{m_actor.x, m_actor.y}};
		const eng::math::Vec<2, s16> to {{wx, wy}};
		RouteScratchS scratch {};
		scratch.best = eng::Span<eng::adv::walkbox_dist_t<s16>> {m_best, kBoxCount};
		scratch.prev = eng::Span<eng::u16> {m_prev, kBoxCount};
		scratch.state = eng::Span<eng::u8> {m_state, kBoxCount};
		eng::Span<eng::math::Vec<2, s16>> out {m_route, kMaxWaypoints};
		const eng::usize n = eng::adv::walkbox_route(eng::Span<const WbS> {kBoxes, kBoxCount}, from,
							     to, static_cast<s16>(2), scratch, out);
		if (n == 0) return false;
		m_wp_count = static_cast<eng::u8>(n);
		m_wp = 0u;
		m_walking = true;
		m_scripted = scripted;
		return true;
	}

	void advance_actor() {
		if (m_wp >= m_wp_count) {
			m_walking = false;
			return;
		}
		const s16 tx = m_route[m_wp].v[0];
		const s16 ty = m_route[m_wp].v[1];
		s32 dx = static_cast<s32>(tx) - static_cast<s32>(m_actor.x);
		s32 dy = static_cast<s32>(ty) - static_cast<s32>(m_actor.y);
		if (dx == 0 && dy == 0) {
			m_wp++;
			if (m_wp >= m_wp_count) {
				m_walking = false;
				m_wait_until = m_scripted ? m_frame_now + 60u : m_frame_now + 10u;
			}
			return;
		}
		// Bresenham a kActorSpeed px: sin multiplicaciones/divisiones por frame.
		for (u8 step = 0u; step < kActorSpeed; ++step) {
			dx = static_cast<s32>(tx) - static_cast<s32>(m_actor.x);
			dy = static_cast<s32>(ty) - static_cast<s32>(m_actor.y);
			if (dx == 0 && dy == 0) break;
			const s32 adx = dx < 0 ? -dx : dx;
			const s32 ady = dy < 0 ? -dy : dy;
			if (adx >= ady) {
				m_actor.x += (dx > 0 ? 1 : -1);
				m_err_y += static_cast<s16>(ady * 2);
				if (m_err_y >= adx) {
					m_err_y -= static_cast<s16>(adx);
					m_actor.y += (dy > 0 ? 1 : -1);
				}
			} else {
				m_actor.y += (dy > 0 ? 1 : -1);
				m_err_x += static_cast<s16>(adx * 2);
				if (m_err_x >= ady) {
					m_err_x -= static_cast<s16>(ady);
					m_actor.x += (dx > 0 ? 1 : -1);
				}
			}
		}
	}

	// --- HUD ---

	void draw_hud_static() {
		playfield::Surface hud {m_hud_pf, playfield::SurfaceRect {0, 0, 320u, kHudLines}};
		hud.fill_rect(0, 0, 352, static_cast<s16>(kHudLines), 0u);
		char label[40] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		(void)hud.draw_text_words(16, 2, "001 ADV WALK", 1u);
		(void)hud.draw_text_words(16, 12, label, 1u);
	}

	void draw_hud_dynamic(u32 frame) {
		playfield::Surface hud {m_hud_pf, playfield::SurfaceRect {0, 0, 320u, kHudLines}};
		char buf[8] {};
		char text[64] {};
		u8 m = 0u;
		m = str_cat(text, m, "FRM ");
		(void)u16_to_str(static_cast<u16>(frame), buf);
		m = str_cat(text, m, buf);
		m = str_cat(text, m, " CAM ");
		(void)u16_to_str(static_cast<u16>(m_cam_int), buf);
		m = str_cat(text, m, buf);
		m = str_cat(text, m, " WP ");
		(void)u16_to_str(static_cast<u16>(m_wp), buf);
		m = str_cat(text, m, buf);
		m = str_cat(text, m, "/");
		(void)u16_to_str(static_cast<u16>(m_wp_count), buf);
		m = str_cat(text, m, buf);
		m = str_cat(text, m, " BF ");
		(void)u16_to_str(static_cast<u16>(m_blit_fail), buf);
		m = str_cat(text, m, buf);
		pad_to(text, m, kHudCols);
		(void)hud.draw_text_words(16, 22, text, 1u);

		// Etiqueta del hotspot bajo el puntero (o el estado del paseo).
		char text2[64] {};
		u8 n = 0u;
		const s16 wx = static_cast<s16>(m_cam_int + m_ptr_x);
		const char* label = nullptr;
		for (u8 i = 0u; i < kHotspotCount; ++i) {
			if (wx >= kHotspots[i].x0 && wx < kHotspots[i].x1 && m_ptr_y >= kHotspots[i].y0 &&
			    m_ptr_y < kHotspots[i].y1) {
				label = kHotspots[i].label;
				break;
			}
		}
		if (label != nullptr) {
			n = str_cat(text2, n, "MIRA: ");
			n = str_cat(text2, n, label);
		} else {
			n = str_cat(text2, n, m_walking ? "PASEO..." : "CLICK: DESTINO   ");
			if (!m_walking && m_wait_until > 0u) {
				n = str_cat(text2, n, " (GUION)");
			}
		}
		pad_to(text2, n, kHudCols);
		(void)hud.draw_text_words(16, 32, text2, 1u);
	}

	// --- Estado ---
	static constexpr eng::u8 kMaxWaypoints = 24u;
	bool m_ready = false;
	scene::Scene m_scene {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_hud {};
	playfield::ContiguousPlayfield m_hud_pf {};
	eng::copper::MoveSlot m_bplcon1_slot {};
	eng::copper::MoveSlot m_plane_hi[kPlanes] {};
	eng::copper::MoveSlot m_plane_lo[kPlanes] {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_art_idle {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_art_step {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_art_mask {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_ptr_art {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_ptr_art_mask {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_actor_stash {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_ptr_stash {};

	eng::adv::Camera<eng::math::Fixed<s16, 4>> m_cam {};
	s16 m_cam_int = 0;

	Bob m_actor {nullptr, nullptr, kActorW, kActorH, 100, 180, true, -1, -1};
	Bob m_ptr {nullptr, nullptr, kPtrW, kPtrH, 0, 0, true, -1, -1};

	eng::math::Vec<2, s16> m_route[kMaxWaypoints] {};
	eng::u8 m_wp = 0u;
	eng::u8 m_wp_count = 0u;
	bool m_walking = false;
	bool m_scripted = true;
	eng::u8 m_tour = 1u;
	u32 m_wait_until = 0u;
	u32 m_frame_now = 0u;
	s16 m_err_x = 0;
	s16 m_err_y = 0;
	u32 m_walk_timer = 0u;
	u32 m_blit_fail = 0u;

	// Router: buffers de trabajo (sin heap).
	eng::adv::walkbox_dist_t<s16> m_best[kBoxCount] {};
	eng::u16 m_prev[kBoxCount] {};
	eng::u8 m_state[kBoxCount] {};

	s16 m_ptr_x = 160;
	s16 m_ptr_y = 100;
	eng::amiga::MousePollState m_mouse_poll {};
	bool m_prev_click = false;
};

AdvWalkDemo g_game {};

} // namespace

int main() {
	eng::amiga::ensure_boot();
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	eng::Engine engine {backend, g_game};
	// Mini-SO: el latido de VBlank por IRQ + senal es lo que deja al bucle dormir en `Wait()`
	// (sin esto `run_frames` no duerme, la CPU gira y el frame pierde cadencia).
	(void)eng::os::init(engine, 0u);
	// **Servicio de Blitter de fondo OFF**: la demo lanza blits con `blitter_submit(j, true)`
	// síncrono; el servicio de fondo (IRQ) usa el mismo slot y compite por los registros del
	// Blitter entre dos jobs del mismo frame (carreras: filas del save-under perdidas -> estela
	// en el fondo y bloqueos intermitentes). El slot es exclusivo por contrato (ver
	// `engine.hpp`, `set_blit_service_enabled`).
	engine.set_blit_service_enabled(false);
	engine.run_frames(0xffffu);
	return 0;
}
