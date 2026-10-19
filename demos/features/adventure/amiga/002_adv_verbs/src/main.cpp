// ============================================================================
// Demo 002 — Verbos e interacción (A1 de la capa de aventura).
// ============================================================================
//
// Sala única con mundo desplazable (misma técnica que `001_adv_walk`): walkboxes,
// cámara con paneo real, actor que camina y click-to-walk. Sobre esa base, el hito A1:
//
//   - barra de verbos clásica en la franja inferior (contenido `eng::adv::VerbSet`: el
//     motor no conoce la tabla),
//   - frase en construcción (verbo + sujeto + objetivo) y réplicas en cola
//     (`eng::adv::SayQueue`, duración exacta por VBlank),
//   - hotspots de sala con prioridad GUI -> hotspot -> suelo (`eng::adv::pick_pointer`) y
//     **hit-test del HUD** con el modelo `eng::hud` (dos rejillas: verbos e inventario),
//   - objetos del kernel `eng::rpg`: llave (etiqueta `key`) y moneda en el suelo
//     (`GroundItems`), bolsa (`ItemBag`) y cerradura de la puerta (`LockSet`), con el
//     inventario pintado como **proyección** de la bolsa a slots del HUD,
//   - cursores por verbo (flecha / ojo / mano) según `CursorMode` del verbo activo.
//
// Todo el trabajo de frame es barato por diseño (HUD 1bpp, texto por palabras con
// `DiffText`, blits con el servicio de Blitter desactivado): el objetivo es 1 frame por
// VBlank. La medición y la validación van en `VALIDATION.md`; la prueba del movimiento,
// en `src/VISION.md`.
//
// Regla de la demo (tutorial): la lógica usa la fachada y las capas del engine
// (`eng::adv`, `eng::hud`, `eng::rpg`); no hay registros ni punteros crudos fuera del
// pegamento de presentación (copias a Chip/Blitter del planificador).

#include <eng/adv/camera.hpp>
#include <eng/adv/navigation.hpp>
#include <eng/adv/say.hpp>
#include <eng/adv/types.hpp>
#include <eng/adv/verb.hpp>
#include <eng/adv/walkbox.hpp>
#include <eng/api/api.hpp>
#include <eng/core/math/fixed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/diff_text.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/hud/input.hpp>
#include <eng/hud/model.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/input_poll.hpp>
#include <eng/rpg/interaction.hpp>
#include <eng/rpg/inventory.hpp>
#include <eng/rpg/items.hpp>

#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;
using eng::adv::PointerHit;
using eng::adv::PointerKind;

// --- Geometria del display ----------------------------------------------------------
// Igual que 001: bitmap de 368 px (mundo 352 + 16 de margen delantero para el fino),
// 208 filas de mundo + 48 de franja. La franja es la MISMA region HUD del modelo.
constexpr u16 kWorldW = 352u;
constexpr u16 kMarginX = 16u;
constexpr u16 kBitmapW = kWorldW + kMarginX;
constexpr u16 kFbH = 208u;
constexpr u16 kRowBytes = kBitmapW / 8u;
constexpr u16 kViewportW = 320u;
constexpr u8 kPlanes = 4u;
constexpr u16 kSceneBytes = kRowBytes * kFbH;
constexpr u16 kDdfstrt = eng::graphics::fine_scroll_ddfstrt; // $30
constexpr u16 kFetchBytes = 2u + 40u;
constexpr s16 kPlaneMod = static_cast<s16>(kRowBytes - kFetchBytes);
constexpr u16 kDdfstop = 0x00d0u;
constexpr u16 kDiwstrt = 0x2c81u;
constexpr u16 kDiwstop = 0x2cc1u;

// Franja HUD: 48 lineas (208..255), 1 plano, 352 px (fetch $30..$d8).
constexpr u16 kHudLines = 48u;
constexpr u16 kHudLine = 44u + kFbH;
constexpr u16 kHudBytes = 44u * kHudLines;
constexpr u16 kHudColor0 = 0x011u;
constexpr u16 kHudColor1 = 0xfffu;
constexpr u16 kHudW = 352u;
constexpr u8 kTextX = 16u;            // x de las lineas de texto (bitmap)
constexpr u8 kHudCols = 38u;          // columnas de la frase/replica (38x8 = 304 px)
constexpr u16 kHudTopScreen = kFbH;   // la franja empieza en la fila 208 de pantalla

// Rejilla de VERBOS: 2 filas x 5 columnas, celdas de 64x10 (paso 64/10) en (16,18).
constexpr u8 kVerbCols = 5u;
constexpr u8 kVerbRows = 2u;
constexpr u8 kVerbPitchX = 64u;
constexpr u8 kVerbPitchY = 10u;
constexpr s16 kVerbAreaX = 16;
constexpr s16 kVerbAreaY = 18;

// Rejilla de INVENTARIO: 1 fila x 2 slots de 96x10 en (16,36).
constexpr u8 kInvCols = 2u;
constexpr s16 kInvAreaX = 16;
constexpr s16 kInvAreaY = 36;
constexpr u8 kInvPitchX = 96u;

// Camara y actor (identicos a 001).
constexpr s16 kCamMaxX = static_cast<s16>(kWorldW - kViewportW);
constexpr u8 kActorSpeed = 2u;
constexpr u8 kActorW = 32u;
constexpr u8 kActorH = 24u;
constexpr u8 kPtrW = 32u;
constexpr u8 kPtrH = 11u;
constexpr u8 kItemW = 16u;  // el arte ocupa la mitad izquierda del bob
constexpr u8 kItemH = 16u;

// --- Paleta (4 planos; la misma de la 001) ------------------------------------------
constexpr eng::Palette32 kPalette {{
	0x000, 0x113, 0x225, 0x448, 0x036, 0x048, 0x26a, 0xfff,
	0xda7, 0xc96, 0xb85, 0x852, 0x999, 0x322, 0xc63, 0xfdb,
}};

constexpr eng::debug::Version kVersion {1u, 0u}; // el build lo inyecta build-demo.sh

enum : eng::u32 {
	kBootMemory = 0x00000101u,
	kBootHud = 0x00000102u,
	kBootStash = 0x00000103u,
	kBootCompose = 0x00000104u,
	kBootArt = 0x00000105u,
	kBootContent = 0x00000106u, ///< catalogo/instancias/bolsa del kernel rpg
};

enum : eng::u8 {
	kProfFrame = 0,
	kProfBlits = 1,
	kProfHud = 2,
	kProfDbg = 3,
};

// --- Contenido de la demo ------------------------------------------------------------

/// Ids de objeto del juego (contenido de la sala; ≤255: viajan en la frase de 8 bits).
enum : eng::u16 {
	kObjDoor = 30u,
	kObjSign = 31u,
	kObjKey = 32u,
	kObjCoin = 33u,
	kObjFloor = 0xffffu, // hit-test sin objeto (suelo)
};

/// Tabla de textos (contenido): la cola de replicas transporta el id, no el texto.
constexpr const char* kTexts[] = {
	"UNA PUERTA DE ROBLE. ESTA CERRADA.",   // 0
	"UN CARTEL DEL PUERTO.",                // 1
	"UNA LLAVE DE LATON.",                  // 2
	"UNA MONEDA ANTIGUA.",                  // 3
	"HE COGIDO UNA MONEDA.",                // 4
	"UNA LLAVE. ABRIRA ALGO.",              // 5
	"NO PUEDO LLEVARME ESO.",               // 6
	"LA PUERTA YA ESTA ABIERTA.",           // 7
	"ESTA CERRADA. NECESITA UNA LLAVE.",    // 8
	"LA LLAVE GIRA. LA PUERTA SE ABRE.",    // 9
	"LA MONEDA NO ENCAJA EN LA CERRADURA.", // 10
	"EL CARTEL NO TIENE CERRADURA.",        // 11
	"NO PASA NADA.",                        // 12
	"LAS PUERTAS NO HABLAN.",               // 13
	"NO HAY NADA QUE COGER AHI.",           // 14
	"PRIMERO ELIGE UN OBJETO.",             // 15
	"CIERRO LA PUERTA.",                    // 16
	"MEJOR DEJO ESO QUIETO.",               // 17
};
constexpr eng::u8 kTextCount = static_cast<eng::u8>(sizeof(kTexts) / sizeof(kTexts[0]));
static_assert(kTextCount <= 0xffu, "tabla de textos: id de 8 bits");

