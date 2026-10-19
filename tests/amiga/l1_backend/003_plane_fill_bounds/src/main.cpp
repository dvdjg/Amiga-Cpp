// ============================================================================
// L1-003 — Relleno por plano del Blitter con caras que tocan el borde
// ============================================================================
//
// Test **on-target** (hardware real del emulador) del camino de contorno+relleno
// por bitplane con polígonos que llegan *exactamente* al borde del bitmap. Es el
// caso que producía el residuo del borde derecho en la demo 088: el trazador de
// líneas calcula la palabra con `(x0 >> 3) & ~1` sobre `u32`, así que un vértice
// en `x == width` (un píxel FUERA, como deja el clip de cámara) cae en la primera
// palabra de la fila siguiente, y uno en `x < 0` en la cola de la fila anterior.
// El contorno queda fuera del bbox de la cara y el clear por rectángulo sucio no
// lo borra nunca.
//
// La sonda es **estática y determinista**: una sola postura, cuatro caras
// separadas en bandas (control interior, borde derecho con x=320, borde izquierdo
// con x=-1, borde inferior con y=232 y una cara fuera del todo) y UNA pasada del
// Blitter. Después compara byte a byte contra la referencia CPU
// (`fill_polygons_by_plane_cpu`), que es el oráculo portátil ya validado por
// HOST-217/474, y comprueba:
//
// Verificación (bits de `g_eng_run_status.detail`):
//
//   bit 0: no hay píxeles fuera de las cajas esperadas dilatadas 1 px (residuo del borde)
//   bit 1: filas 232..255 limpias antes del HUD (nada escrito fuera del mundo)
//   bit 2: columnas del borde (x=0 y x=319) limpias (margen seguro del contorno)
//   bit 3: los interiores de los cuatro casos están rellenos
//   bit 4: nada perdido respecto a la referencia CPU (±1 px, el trazador va un paso detrás)
//   bit 5: fila 0 limpia: el primer píxel de cada línea va por el canal D y el relleno
//          por plano lo descarta en una palabra Chip fuera del bitmap (m_line_trash)
//
// `verify.sh` exige `detail == 0x3f` (los seis bits). Con el motor sin recortar las
// caras al rectángulo seguro del contorno fallan los bits 0/2; sin la palabra de
// descarte falla el 5.
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u8 kPlanes = 2u;
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kWorldH = 232u;
constexpr u16 kBandY = kWorldH;
constexpr u16 kBandH = kHeight - kWorldH;