/// Duracion de las replicas (VBlanks): 120 frames = 2,4 s a 50 Hz.
constexpr u16 kSayFrames = 120u;

// --- Backdrop procedural (identico a 001: misma sala) -------------------------------

[[nodiscard]] constexpr u8 backdrop(u16 bx, u16 y) noexcept {
	const s32 x = static_cast<s32>(bx) - static_cast<s32>(kMarginX);
	if (y < 60u) return (y < 24u) ? u8 {1u} : ((y < 44u) ? u8 {2u} : u8 {3u});
	if (y < 88u) {
		if (x >= 250 && x < 290 && y >= 40 && y < 60) return 7u;
		if (y < 72u && x >= 60 && x < 220 && (y & 3u) < 2u) return 7u;
		return 3u;
	}
	if (y < 128u) {
		if (((y + x / 3) & 7u) == 0u) return 7u;
		return ((y & 1u) != 0u) ? u8 {5u} : u8 {4u};
	}
	if (y < 196u) {
		if (x < -8 || x >= static_cast<s32>(kWorldW) + 8) return 6u;
		if (y < 140u && x > 240 && y - 128 > (x - 240) / 3) return ((x & 3u) == 0u) ? u8 {11u} : u8 {10u};
		if (x >= 40 && x < 200 && y >= 148 && y < 168) {
			if ((x & 7u) == 0u) return 11u;
			return 10u;
		}
		if (x >= 278 && x < 350 && y >= 104 && y < 168) {
			if (x >= 288 && x < 318 && y >= 112 && y < 164) {
				if (x == 302) return 13u;
				return ((x + y) & 1u) != 0u ? u8 {12u} : u8 {13u};
			}
			return ((x ^ y) & 2u) != 0u ? u8 {11u} : u8 {12u};
		}
		if (x >= 118 && x < 152 && y >= 138 && y < 148) return 14u;
		if (x >= 132 && x < 138 && y >= 148 && y < 168) return 11u;
		return ((x + y) & 15u) == 0u ? u8 {10u} : u8 {8u};
	}
	return ((x & 3u) == 0u) ? u8 {9u} : u8 {8u};
}

// --- Arte planar compacto (mismo generador que 001) ---------------------------------

constexpr u16 bob_plane_words(u16 w, u16 h) noexcept { return static_cast<u16>(w / 16u) * h; }

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

[[nodiscard]] constexpr u8 actor_pixel(u16 y, u16 x, bool step) noexcept {
	if (x >= 16u) return 0u;
	if (y < 8u) {
		if (x >= 5u && x < 11u) {
			if (y == 0u || y == 7u || x == 5u || x == 10u) return 13u;
			return 15u;
		}
		return 0u;
	}
	if (y < 17u) {
		if (x >= 5u && x < 11u) return 14u;
		if (x == 4u || x == 11u) return (y < 12u) ? u8 {15u} : u8 {14u};
		return 0u;
	}
	const bool left_forward = step;
	if (y < 22u) {
		if (x >= 5u && x < 8u) return left_forward ? u8 {14u} : u8 {13u};
		if (x >= 8u && x < 11u) return left_forward ? u8 {13u} : u8 {14u};
		return 0u;
	}
	if (x >= 4u && x < 8u) return left_forward ? u8 {11u} : u8 {13u};
	if (x >= 8u && x < 12u) return left_forward ? u8 {13u} : u8 {11u};
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

/// Cursores por verbo: flecha (defecto/andar), ojo (mirar/hablar) y mano (usar/coger...).
/// Tres variantes de 16x11 (arte a la izquierda, guarda a la derecha para el shift).
[[nodiscard]] constexpr u8 cursor_pixel(u16 y, u16 x, u8 kind) noexcept {
	if (x >= 10u) return 0u;
	if (kind == 0u) { // flecha
		static constexpr u8 kRows[11] = {0b10000000u, 0b11000000u, 0b11100000u, 0b11110000u,
						 0b11111000u, 0b11111100u, 0b11111110u, 0b11100000u,
						 0b11011000u, 0b00001100u, 0b00001100u};
		if (x >= 8u || y >= 11u) return 0u;
		const u8 row = kRows[y];
		return ((row >> (7u - x)) & 1u) != 0u ? static_cast<u8>((x < 1u || y < 1u) ? 0u : 15u) : 0u;
	}
	if (kind == 1u) { // ojo (mirar)
		if (y == 2u && x >= 3u && x < 7u) return 15u;
		if ((y == 1u || y == 3u) && (x == 2u || x == 7u)) return 15u;
		if (y >= 2u && y <= 3u && (x == 4u || x == 5u)) return 13u;
		if ((y == 0u || y == 4u) && x >= 3u && x < 7u) return 14u;
		return 0u;
	}
	// mano (usar/coger/abrir)
	if (y >= 2u && y < 8u && x >= 3u && x < 7u) return 15u;
	if (y >= 6u && y < 9u && x >= 2u && x < 8u) return 15u;
	if (y == 9u && x >= 3u && x < 8u) return 14u;
	if (y == 2u && (x == 3u || x == 5u)) return 14u;
	return 0u;
}

constexpr auto kCursorArrow = make_sheet<kPtrW, kPtrH>([](u16 y, u16 x) {
	return cursor_pixel(y, x, 0u);
});
constexpr auto kCursorArrowMask = make_mask<kPtrW, kPtrH>([](u16 y, u16 x) {
	return cursor_pixel(y, x, 0u);
});
constexpr auto kCursorEye = make_sheet<kPtrW, kPtrH>([](u16 y, u16 x) {
	return cursor_pixel(y, x, 1u);
});
constexpr auto kCursorEyeMask = make_mask<kPtrW, kPtrH>([](u16 y, u16 x) {
	return cursor_pixel(y, x, 1u);
});
constexpr auto kCursorHand = make_sheet<kPtrW, kPtrH>([](u16 y, u16 x) {
	return cursor_pixel(y, x, 2u);
});
constexpr auto kCursorHandMask = make_mask<kPtrW, kPtrH>([](u16 y, u16 x) {
	return cursor_pixel(y, x, 2u);
});

/// Llave (16x16, arte 12x10 a la izquierda).
[[nodiscard]] constexpr u8 key_pixel(u16 y, u16 x) noexcept {
	if (x >= 13u) return 0u;
	// Anilla (filas 2..6, cols 1..5).
	if (y >= 2u && y <= 6u && x >= 1u && x <= 5u) {
		if (y == 2u || y == 6u || x == 1u || x == 5u) return 7u;
		return 0u;
	}
	// Cana (filas 4..5).
	if ((y == 4u || y == 5u) && x >= 5u && x < 12u) return 7u;
	// Dientes.
	if (y == 6u && (x == 8u || x == 10u || x == 11u)) return 7u;
	if (y == 7u && (x == 8u || x == 10u)) return 7u;
	return 0u;
}
constexpr auto kKeySheet = make_sheet<kItemW, kItemH>([](u16 y, u16 x) { return key_pixel(y, x); });
constexpr auto kKeyMask = make_mask<kItemW, kItemH>([](u16 y, u16 x) { return key_pixel(y, x); });

/// Moneda (16x16, arte 10x10 a la izquierda).
[[nodiscard]] constexpr u8 coin_pixel(u16 y, u16 x) noexcept {
	if (y >= 12u || x >= 12u) return 0u;
	const s32 dx = static_cast<s32>(x) - 5;
	const s32 dy = static_cast<s32>(y) - 6;
	if (dx * dx + dy * dy > 20) return 0u;
	if (dx * dx + dy * dy > 12) return 9u; // canto
	if (dx * dx + dy * dy > 4) return 7u;  // cara
	return 10u;                            // brillo
}
constexpr auto kCoinSheet = make_sheet<kItemW, kItemH>([](u16 y, u16 x) { return coin_pixel(y, x); });
constexpr auto kCoinMask = make_mask<kItemW, kItemH>([](u16 y, u16 x) { return coin_pixel(y, x); });

/// Puerta ABIERTA: hueco oscuro (color 1; el 0 es transparente en un blob enmascarado) con
/// marco de piedra. Se dibuja como un prop mas (save-under), sin tocar el backdrop: asi el
/// actor y el puntero conservan sus stashes y cerrar la puerta es solo destapar.
constexpr u16 kDoorW = 32u;
constexpr u16 kDoorH = 48u;
[[nodiscard]] constexpr u8 door_open_pixel(u16 y, u16 x) noexcept {
	if (x < 2u || x >= kDoorW - 2u || y < 2u || y >= kDoorH - 2u) return 12u; // marco
	if (((x + y) % 11u) == 0u) return 12u;                                    // sillares al fondo
	return 1u;
}
constexpr auto kDoorOpenSheet = make_sheet<kDoorW, kDoorH>([](u16 y, u16 x) {
	return door_open_pixel(y, x);
});
constexpr auto kDoorOpenMask = make_mask<kDoorW, kDoorH>([](u16 y, u16 x) {
	return door_open_pixel(y, x);
});

// --- Utilidades de texto (como 001) --------------------------------------------------

u8 str_cat(char* dst, u8 len, const char* src) {
	for (u8 i = 0u; src[i] != '\0' && len < 60u; ++i) dst[len++] = src[i];
	dst[len] = '\0';
	return len;
}

void pad_to(char* text, u8 len, u8 width) {
	while (len < width && len < 62u) text[len++] = ' ';
	text[len] = '\0';
}

// --- Contenido de la sala (walkboxes y hotspots) ------------------------------------

using WbS = eng::adv::Walkbox<s16>;
using RouteScratchS = eng::adv::RouteScratch<s16>;

constexpr WbS kBoxes[3] = {
	WbS {{32, 150}, {160, 150}, {160, 196}, {32, 196}},
	WbS {{160, 140}, {272, 140}, {272, 196}, {160, 196}},
	WbS {{272, 150}, {344, 150}, {344, 196}, {272, 196}},
};
constexpr eng::u16 kBoxCount = 3u;

/// Hotspot de sala: region clicable + objetivo de paseo + etiqueta + objeto del juego.
struct HotspotDef {
	s16 x0, y0, x1, y1;
	s16 target_x, target_y;
	const char* label;
	eng::adv::HotspotId id;
};

constexpr HotspotDef kHotspots[4] = {
	{288, 112, 318, 164, 302, 168, "PUERTA", kObjDoor},
	{118, 138, 152, 168, 135, 172, "CARTEL", kObjSign},
	{126, 152, 146, 168, 136, 172, "LLAVE", kObjKey},
	{236, 174, 252, 188, 244, 190, "MONEDA", kObjCoin},
};
constexpr eng::u8 kHotspotCount = 4u;

/// Paseo de atraccion (mientras no hay clicks): los mismos destinos de la 001.
constexpr s16 kTour[][2] = {{302, 176}, {262, 172}, {133, 172}, {210, 186}, {332, 186}, {64, 162}};
constexpr eng::u8 kTourCount = static_cast<eng::u8>(sizeof(kTour) / sizeof(kTour[0]));

// --- Bob (job por plano) --------------------------------------------------------------

struct Bob {
	const u16* sheet = nullptr;
	const u16* mask = nullptr;
	u16 w = 0u;
	u16 h = 0u;
	s16 x = 0; // mundo
	s16 y = 0;
	bool dirty = true;
	s16 px = -1;
	s16 py = -1;

	[[nodiscard]] constexpr u16 words() const noexcept { return static_cast<u16>(w / 16u); }
};

/// Un objeto de suelo dibujado como bob (llave/moneda): arte + hotspot + presencia.
struct GroundProp {
	Bob bob {};
	const u16* sheet = nullptr;
	const u16* mask = nullptr;
	eng::adv::HotspotId id {eng::adv::kInvalidHotspot};
	bool present = false; ///< false tras recogerlo
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> stash {};
};

// --- Contenido de verbos (tabla del juego; el motor solo la consulta) ----------------

constexpr u8 kVerbCapacity = 10u;

enum : u8 {
	kCursorKindArrow = 0u,
	kCursorKindEye = 1u,
	kCursorKindHand = 2u,
};

// --- Demo -----------------------------------------------------------------------------

struct AdvVerbsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!backend.configure_memory({160u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		build_content();
		m_hud = backend.memory_manager().chip().reserve<eng::PlaneTag>(kHudBytes + 2u, 16);
		if (!m_hud.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootHud);
			return;
		}
		(void)backend.blitter_clear(m_hud.view.subspan(2u, kHudBytes), 1u, 44u, kHudBytes, 352u,
					    kHudLines);
		(void)m_hud_pf.bind_raw(m_hud.view.data() + 2u, kHudBytes, 352u, kHudLines, 1u, kHudBytes);

		m_art_idle = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kActorW / 16u) * 2u * kActorH * kPlanes, 16);
		m_art_step = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kActorW / 16u) * 2u * kActorH * kPlanes, 16);
		m_art_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kActorW / 16u) * 2u * kActorH, 16);
		m_cur_arrow = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH * kPlanes, 16);
		m_cur_arrow_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH, 16);
		m_cur_eye = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH * kPlanes, 16);
		m_cur_eye_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH, 16);
		m_cur_hand = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH * kPlanes, 16);
		m_cur_hand_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH, 16);
		m_key_art = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kItemW / 16u) * 2u * kItemH * kPlanes, 16);
		m_key_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kItemW / 16u) * 2u * kItemH, 16);
		m_coin_art = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kItemW / 16u) * 2u * kItemH * kPlanes, 16);
		m_coin_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kItemW / 16u) * 2u * kItemH, 16);
		m_door_art = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kDoorW / 16u) * 2u * kDoorH * kPlanes, 16);
		m_door_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kDoorW / 16u) * 2u * kDoorH, 16);
		if (!m_art_idle.valid() || !m_art_step.valid() || !m_art_mask.valid() ||
		    !m_cur_arrow.valid() || !m_cur_arrow_mask.valid() || !m_cur_eye.valid() ||
		    !m_cur_eye_mask.valid() || !m_cur_hand.valid() || !m_cur_hand_mask.valid() ||
		    !m_key_art.valid() || !m_key_mask.valid() || !m_coin_art.valid() ||
		    !m_coin_mask.valid() || !m_door_art.valid() || !m_door_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootArt);
			return;
		}
		copy_art(m_art_idle, kActorIdle);
		copy_art(m_art_step, kActorStepA);
		copy_art(m_art_mask, kActorMask);
		copy_art(m_cur_arrow, kCursorArrow);
		copy_art(m_cur_arrow_mask, kCursorArrowMask);
		copy_art(m_cur_eye, kCursorEye);
		copy_art(m_cur_eye_mask, kCursorEyeMask);
		copy_art(m_cur_hand, kCursorHand);
		copy_art(m_cur_hand_mask, kCursorHandMask);
		copy_art(m_key_art, kKeySheet);
		copy_art(m_key_mask, kKeyMask);
		copy_art(m_coin_art, kCoinSheet);
		copy_art(m_coin_mask, kCoinMask);
		copy_art(m_door_art, kDoorOpenSheet);
		copy_art(m_door_mask, kDoorOpenMask);

		m_actor.sheet = reinterpret_cast<const u16*>(m_art_idle.data());
		m_actor.mask = reinterpret_cast<const u16*>(m_art_mask.data());
		set_cursor(kCursorKindArrow);

		m_actor_stash = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kActorW / 16u) * 2u * kActorH * kPlanes, 16);
		m_ptr_stash = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kPtrW / 16u) * 2u * kPtrH * kPlanes, 16);
		m_key.stash = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kItemW / 16u) * 2u * kItemH * kPlanes, 16);
		m_coin.stash = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kItemW / 16u) * 2u * kItemH * kPlanes, 16);
		m_door.stash = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kDoorW / 16u) * 2u * kDoorH * kPlanes, 16);
		if (!m_actor_stash.valid() || !m_ptr_stash.valid() || !m_key.stash.valid() ||
		    !m_coin.stash.valid() || !m_door.stash.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootStash);
			return;
		}
		m_key.sheet = reinterpret_cast<const u16*>(m_key_art.data());
		m_key.mask = reinterpret_cast<const u16*>(m_key_mask.data());
		m_key.bob.sheet = m_key.sheet;
		m_key.bob.mask = m_key.mask;
		m_key.bob.w = kItemW;
		m_key.bob.h = kItemH;
		m_key.bob.x = 132;
		m_key.bob.y = 152;
		m_key.id = kObjKey;
		m_key.present = true;
		m_coin.sheet = reinterpret_cast<const u16*>(m_coin_art.data());
		m_coin.mask = reinterpret_cast<const u16*>(m_coin_mask.data());
		m_coin.bob.sheet = m_coin.sheet;
		m_coin.bob.mask = m_coin.mask;
		m_coin.bob.w = kItemW;
		m_coin.bob.h = kItemH;
		m_coin.bob.x = 240;
		m_coin.bob.y = 176;
		m_coin.id = kObjCoin;
		m_coin.present = true;
		m_door.sheet = reinterpret_cast<const u16*>(m_door_art.data());
		m_door.mask = reinterpret_cast<const u16*>(m_door_mask.data());
		m_door.bob.sheet = m_door.sheet;
		m_door.bob.mask = m_door.mask;
		m_door.bob.w = kDoorW;
		m_door.bob.h = kDoorH;
		m_door.bob.x = 288;
		m_door.bob.y = 116;
		m_door.id = kObjDoor;
		m_door.present = false;

		scene::SceneResources res {};
		res.width = kBitmapW;
		res.height = kFbH + kHudLines;
		res.rows = kFbH;
		res.planes = kPlanes;
		res.buffers = 1u;
		res.copper_bytes = 4096u;
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::palette(kPalette, 0u, 16u),
				    [this](scene::Scene& sc) { emit_display(sc); })) {
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<eng::u32>(m_scene.config_error().code));
			return;
		}
		paint_backdrop();
		draw_hud_static();
		m_scene.takeover(backend);
		m_cam.set_bounds({0, 0}, {kCamMaxX, 0});
		m_cam.place({0, 0});
		setup_hud_model();
		m_ready = true;
		set_destination(kTour[0][0], kTour[0][1], true);
		ENG_PROF_INIT(4u);
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready || !m_scene.ok()) return;
		ENG_PROF_FRAME();
		ENG_PROF_BEGIN(kProfFrame);

		// --- Entrada: puntero software + click (franja incluida: y 0..255) ---
		eng::amiga::MousePollState mps = m_mouse_poll;
		eng::input::MouseState mouse {};
		eng::amiga::poll_mouse(mouse, mps);
		m_mouse_poll = mps;
		s16 ptr_x = static_cast<s16>(m_ptr_x + mouse.dx);
		s16 ptr_y = static_cast<s16>(m_ptr_y + mouse.dy);
		if (ptr_x < 0) ptr_x = 0;
		if (ptr_x > static_cast<s16>(kViewportW - 1u)) ptr_x = static_cast<s16>(kViewportW - 1u);
		if (ptr_y < 0) ptr_y = 0;
		if (ptr_y > static_cast<s16>(kFbH + kHudLines - 1u)) {
			ptr_y = static_cast<s16>(kFbH + kHudLines - 1u);
		}
		if (ptr_x != m_ptr_x || ptr_y != m_ptr_y) {
			m_ptr_x = ptr_x;
			m_ptr_y = ptr_y; // posicion REAL (0..255: la franja tambien recibe puntero)
			m_ptr.dirty = true;
		}
		const s16 ptr_true_y = m_ptr_y;
		// El bob del puntero vive en el mundo: sobre la franja se oculta (la barra la pinta el HUD).
		const bool ptr_hidden = ptr_true_y >= static_cast<s16>(kFbH);

		const u32 frame = context.frame.frame_index;
		m_frame_now = frame;
		const bool click = mouse.left_button && !m_prev_click;
		m_prev_click = mouse.left_button;

		// --- Hit-test: GUI (hud) -> hotspot -> suelo (prioridad de `pick_pointer`) ---
		PointerHit hit {};
		PointerHit gui_hit {};
		if (ptr_true_y >= static_cast<s16>(kFbH)) {
			gui_hit.kind = PointerKind::Gui;
		}
		if (gui_hit.kind == PointerKind::Gui) {
			const s16 ly = static_cast<s16>(ptr_true_y - static_cast<s16>(kFbH));
			// Coordenadas de la region-franja: el bitmap de la franja empieza en la columna
			// 16 de pantalla (mismo fetch que el mundo sin camara), asi que x_local = x + 16.
			const eng::hud::HudHit hh = eng::hud::hud_hit_test(
				m_hud_model, static_cast<s16>(ptr_x + static_cast<s16>(kMarginX)), ly);
			m_hud_verb_hover = eng::adv::kNoObject;
			m_hud_inv_hover = eng::adv::kNoObject;
			if (hh.hit) {
				if (hh.grid == m_hud_verbs_grid) {
					if (hh.slot != eng::hud::kHudNoSlot && hh.slot < kVerbCapacity) {
						m_hud_verb_hover = static_cast<u8>(hh.slot);
					}
				} else if (hh.grid == m_hud_inv_grid) {
					if (hh.slot != eng::hud::kHudNoSlot && hh.slot < kInvCols) {
						m_hud_inv_hover = static_cast<u8>(hh.slot);
					}
				}
			}
			if (click) on_gui_click();
			hit.kind = PointerKind::Gui;
		} else {
			// Punto de sala (mundo): ver que haya hotspot visible.
			const s16 wx = static_cast<s16>(m_cam_int + ptr_x);
			const s16 wy = ptr_true_y;
			m_hover_hotspot = eng::adv::kInvalidHotspot;
			for (u8 i = 0u; i < kHotspotCount; ++i) {
				if (hotspot_present(i) && wx >= kHotspots[i].x0 && wx < kHotspots[i].x1 &&
				    wy >= kHotspots[i].y0 && wy < kHotspots[i].y1) {
					m_hover_hotspot = kHotspots[i].id;
				}
			}
			if (m_hover_hotspot != eng::adv::kInvalidHotspot) {
				hit.kind = PointerKind::Hotspot;
				hit.id = m_hover_hotspot;
			} else {
				hit.kind = PointerKind::Floor;
			}
			if (click) on_world_click(wx, wy, hit);
		}

		// --- Guion determinista de paseo (como 001) y attract de interaccion ---
		ENG_PROF_BEGIN(kProfBlits);
		if (click) m_idle_frames = 0u;