constexpr eng::Palette32 kPalette {{
	0x000, 0x00f, 0xf00, 0xf0f, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

constexpr eng::debug::Version kVersion {1u, 0u};

enum : eng::u32 {
	kBootMemory = 0x00009001u,
	kBootCompose = 0x00009002u,
};

// Casos de la sonda. Cada uno vive en su banda (separadas por >= 2 filas) para que
// cualquier escritura fuera de su caja sea inequívoca.
//   control: interior, no toca bordes
//   borde derecho: x=320 (un píxel FUERA del bitmap)
//   borde izquierdo: x=-1
//   borde inferior: y=232 (una fila FUERA de la zona de mundo)
//   fuera: enteramente fuera del bitmap (no debe escribir nada)
//   adyacencia: dos caras que comparten la vertical x=200 en PLANOS DISTINTOS (1 y 2)
constexpr s16 kCase0X[4] = {40, 120, 120, 40};
constexpr s16 kCase0Y[4] = {20, 20, 60, 60};
constexpr s16 kCase1X[4] = {280, 320, 320, 280};
constexpr s16 kCase1Y[4] = {20, 20, 70, 70};
constexpr s16 kCase2X[4] = {-1, 60, 60, -1};
constexpr s16 kCase2Y[4] = {90, 90, 130, 130};
constexpr s16 kCase3X[4] = {100, 180, 180, 100};
constexpr s16 kCase3Y[4] = {200, 200, 232, 232};
constexpr s16 kCase4X[4] = {400, 500, 500, 400};
constexpr s16 kCase4Y[4] = {20, 20, 60, 60};
constexpr s16 kCase5X[4] = {120, 200, 200, 120}; // izquierda, color 1 (plano 0)
constexpr s16 kCase5Y[4] = {150, 150, 190, 190};
constexpr s16 kCase6X[4] = {200, 280, 280, 200}; // derecha, color 2 (plano 1)
constexpr s16 kCase6Y[4] = {150, 150, 190, 190};

// Cajas esperadas (recorte al bitmap; el relleno exclusivo deja el borde derecho).
struct Rect {
	s16 x0, y0, x1, y1;
};
constexpr Rect kExpect[5] = {
	{40, 20, 120, 60},    // control
	{280, 20, 319, 70},   // borde derecho (x=320 se recorta a 319)
	{0, 90, 60, 130},     // borde izquierdo (x=-1 se recorta a 0)
	{100, 200, 180, 231}, // borde inferior (y=232 se recorta a 231)
	{120, 150, 280, 190}  // adyacencia (dos caras, mismo borde x=200)
};

// Puntos interiores de control (uno por caso; la cara del borde izquierdo es magenta,
// así que se comprueba en los dos planos).
constexpr s16 kProbe0X = 60; // control (plano 0)
constexpr s16 kProbe0Y = 40;
constexpr s16 kProbe1X = 300; // borde derecho (plano 1)
constexpr s16 kProbe1Y = 45;
constexpr s16 kProbe2X = 30; // borde izquierdo (ambos planos)
constexpr s16 kProbe2Y = 110;
constexpr s16 kProbe3X = 140; // borde inferior (plano 0)
constexpr s16 kProbe3Y = 215;

struct PlaneFillBoundsProbe {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = 1u; // sin doble buffer: una sola postura, sin ambigüedad
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::display(res), scene::palette(kPalette, 0u, 4u))) {
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<eng::u32>(m_scene.config_error().code));
			return;
		}
		m_scene.takeover(backend);
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		// Referencia CPU: mismo tamaño y misma limpieza, fuera de la pantalla.
		m_ref = backend.memory_manager().chip().reserve<eng::PlaneTag>(
			static_cast<u32>(kPlaneBytes) * kPlanes + 16u, 16);
		if (!m_ref.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootCompose + 1u);
			return;
		}
		(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);
		(void)backend.blitter_clear(m_ref.view, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);

		eng::graphics::PlanePolygon faces[7] = {
			{kCase0X, kCase0Y, 4u, 1u},
			{kCase1X, kCase1Y, 4u, 2u},
			{kCase2X, kCase2Y, 4u, 3u},
			{kCase3X, kCase3Y, 4u, 1u},
			{kCase4X, kCase4Y, 4u, 2u},
			{kCase5X, kCase5Y, 4u, 1u},
			{kCase6X, kCase6Y, 4u, 2u},
		};
#if defined(PROBE_CONTOURS_ONLY)
		// Inspección paso a paso: SOLO contornos, con el mismo planificador y la misma
		// llamada de línea que usa el relleno por plano. Sirve para ver en el framebuffer
		// qué traza el Blitter antes del area fill (bordes, filas, x=319...).
		for (u8 p = 0u; p < kPlanes; ++p) {
			eng::graphics::PlaneEdge edges[64] {};
			const u32 ne = eng::graphics::plan_plane_edges_in_bounds(
				faces, 7u, p, s16 {1}, static_cast<s16>(kWidth - 1u),
				s16 {0}, static_cast<s16>(kWorldH - 1u), edges, 64u);
			eng::PlaneBytes plane =
				planes.subspan(static_cast<u32>(p) * kPlaneBytes, kPlaneBytes);
			for (u32 e = 0u; e < ne && e < 64u; ++e) {
				(void)backend.blitter_line_eor(plane, kRowBytes, edges[e].x0, edges[e].y0,
							       edges[e].x1, edges[e].y1,
							       plane.data());
			}
		}
#else
		// Camino del Blitter (contorno ONEDOT + area fill + clear de la caja) y la
		// referencia CPU, cada uno en su buffer.
		(void)backend.fill_polygons_by_plane(faces, 7u, planes, kRowBytes, kPlaneBytes, kPlanes,
						     kWidth, kWorldH);
		(void)eng::graphics::fill_polygons_by_plane_cpu(
			eng::Span<const eng::graphics::PlanePolygon>(faces, 7u), m_ref.view, kRowBytes,
			kPlaneBytes, kPlanes, kWidth, kWorldH);