#if !defined(ADVVERB_NO_ATTRACT)
		attract_step(frame);
#endif
		if (m_walking) {
			advance_actor();
			m_actor.dirty = true;
			m_walk_timer++;
		} else if (frame >= m_wait_until && !m_user_took_control && m_attract_step >= kAttractSteps) {
			m_tour = static_cast<eng::u8>((m_tour + 1u) % kTourCount);
			set_destination(kTour[m_tour][0], kTour[m_tour][1], true);
		}

		// --- Camara y ventana ---
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
			m_ptr.dirty = true;
		}
		m_ptr.x = static_cast<s16>(m_cam_int + m_ptr_x);
		m_ptr.y = ptr_true_y < static_cast<s16>(kFbH) ? ptr_true_y
							      : static_cast<s16>(kFbH - 1u);
		set_cursor(cursor_kind_for_verb(m_verb));

		// --- Blits: restaurar primero, luego props (estaticos), actor y puntero ---
		if (m_actor.dirty) {
			blit_restore(backend, m_actor, m_actor_stash);
		}
		if (m_ptr.dirty) {
			blit_restore(backend, m_ptr, m_ptr_stash);
			if (ptr_hidden) m_ptr.dirty = false; // la franja no pinta el cursor del mundo
		}
		if (m_key.bob.px >= 0 && !m_key.present) {
			blit_restore(backend, m_key.bob, m_key.stash); // recogida: destapa el fondo
		} else if (m_key.present && m_key.bob.dirty) {
			blit_restore(backend, m_key.bob, m_key.stash);
			blit_prop(backend, m_key);
		}
		if (m_coin.bob.px >= 0 && !m_coin.present) {
			blit_restore(backend, m_coin.bob, m_coin.stash);
		} else if (m_coin.present && m_coin.bob.dirty) {
			blit_restore(backend, m_coin.bob, m_coin.stash);
			blit_prop(backend, m_coin);
		}
		if (m_door.bob.px >= 0 && !m_door.present) {
			blit_restore(backend, m_door.bob, m_door.stash); // cerrar: vuelve la puerta pintada
		} else if (m_door.present && m_door.bob.dirty) {
			blit_restore(backend, m_door.bob, m_door.stash);
			blit_prop(backend, m_door);
		}
		if (m_actor.dirty) {
			blit_stash(backend, m_actor, m_actor_stash);
			blit_draw(backend, m_actor,
				  (m_walking && (m_walk_timer & 8u) != 0u)
					  ? reinterpret_cast<const u16*>(m_art_step.data())
					  : reinterpret_cast<const u16*>(m_art_idle.data()));
			m_actor.dirty = false;
		}
		if (m_ptr.dirty && !ptr_hidden) {
			blit_stash(backend, m_ptr, m_ptr_stash);
			blit_draw(backend, m_ptr, m_ptr_sheet);
			m_ptr.dirty = false;
		}
		ENG_PROF_END(kProfBlits);

		patch_window(static_cast<u16>(m_cam_int + kMarginX));

		// --- HUD: frase, replica, barra de verbos e inventario ---
		ENG_PROF_BEGIN(kProfHud);
		m_say.tick();
		draw_hud_dynamic(frame);
		ENG_PROF_END(kProfHud);
		ENG_PROF_END(kProfFrame);

		g_eng_run_status.detail = (static_cast<u32>(static_cast<u16>(m_cam_int)) << 16u) |
					  (static_cast<u32>(m_walking ? 1u : 0u) << 15u) |
					  static_cast<u32>(m_bag.size());
		eng::debug::probe_when_ready(g_eng_run_status, frame);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	// --- Contenido rpg (objetos, bolsa, cerradura) ---
	void build_content() {
		using namespace eng::rpg;
		ItemDef key_def {};
		key_def.tags = item_tags::key;
		key_def.weight = 1u;
		key_def.stack_max = 1u;
		key_def.durability_max = 0u;
		m_key_type = m_catalog.add(key_def);
		ItemDef coin_def {};
		coin_def.tags = 0u;
		coin_def.weight = 1u;
		coin_def.stack_max = 99u;
		m_coin_type = m_catalog.add(coin_def);
		ItemInstance key_inst {};
		key_inst.type = m_key_type;
		key_inst.qty = 1u;
		key_inst.metadata = 1u; // llave de la puerta
		m_key_item = m_store.spawn(key_inst);
		ItemInstance coin_inst {};
		coin_inst.type = m_coin_type;
		coin_inst.qty = 3u;
		m_coin_item = m_store.spawn(coin_inst);
		(void)m_ground.drop(m_key_item, 132, 152, 0u);
		(void)m_ground.drop(m_coin_item, 240, 176, 0u);
		m_lock = m_locks.add(1u); // cerradura que abre la llave con metadata 1
		if (!m_key_item.valid() || !m_coin_item.valid() || m_lock == 0xffu) {
			eng::debug::mark_failed(g_eng_run_status, kBootContent);
		}
	}

	// --- Display (una vez; identico a 001) ---
	void emit_display(scene::Scene& sc) {
		auto& s = sc.scheduler();
		s.move(eng::copper::Register::DMACON,
		       static_cast<u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
					eng::copper::DmaCopper | eng::copper::DmaBitplane));
		s.move(eng::copper::Register::BPLCON0, scene::bplcon0_for(scene::SceneMode::Standard, kPlanes));
		m_bplcon1_slot = s.move_at(eng::copper::Register::BPLCON1, 0x0000u);
		s.move(eng::copper::Register::BPLCON2, 0x0000u);
		s.move(eng::copper::Register::BPL1MOD, static_cast<u16>(kPlaneMod));
		s.move(eng::copper::Register::BPL2MOD, static_cast<u16>(kPlaneMod));
		s.move(eng::copper::Register::DIWSTRT, kDiwstrt);
		s.move(eng::copper::Register::DIWSTOP, kDiwstop);
		s.move(eng::copper::Register::DDFSTRT, kDdfstrt);
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
		s.wait_line(static_cast<u8>(kHudLine));
		s.move(eng::copper::Register::BPL1MOD, 0x0000u);
		s.move(eng::copper::Register::BPL2MOD, 0x0000u);
		s.move(eng::copper::Register::BPLCON1, 0x0000u);
		s.move(eng::copper::Register::DDFSTOP, 0x00d8u);
		s.move(eng::copper::Register::BPLCON0, scene::bplcon0_for(scene::SceneMode::Standard, 1u));
		s.move(eng::copper::color_register(0u), kHudColor0);
		s.move(eng::copper::color_register(1u), kHudColor1);
		s.move_bitplane_pointer(0u, m_hud.mem_view().address(2));
	}

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
	void blit_restore(eng::amiga::AmigaBackend& backend, Bob& b,
			  eng::Block<eng::MaskTag, eng::MemoryKind::Chip>& stash) {
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

	void blit_stash(eng::amiga::AmigaBackend& backend, Bob& b,
			eng::Block<eng::MaskTag, eng::MemoryKind::Chip>& stash) {
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		const u16 words = b.words();
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
		j.kind = eng::graphics::BlitJobKind::MaskedBlobNoSave;
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

	/// Props estaticos: stash + draw en una llamada (solo cuando `dirty`).
	void blit_prop(eng::amiga::AmigaBackend& backend, GroundProp& p) {
		blit_stash(backend, p.bob, p.stash);
		blit_draw(backend, p.bob, p.sheet);
		p.bob.dirty = false;
	}

	// --- Ruta y avance (identicos a 001) ---
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

	// --- HUD: modelo, pintado y logica de interaccion ---
	void setup_hud_model() {
		eng::hud::HudRegionSpec band;
		band.placement = eng::hud::HudPlacement::Band;
		band.band.top = kHudTopScreen;
		band.band.height = kHudLines;
		band.band.planes = 1u;
		band.capture_input = false;
		(void)m_hud_model.add_region(band);
		m_hud_verbs_grid = m_hud_model.add_grid(kVerbRows, kVerbCols, kVerbPitchX, kVerbPitchY);
		m_hud_inv_grid = m_hud_model.add_grid(1u, kInvCols, kInvPitchX, kVerbPitchY);
		eng::hud::HudGridHit verbs {};
		verbs.area = eng::Box {kVerbAreaX, kVerbAreaY, static_cast<s16>(kVerbCols * kVerbPitchX),
				       static_cast<s16>(kVerbRows * kVerbPitchY)};
		verbs.cols = kVerbCols;
		verbs.rows = kVerbRows;
		verbs.pitch_x = kVerbPitchX;
		verbs.pitch_y = kVerbPitchY;
		verbs.first_id = 0u;
		verbs.grid = m_hud_verbs_grid;
		(void)m_hud_model.push_grid_hit(verbs);
		eng::hud::HudGridHit inv {};
		inv.area = eng::Box {kInvAreaX, kInvAreaY, static_cast<s16>(kInvCols * kInvPitchX),
				     static_cast<s16>(kVerbPitchY)};
		inv.cols = kInvCols;
		inv.rows = 1u;
		inv.pitch_x = kInvPitchX;
		inv.pitch_y = kVerbPitchY;
		inv.first_id = 0u;
		inv.grid = m_hud_inv_grid;
		(void)m_hud_model.push_grid_hit(inv);
		(void)m_hud_model.bind_grid(m_hud_inv_grid,
					    eng::Span<const eng::hud::HudSlotView> {m_slot_views, kInvCols});
		refresh_slot_views();
	}

	/// Proyeccion `ItemBag` -> vistas de slot del HUD (sin duplicar el modelo de objetos).
	void refresh_slot_views() {
		for (u8 i = 0u; i < kInvCols; ++i) {
			eng::hud::HudSlotView& v = m_slot_views[i];
			v = eng::hud::HudSlotView {};
			if (i < m_bag.size()) {
				const eng::rpg::ItemHandle h = m_bag.at(i);
				const eng::Ref<const eng::rpg::ItemInstance> inst = m_store.get(h);
				v.item_id = inst.valid() ? static_cast<u16>(inst->type + 1u) : 0u;
				v.qty = inst.valid() ? inst->qty : 0u;
				v.flags = static_cast<u8>(eng::hud::kHudSlotEnabled);
				if (m_selected_item == h) v.flags |= eng::hud::kHudSlotSelected;
			} else {
				v.flags = eng::hud::kHudSlotEmpty;
			}
		}
	}

	[[nodiscard]] bool hotspot_present(u8 i) const {
		if (kHotspots[i].id == kObjKey) return m_key.present;
		if (kHotspots[i].id == kObjCoin) return m_coin.present;
		return true;
	}

	void say(u8 text_id) { (void)m_say.push(text_id, 1u, kSayFrames); }

	/// Click en la franja: verbo -> selecciona; inventario -> sujeto de «Usar».
	/// Selecciona un verbo (frase nueva: sin sujeto).
	void select_verb(u8 verb) {
		if (verb == eng::adv::kInvalidVerb) return;
		m_verb = verb;
		m_selected_item = eng::rpg::no_item;
		refresh_slot_views();
	}

	void on_gui_click() {
		if (m_hud_verb_hover != eng::adv::kNoObject) {
			select_verb(verb_for_slot(m_hud_verb_hover));
			m_user_took_control = true;
		} else if (m_hud_inv_hover != eng::adv::kNoObject) {
			const u8 slot = m_hud_inv_hover;
			if (slot < m_bag.size()) {
				if (m_verb != eng::adv::kInvalidVerb &&
				    m_verbs.at(m_verb).arity == eng::adv::VerbArity::TwoObjects) {
					m_selected_item = m_bag.at(slot);
				} else {
					say(15u); // «necesito un verbo de dos objetos (USA)»
				}
				refresh_slot_views();
				m_user_took_control = true;
			}
		}
	}

	/// Click en el mundo: suelo -> andar; hotspot -> ejecutar la frase.
	void on_world_click(s16 wx, s16 wy, PointerHit hit) {
		m_user_took_control = true;
		if (hit.kind == PointerKind::Floor) {
			set_destination(wx, wy, false);
			m_wait_until = m_frame_now + 10u;
			return;
		}
		if (m_verb == eng::adv::kInvalidVerb) {
			select_verb(m_verbs.find(eng::adv::VerbSemantic::Look));
		}
		(void)run_action(m_verb, m_selected_item, hit.id);
	}

	/// Ejecuta verbo+sujeto+objeto por las mismas reglas que el click (aridad y resolver).
	[[nodiscard]] bool run_action(u8 verb, eng::rpg::ItemHandle subject, eng::u16 target) {
		eng::adv::AdvAction action {};
		action.verb = verb;
		action.subject = subject == eng::rpg::no_item
					 ? eng::adv::kNoObject
					 : static_cast<u8>(subject.slot + 1u);
		action.target = static_cast<u8>(target);
		eng::adv::ResolvedAction resolved {};
		if (!eng::adv::resolve_action(m_verbs, action, resolved)) {
			// Aridad incompleta (p. ej. USA sin sujeto): pista, sin ejecutar.
			say(15u);
			return false;
		}
		execute(resolved);
		m_selected_item = eng::rpg::no_item;
		refresh_slot_views();
		return true;
	}

	/// **Guion de atracción** (determinista, en frames): tras ~2 s sin entrada recorre la
	/// interacción completa (verbo -> respuesta -> recoger -> usar llave -> andar) para que la
	/// demo se explique sola, igual que el attract de la 303. Cualquier click real lo cancela.
	void attract_step(u32 frame) {
		if (m_attract_step >= kAttractSteps) return;
		if (m_idle_frames < 100u) {
			++m_idle_frames;
			return;
		}
		if (m_attract_step == 0u && m_attract_t0 == 0u) m_attract_t0 = frame;
		const u32 t = frame - m_attract_t0;
		switch (m_attract_step) {
		case 0: // ABRE
			if (t >= 10u) { select_verb(3u); m_attract_step = 1u; }
			break;
		case 1: // ABRE PUERTA -> «está cerrada»
			if (t >= 50u) { (void)run_action(m_verb, eng::rpg::no_item, kObjDoor); m_attract_step = 2u; }
			break;
		case 2: // COGE
			if (t >= 120u) { select_verb(1u); m_attract_step = 3u; }
			break;
		case 3: // COGE MONEDA
			if (t >= 150u) { (void)run_action(m_verb, eng::rpg::no_item, kObjCoin); m_attract_step = 4u; }
			break;
		case 4: // COGE
			if (t >= 220u) { select_verb(1u); m_attract_step = 5u; }
			break;
		case 5: // COGE LLAVE
			if (t >= 250u) { (void)run_action(m_verb, eng::rpg::no_item, kObjKey); m_attract_step = 6u; }
			break;
		case 6: // USA
			if (t >= 320u) { select_verb(2u); m_attract_step = 7u; }
			break;
		case 7: // sujeto = primera entrada de la bolsa (la llave)
			if (t >= 350u) {
				if (m_bag.size() > 0u) {
					m_selected_item = m_bag.at(0u);
					refresh_slot_views();
				}
				m_attract_step = 8u;
			}
			break;
		case 8: // USA LLAVE CON PUERTA -> se abre
			if (t >= 380u) {
				(void)run_action(m_verb, m_selected_item, kObjDoor);
				m_attract_step = 9u;
			}
			break;
		case 9: // andar por la playa
			if (t >= 500u) {
				(void)set_destination(200, 190, false);
				m_attract_step = 10u;
			}
			break;
		default:
			break;
		}
	}

	/// Semantica -> objetos de la demo (la tabla de contenido de esta sala).
	void execute(const eng::adv::ResolvedAction& a) {
		const eng::u16 target = target_object(a.target);
		const bool have_subject = a.subject != eng::adv::kNoObject;
		switch (a.semantic) {
		case eng::adv::VerbSemantic::Look:
			if (target == kObjDoor) say(0u);
			else if (target == kObjSign) say(1u);
			else if (target == kObjKey) say(2u);
			else if (target == kObjCoin) say(3u);
			break;
		case eng::adv::VerbSemantic::Take:
			if (target == kObjCoin && m_coin.present) take_prop(m_coin, 4u);
			else if (target == kObjKey && m_key.present) take_prop(m_key, 5u);
			else say(6u);
			break;
		case eng::adv::VerbSemantic::Open:
			if (target == kObjDoor) {
				if (m_door_open) say(7u);
				else if (m_locks.is_open(m_lock)) {
					m_door_open = true;
					m_door.present = true;
					m_door.bob.dirty = true;
					say(9u);
				} else {
					say(8u);
				}
			}
			break;
		case eng::adv::VerbSemantic::Close:
			if (target == kObjDoor && m_door_open) {
				m_door_open = false;
				m_door.present = false; // el pase de blits destapa la puerta pintada
				say(16u);
			}
			break;
		case eng::adv::VerbSemantic::Use:
			if (!have_subject) {
				say(15u);
				break;
			}
			if (target == kObjDoor) {
				const eng::rpg::ItemHandle held = subject_handle(a.subject);
				if (!held.valid() || !m_bag.contains(held)) {
					say(10u);
				} else if (m_locks.try_open(m_lock, held, m_store, m_catalog)) {
					if (!m_door_open) {
						m_door_open = true;
						m_door.present = true;
						m_door.bob.dirty = true;
					}
					say(9u);
				} else {
					say(10u);
				}
			} else if (target == kObjSign) {
				say(11u);
			} else {
				say(12u);
			}
			break;
		case eng::adv::VerbSemantic::Talk:
			if (target == kObjDoor) say(13u);
			else say(12u);
			break;
		default:
			say(12u);
			break;
		}
	}

	void take_prop(GroundProp& prop, u8 text_id) {
		// Localiza la entrada del suelo por su handle y recoge con el kernel rpg.
		eng::usize idx = 0u;
		bool found = false;
		for (eng::usize i = 0; i < m_ground.size(); ++i) {
			if ((prop.id == kObjKey && m_ground.at(i).item == m_key_item) ||
			    (prop.id == kObjCoin && m_ground.at(i).item == m_coin_item)) {
				idx = i;
				found = true;
				break;
			}
		}
		if (!found) {
			say(14u);
			return;
		}
		(void)eng::rpg::pick_up(m_ground, idx, m_bag, m_store, m_catalog);
		prop.present = false;
		prop.bob.dirty = true; // el siguiente pase restaura el fondo y ya no lo pinta
		say(text_id);
		refresh_slot_views();
	}

	[[nodiscard]] eng::u16 target_object(u8 target8) const {
		return target8 == 0xffu ? static_cast<eng::u16>(kObjFloor)
					: static_cast<eng::u16>(target8);
	}

	[[nodiscard]] eng::rpg::ItemHandle subject_handle(u8 subject8) const {
		// En esta demo el sujeto viaja como slot+1 del inventario (ver on_world_click).
		if (subject8 == eng::adv::kNoObject) return eng::rpg::no_item;
		const u8 slot = static_cast<u8>(subject8 - 1u);
		if (slot >= m_bag.size()) return eng::rpg::no_item;
		return m_bag.at(slot);
	}

	// --- Tabla de verbos (contenido de la demo) ---
	void build_verbs() {
		(void)m_verbs.add("MIRA", eng::adv::VerbSemantic::Look, eng::adv::VerbArity::Target,
				  eng::adv::CursorMode::Look);
		(void)m_verbs.add("COGE", eng::adv::VerbSemantic::Take, eng::adv::VerbArity::Target,
				  eng::adv::CursorMode::Use);
		(void)m_verbs.add("USA", eng::adv::VerbSemantic::Use, eng::adv::VerbArity::TwoObjects,
				  eng::adv::CursorMode::Use);
		(void)m_verbs.add("ABRE", eng::adv::VerbSemantic::Open, eng::adv::VerbArity::Target,
				  eng::adv::CursorMode::Use);
		(void)m_verbs.add("HABLA", eng::adv::VerbSemantic::Talk, eng::adv::VerbArity::Target,
				  eng::adv::CursorMode::Talk);
		(void)m_verbs.add("CIERRA", eng::adv::VerbSemantic::Close, eng::adv::VerbArity::Target,
				  eng::adv::CursorMode::Use);
		(void)m_verbs.add("EMPUJA", eng::adv::VerbSemantic::Push, eng::adv::VerbArity::Target,
				  eng::adv::CursorMode::Use);
		(void)m_verbs.add("TIRA", eng::adv::VerbSemantic::Pull, eng::adv::VerbArity::Target,
				  eng::adv::CursorMode::Use);
		(void)m_verbs.add("DA", eng::adv::VerbSemantic::Give, eng::adv::VerbArity::TwoObjects,
				  eng::adv::CursorMode::Use);
	}

	[[nodiscard]] u8 verb_for_slot(u8 slot) const {
		return slot < m_verbs.size() ? slot : eng::adv::kInvalidVerb;
	}

	// --- Pintado de la franja ---
	void draw_hud_static() {
		playfield::Surface hud {m_hud_pf, playfield::SurfaceRect {0, 0, kHudW, kHudLines}};
		hud.fill_rect(0, 0, static_cast<s16>(kHudW), static_cast<s16>(kHudLines), 0u);
		build_verbs();
		for (u8 i = 0u; i < m_verbs.size(); ++i) {
			m_verb_state[i] = 0xffu; // sentinela: la primera pintura no se salta
			paint_verb_cell(hud, i);
		}
	}

	/// Cabecera de la franja (nombre + versión): se pinta en el camino dinámico con `DiffText`
	/// (una vez; después solo si cambia).
	void draw_hud_header(playfield::Surface& hud) {
		(void)m_title_diff.paint(hud, 272, 27, "002VERBS", 1u);
		char label[40] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		(void)m_build_diff.paint(hud, 272, 38, label, 1u);
	}

	void paint_verb_cell(playfield::Surface& hud, u8 slot) {
		const u8 t = verb_for_slot(slot);
		if (t == eng::adv::kInvalidVerb) return;
		const s16 x = static_cast<s16>(kVerbAreaX + (slot % kVerbCols) * kVerbPitchX);
		const s16 y = static_cast<s16>(kVerbAreaY + (slot / kVerbCols) * kVerbPitchY);
		const u8 state = verb_cell_state(t);
		if (state == m_verb_state[t]) return;
		m_verb_state[t] = state;
		m_verb_diff[t].reset(); // fondo limpio: la celda se repinta entera
		hud.fill_rect(x, y, 56, 10, 0u);
		(void)m_verb_diff[t].paint(hud, x, y, m_verbs.at(t).label, 1u);
		if (state == 2u) {
			hud.fill_rect(x, static_cast<s16>(y + 8), 56, 2, 1u); // verbo seleccionado
		} else if (state == 1u) {
			hud.fill_rect(x, static_cast<s16>(y + 9), 56, 1, 1u); // bajo el puntero
		}
	}

	[[nodiscard]] u8 verb_cell_state(u8 verb) const {
		if (verb == m_verb) return 2u;                                  // seleccionado
		if (m_hud_verb_hover != eng::adv::kNoObject &&
		    verb_for_slot(m_hud_verb_hover) == verb) return 1u;        // bajo el puntero
		return 0u;
	}

	void draw_hud_dynamic(u32 frame) {
		playfield::Surface hud {m_hud_pf, playfield::SurfaceRect {0, 0, kHudW, kHudLines}};
		// Frase: VERBO [SUJETO] [CON] [OBJETIVO bajo el puntero].
		char line[64] {};
		u8 n = 0u;
		if (m_verb != eng::adv::kInvalidVerb) {
			n = str_cat(line, n, m_verbs.at(m_verb).label);
			if (m_selected_item != eng::rpg::no_item) {
				n = str_cat(line, n, " ");
				const eng::Ref<const eng::rpg::ItemInstance> inst = m_store.get(m_selected_item);
				if (inst.valid()) n = str_cat(line, n, item_label(inst->type));
			}
			const char* hov = hotspot_label(m_hover_hotspot);
			if (hov != nullptr) {
				if (m_selected_item != eng::rpg::no_item) n = str_cat(line, n, " CON");
				n = str_cat(line, n, " ");
				n = str_cat(line, n, hov);
			}
		} else {
			const char* hov = hotspot_label(m_hover_hotspot);
			n = str_cat(line, n, hov != nullptr ? hov : "ELIGE UN VERBO");
		}
		pad_to(line, n, kHudCols);
		(void)m_sentence_diff.paint(hud, kTextX, 2, line, 1u);

		// Replica en curso (tabla de textos del contenido).
		char say_line[64] {};
		u8 s = 0u;
		const eng::adv::SayQueue<4>::Line* cur = m_say.current();
		if (cur != nullptr && cur->text_id < kTextCount && kTexts[cur->text_id] != nullptr) {
			s = str_cat(say_line, s, kTexts[cur->text_id]);
		}
		pad_to(say_line, s, kHudCols);
		(void)m_say_diff.paint(hud, kTextX, 10, say_line, 1u);

		// Barra de verbos: solo celdas cuyo estado cambia.
		for (u8 i = 0u; i < m_verbs.size(); ++i) {
			paint_verb_cell(hud, i);
		}

		// Inventario: proyeccion de la bolsa (etiquetas de los slots ocupados).
		for (u8 i = 0u; i < kInvCols; ++i) {
			char cell[16] {};
			u8 c = 0u;
			const eng::hud::HudSlotView& v = m_slot_views[i];
			if ((v.flags & eng::hud::kHudSlotEmpty) == 0u && v.item_id != 0u) {
				c = str_cat(cell, c, item_label(static_cast<eng::rpg::ItemTypeId>(v.item_id - 1u)));
				if (m_selected_item != eng::rpg::no_item && i < m_bag.size() &&
				    m_bag.at(i) == m_selected_item) {
					c = str_cat(cell, c, " *");
				}
			}
			pad_to(cell, c, 10u);
			(void)m_inv_diffs[i].paint(hud, static_cast<s16>(kInvAreaX + i * kInvPitchX), kInvAreaY,
						   cell, 1u);
		}

		// Contador de frames (depuracion): "F<num>" en la celda libre de la barra.
		char f[8] {};
		u8 q = 1u;
		f[0] = 'F';
		u32 v = frame;
		char tmp[5] {};
		u8 d = 0u;
		do { tmp[d++] = static_cast<char>('0' + (v % 10u)); v /= 10u; } while (v != 0u && d < 5u);
		for (u8 k = 0u; k < d; ++k) f[q++] = tmp[d - 1u - k];
		f[q] = '\0';
		(void)m_frame_diff.paint(hud, 208, 38, f, 1u);

		// Cabecera (nombre + version) AL FINAL: el `DiffText` del contador de frames cubre
		// 8 celdas y, en su primera pintura, escribe espacios hasta x=336; si la version se
		// pintara antes, quedaria borrada por esos espacios de esa primera pasada.
		draw_hud_header(hud);
	}

	[[nodiscard]] static const char* item_label(eng::rpg::ItemTypeId t) {
		static constexpr const char* kLabels[] = {"LLAVE", "MONEDA"};
		return t < 2u ? kLabels[t] : "OBJETO";
	}

	[[nodiscard]] const char* hotspot_label(eng::adv::HotspotId id) const {
		if (id == eng::adv::kInvalidHotspot) return nullptr;
		for (u8 i = 0u; i < kHotspotCount; ++i) {
			if (kHotspots[i].id == id && hotspot_present(i)) return kHotspots[i].label;
		}
		return nullptr;
	}

	// --- Cursor por verbo ---
	void set_cursor(u8 kind) {
		if (kind == m_cursor_kind && m_ptr.sheet != nullptr) return;
		m_cursor_kind = kind;
		if (kind == 1u) {
			m_ptr_sheet = reinterpret_cast<const u16*>(m_cur_eye.data());
			m_ptr.mask = reinterpret_cast<const u16*>(m_cur_eye_mask.data());
		} else if (kind == 2u) {
			m_ptr_sheet = reinterpret_cast<const u16*>(m_cur_hand.data());
			m_ptr.mask = reinterpret_cast<const u16*>(m_cur_hand_mask.data());
		} else {
			m_ptr_sheet = reinterpret_cast<const u16*>(m_cur_arrow.data());
			m_ptr.mask = reinterpret_cast<const u16*>(m_cur_arrow_mask.data());
		}
		m_ptr.sheet = m_ptr_sheet;
		m_ptr.dirty = true;
	}

	[[nodiscard]] u8 cursor_kind_for_verb(u8 verb) const {
		if (verb == eng::adv::kInvalidVerb) return kCursorKindArrow;
		switch (m_verbs.cursor_for(verb)) {
		case eng::adv::CursorMode::Look:
		case eng::adv::CursorMode::Talk:
			return kCursorKindEye;
		case eng::adv::CursorMode::Use:
		case eng::adv::CursorMode::Exit:
			return kCursorKindHand;
		default:
			return kCursorKindArrow;
		}
	}

	// --- Copia de arte a Chip ---
	template <class A>
	void copy_art(eng::Block<eng::MaskTag, eng::MemoryKind::Chip>& dst, const A& src) {
		const eng::Words<eng::MaskTag> w = dst.view.as_words();
		for (eng::u32 i = 0u; i < src.size(); ++i) w[i] = src[i];
	}

	// --- Estado ---
	static constexpr eng::u8 kMaxWaypoints = 24u;
	static constexpr eng::u8 kAttractSteps = 11u;
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
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_cur_arrow {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_cur_arrow_mask {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_cur_eye {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_cur_eye_mask {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_cur_hand {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_cur_hand_mask {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_key_art {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_key_mask {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_coin_art {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_coin_mask {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_door_art {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_door_mask {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_actor_stash {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_ptr_stash {};

	eng::adv::Camera<eng::math::Fixed<s16, 4>> m_cam {};
	s16 m_cam_int = 0;

	Bob m_actor {nullptr, nullptr, kActorW, kActorH, 100, 180, true, -1, -1};
	Bob m_ptr {nullptr, nullptr, kPtrW, kPtrH, 0, 0, true, -1, -1};
	const u16* m_ptr_sheet = nullptr;
	u8 m_cursor_kind = 0xffu;

	GroundProp m_key {};
	GroundProp m_coin {};
	GroundProp m_door {};

	eng::math::Vec<2, s16> m_route[kMaxWaypoints] {};
	eng::u8 m_wp = 0u;
	eng::u8 m_wp_count = 0u;
	bool m_walking = false;
	bool m_scripted = true;
	bool m_user_took_control = false;
	eng::u32 m_idle_frames = 0u;
	eng::u8 m_attract_step = 0u;
	eng::u32 m_attract_t0 = 0u;
	eng::u8 m_tour = 1u;
	u32 m_wait_until = 0u;
	u32 m_frame_now = 0u;
	u32 m_walk_timer = 0u;
	u32 m_blit_fail = 0u;
	s16 m_err_x = 0;
	s16 m_err_y = 0;

	s16 m_ptr_x = 160;
	s16 m_ptr_y = 100;
	eng::amiga::MousePollState m_mouse_poll {};
	bool m_prev_click = false;

	// --- Verbos y frase (contenido A1) ---
	eng::adv::VerbSet<kVerbCapacity> m_verbs {};
	u8 m_verb = eng::adv::kInvalidVerb;
	eng::rpg::ItemHandle m_selected_item {};
	eng::adv::SayQueue<4> m_say {};
	eng::adv::HotspotId m_hover_hotspot {eng::adv::kInvalidHotspot};
	u8 m_hud_verb_hover = eng::adv::kNoObject;
	u8 m_hud_inv_hover = eng::adv::kNoObject;
	u8 m_verb_state[kVerbCapacity] {};
	playfield::DiffText<48> m_verb_diff[kVerbCapacity] {};
	playfield::DiffText<48> m_sentence_diff {};
	playfield::DiffText<48> m_say_diff {};
	playfield::DiffText<16> m_title_diff {};
	playfield::DiffText<16> m_build_diff {};
	playfield::DiffText<16> m_frame_diff {};
	playfield::DiffText<16> m_inv_diffs[kInvCols] {};

	// --- HUD (modelo + slots) ---
	eng::hud::Hud<1u, 8u, 8u, 8u, 8u> m_hud_model {};
	eng::hud::GridHandle m_hud_verbs_grid {};
	eng::hud::GridHandle m_hud_inv_grid {};
	eng::hud::HudSlotView m_slot_views[kInvCols] {};

	// --- Kernel rpg (objetos de la sala) ---
	eng::rpg::ItemCatalog<2> m_catalog {};
	eng::rpg::ItemStore<4> m_store {};
	eng::rpg::ItemBag<2> m_bag {};
	eng::rpg::GroundItems<2> m_ground {};
	eng::rpg::LockSet<1> m_locks {};
	eng::rpg::ItemTypeId m_key_type {0u};
	eng::rpg::ItemTypeId m_coin_type {1u};
	eng::rpg::ItemHandle m_key_item {};
	eng::rpg::ItemHandle m_coin_item {};
	u8 m_lock = 0u;
	bool m_door_open = false;

	// Router: buffers de trabajo (sin heap).
	eng::adv::walkbox_dist_t<s16> m_best[kBoxCount] {};
	eng::u16 m_prev[kBoxCount] {};
	eng::u8 m_state[kBoxCount] {};
};

AdvVerbsDemo g_game {};

} // namespace

int main() {
	eng::amiga::ensure_boot();
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	// Espera de VBlank ACTIVA (variante IRQ + sondeo): ver
	// `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md` (carrera de
	// despertar del bucle por senal con trabajo variable por frame).
	backend.set_vblank_sleep(false);
	eng::Engine engine {backend, g_game};
	(void)eng::os::init(engine, 0u);
	// Servicio de Blitter de fondo OFF: los blits de la demo son sincronos por frame.
	engine.set_blit_service_enabled(false);
	(void)engine.run_frames(0xffffu);
	return 0;
}