#endif

		const u32 detail = check(planes, m_ref.view);
		// HUD DESPUÉS de las comprobaciones: la franja inferior no puede contaminar el
		// chequeo de "fuera del mundo" (filas 232..255).
		playfield::ContiguousPlayfield pf {};
		(void)pf.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth,
				  kHeight, kPlanes, kPlaneBytes);
		playfield::Surface hud {pf, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY), "090 PLANE FILL BOUNDS", 3u);
		char label[40] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY + 10), label, 3u);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, detail);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	/// Fotograma de un bit del buffer del Blitter (x,y) para el plano `p`.
	[[nodiscard]] static bool pixel(const u8* planes, u32 p, u32 x, u32 y) {
		const u8* gp = planes + p * kPlaneBytes;
		return ((gp[y * kRowBytes + (x >> 3u)] >> (7u - (x & 7u))) & 1u) != 0u;
	}

	/// Devuelve los bits de la comprobación. `want` es la referencia CPU (oráculo
	/// portátil), usada con tolerancia de ±1 px: el trazador del Blitter pinta un paso
	/// por detrás (ver `clip_face_to_rect`), así que el borde inferior/superior y las
	/// columnas del borde pueden diferir en 1 px; lo que NO puede pasar es un píxel
	/// suelto lejos de las caras (el residuo del borde) ni una escritura fuera del mundo.
	[[nodiscard]] static u32 check(eng::PlaneBytes got, eng::PlaneBytes want) {
		u32 bits = 0u;
		const u8* g = got.data();
		const u8* w = want.data();
		// bit 0: nada fuera de las cajas esperadas dilatadas 1 px (filas 1..231; la
		// fila 0 tiene su propio bit porque el primer píxel de cada línea va por D).
		bool outside = false;
		for (u32 p = 0u; p < kPlanes && !outside; ++p) {
			for (u32 y = 1u; y < kWorldH && !outside; ++y) {
				for (u32 x = 0u; x < kWidth && !outside; ++x) {
					if (!pixel(g, p, x, y)) {
						continue;
					}
					bool in_box = false;
					for (u32 c = 0u; c < 5u && !in_box; ++c) {
						const s16 dx0 = static_cast<s16>(kExpect[c].x0 - 1);
						const s16 dy0 = static_cast<s16>(kExpect[c].y0 - 1);
						const s16 dx1 = static_cast<s16>(kExpect[c].x1 + 1);
						const s16 dy1 = static_cast<s16>(kExpect[c].y1 + 1);
						in_box = static_cast<s16>(x) >= dx0 && static_cast<s16>(x) <= dx1 &&
							 static_cast<s16>(y) >= dy0 &&
							 static_cast<s16>(y) <= dy1;
					}
					if (!in_box) {
						outside = true;
					}
				}
			}
		}
		if (!outside) {
			bits |= 1u << 0u;
		}
		// bit 1: filas 232..255 limpias (nada escrito fuera de la zona de mundo).
		bool outside_world = false;
		for (u32 p = 0u; p < kPlanes && !outside_world; ++p) {
			const u8* gp = g + p * kPlaneBytes;
			for (u32 y = kWorldH; y < kHeight && !outside_world; ++y) {
				for (u32 b = 0u; b < kRowBytes; ++b) {
					if (gp[y * kRowBytes + b] != 0u) {
						outside_world = true;
					}
				}
			}
		}
		if (!outside_world) {
			bits |= 1u << 1u;
		}
		// bit 2: columnas del borde (x=0 y x=319) limpias en el mundo (filas 1..231).
		// Es el invariante del margen seguro: el contorno no baja de x=1 (se dibuja en
		// x-1) y el relleno no pasa de x=318.
		bool borders_clean = true;
		for (u32 p = 0u; p < kPlanes && borders_clean; ++p) {
			for (u32 y = 1u; y < kWorldH; ++y) {
				if (pixel(g, p, 0u, y) || pixel(g, p, kWidth - 1u, y)) {
					borders_clean = false;
					break;
				}
			}
		}
		if (borders_clean) {
			bits |= 1u << 2u;
		}
		// bit 3: interiores rellenos (los cuatro casos; color 3 = ambos planos).
		const bool inside_ok =
			pixel(g, 0u, static_cast<u32>(kProbe0X), static_cast<u32>(kProbe0Y)) &&
			pixel(g, 1u, static_cast<u32>(kProbe1X), static_cast<u32>(kProbe1Y)) &&
			pixel(g, 0u, static_cast<u32>(kProbe2X), static_cast<u32>(kProbe2Y)) &&
			pixel(g, 1u, static_cast<u32>(kProbe2X), static_cast<u32>(kProbe2Y)) &&
			pixel(g, 0u, static_cast<u32>(kProbe3X), static_cast<u32>(kProbe3Y));
		if (inside_ok) {
			bits |= 1u << 3u;
		}
		// bit 4: nada perdido respecto a la referencia CPU (±1 px en las 8 direcciones).
		bool lost = false;
		for (u32 p = 0u; p < kPlanes && !lost; ++p) {
			for (u32 y = 0u; y < kWorldH && !lost; ++y) {
				for (u32 x = 0u; x < kWidth && !lost; ++x) {
					if (!pixel(w, p, x, y)) {
						continue;
					}
					bool found = false;
					for (s32 dy = -1; dy <= 1 && !found; ++dy) {
						for (s32 dx = -1; dx <= 1 && !found; ++dx) {
							const s32 nx = static_cast<s32>(x) + dx;
							const s32 ny = static_cast<s32>(y) + dy;
							if (nx >= 0 && nx < static_cast<s32>(kWidth) && ny >= 0 &&
							    ny < static_cast<s32>(kWorldH) &&
							    pixel(g, p, static_cast<u32>(nx), static_cast<u32>(ny))) {
								found = true;
							}
						}
					}
					if (!found) {
						lost = true;
					}
				}
			}
		}
		if (!lost) {
			bits |= 1u << 4u;
		}
		// bit 5: fila 0 limpia (el primer píxel de cada línea va por D y se descarta en la
		// palabra Chip del backend).
		bool row0_clean = true;
		for (u32 p = 0u; p < kPlanes && row0_clean; ++p) {
			for (u32 x = 0u; x < kWidth; ++x) {
				if (pixel(g, p, x, 0u)) {
					row0_clean = false;
					break;
				}
			}
		}
		if (row0_clean) {
			bits |= 1u << 5u;
		}
		// bit 6: adyacencia entre planos. La columna compartida por las dos caras (x=200,
		// filas 151..189) debe pertenecer a UN solo plano: 1,1 sería magenta (ambas caras
		// pintan el borde) y 0,0 un hueco.
		bool seam_ok = true;
		for (u32 y = 151u; y <= 189u; ++y) {
			if (pixel(g, 0u, 200u, y) == pixel(g, 1u, 200u, y)) {
				seam_ok = false;
			}
		}
		if (seam_ok) {
			bits |= 1u << 6u;
		}
		// bit 7: junta contigua: la última columna de la cara izquierda (x=199, plano 0) y la
		// primera de la derecha después del borde (x=201, plano 1) están pintadas; no hay
		// hueco negro de separación.
		bool join_ok = true;
		for (u32 y = 151u; y <= 189u; ++y) {
			if (!pixel(g, 0u, 199u, y) || !pixel(g, 1u, 201u, y)) {
				join_ok = false;
			}
		}
		if (join_ok) {
			bits |= 1u << 7u;
		}
		return bits;
	}

	scene::Scene m_scene {};
	eng::Block<eng::PlaneTag> m_ref {};
	bool m_ready = false;
};

PlaneFillBoundsProbe g_game {};

} // namespace

int main() {
	eng::amiga::ensure_boot();
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	backend.set_vblank_sleep(false);
	eng::Engine engine {backend, g_game};
	(void)eng::os::init(engine, 0u);
	engine.set_blit_service_enabled(false);
	engine.run_frames(0xffffu);
	return 0;
}
