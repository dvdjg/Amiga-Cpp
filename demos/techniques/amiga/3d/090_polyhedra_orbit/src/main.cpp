// ============================================================================
// Demo 090 — poliedros 3D: icosidodecaedro central y cubo en órbita
// ============================================================================
//
// Un icosidodecaedro (30 vértices, 60 aristas, 32 caras: 20 triángulos y 12
// pentágonos) gira en el centro mientras un cubo lo orbita girando también. Las
// tres componentes de Euler de cada sólido llevan una oscilación lenta, de modo
// que sus ejes de rotación van variando ligeramente.
//
// Render: se proyectan los vértices UNA vez por sólido (30 + 8 divisiones) y las
// caras se ensamblan desde esas proyecciones. El culling usa la normal 3D en s32
// (`mul_wide`). El relleno es la técnica del Blitter Amiga para sólidos convexos:
// por cada plano se traza SOLO la frontera de la unión de caras con ese bit
// (contorno ONEDOT/EOR, las aristas internas se cancelan por paridad) y un area
// fill XOR reconstruye el índice de cada cara. El plano 3 es la capa de entramado:
// lo tienen todas las caras triangulares, así que selecciona su unión; tras el
// relleno se le aplica un damero 2×2 y cada triángulo alterna entre dos entradas
// de paleta (base y base+8) mientras los pentágonos quedan sólidos.
// `K090_CPU_FILL=1..3` conserva los caminos A/B (spans de CPU, cookie por cara y
// máscara por plano) para medir; no aplican el entramado. Las dos siluetas no se
// solapan: `orbit_separation_ok()` lo verifica al arrancar, de modo que el painter
// por cara es correcto.
//
// Colores: 4 planos (16 índices). 0 = fondo; los índices de cara usan un código
// de 3 bits con paridad par en los triángulos y paridad impar en los pentágonos:
// así cada arista triángulo-pentágono difiere en 1 o 3 bits y nunca hay dos caras
// adyacentes con el mismo color. 15 = blanco del HUD. El cubo reutiliza tonos
// sólidos en caras adyacentes distintas.
//
// Tabla del poliedro: puntos medios de las aristas del icosaedro de aristas 2
// ((0,±1,±φ) y permutaciones cíclicas), escalados a radio 90. Cada pentágono
// agrupa los cinco puntos medios de las aristas de un vértice del icosaedro,
// ordenados alrededor de su eje; el winding se corrige para que la normal
// apunte hacia fuera. La demo valida la malla al arrancar (Euler=2, cada arista
// en dos caras, caras planas, normales exteriores y radio constante).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/090_polyhedra_orbit --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/090_polyhedra_orbit

#include <eng/api/api.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/retro/flat_shade_xor.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

// --- Display: 320×256, cuatro planos; filas 208..255 reservadas al HUD ---------
constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u8 kPlanes = 4u;
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kWorldH = 208u; // zona 3D; el HUD ocupa la franja inferior
constexpr u16 kBandY = kWorldH;
constexpr u16 kBandH = kHeight - kWorldH;
constexpr u8 kBuffers = 2u;

// --- Cámara y escena ----------------------------------------------------------
// focal/z ≈ 0,49: el poliedro de radio 90 proyecta ~44 px. La órbita usa
// A=210/B=150 y z=520±30; `orbit_separation_ok()` comprueba que la silueta del
// cubo (circunradio 24·√3) nunca toca la del poliedro y que todo cabe.
constexpr s16 kFocal = 256;
constexpr s16 kCx = 160;
constexpr s16 kCy = 104;
constexpr s16 kNearZ = 64;
constexpr s16 kHalfW = 160;
constexpr s16 kHalfH = 104;
constexpr s16 kPolyZ = 520;
constexpr s16 kPolyR = 91; // radio máximo del poliedro tras el redondeo (90,21)
constexpr s16 kOrbitA = 210;
constexpr s16 kOrbitB = 150;
constexpr s16 kOrbitZWob = 30;
constexpr s16 kCubeHalf = 24;
constexpr u16 kPhaseStep = 12u; // ~0,3 vueltas de órbita por segundo a 25 fps

#ifndef K090_CPU_FILL
#define K090_CPU_FILL 0
#endif
#ifndef K090_FREEZE
#define K090_FREEZE 0
#endif
#ifndef K090_START_PHASE
#define K090_START_PHASE 0
#endif
#ifndef K090_CULL2D
#define K090_CULL2D 1 ///< 1 = culling por signo del area proyectada (2D), 0 = normal 3D
#endif
#ifndef K090_DITHER
#define K090_DITHER 1 ///< 1 = damero 2×2 en la capa de entramado (plano 3)
#endif
static_assert(K090_CPU_FILL >= 0 && K090_CPU_FILL <= 3,
	      "K090_CPU_FILL: 0=Blitter por plano (silueta XOR), 1=CPU spans, 2=cookie por cara, 3=mascara por plano");
static_assert(K090_FREEZE == 0 || K090_FREEZE == 1,
	      "K090_FREEZE detiene la animacion para capturas deterministas");
static_assert(K090_START_PHASE < 4096,
	      "K090_START_PHASE es un indice de la tabla seno");

// --- Paleta (4 planos = 16 índices) ------------------------------------------
// 0 fondo. Pentágonos sólidos: 1 (naranja), 2 (cian), 4 (amarillo), 7 (rojo).
// Triángulos (bit 3 = capa de entramado): 11, 13, 14; el damero los alterna con
// sus parejas 3, 5, 6, que son tonos más claros del mismo color. 15 = blanco HUD.
constexpr eng::Palette32 kPalette {{
	0x013, 0xf80, 0x0df, 0x7ef, 0xff0, 0x6f6, 0xf9c, 0xf24,
	0x333, 0x888, 0xccc, 0x05f, 0x444, 0x0a3, 0xa07, 0xfff,
}};
constexpr u8 kHudColor = 15u;
// Damero 2×2 de la capa de entramado (filas uniformes 0xAAAA/0x5555).
constexpr u16 kDitherRow0 = 0xaaaau;
constexpr u16 kDitherRow1 = 0x5555u;
constexpr u32 kDitherBytes = static_cast<u32>(kRowBytes) * 2u;

constexpr eng::debug::Version kVersion {1u, 0u};

constexpr u8 kHudChars = 38u; // 304 px desde x=16: cubre toda la franja
constexpr u8 kHudPeriod = 32u; // el fps cambia 1 vez/s: no repintar la franja cada 8 frames

constexpr eng::math3d::Vec3 p3(s16 x, s16 y, s16 z) {
	return eng::math3d::vec3(x, y, z);
}

// 30 vértices, 32 caras (20 triángulos y 12 pentágonos), 60 aristas. Generado a
// partir del icosaedro: puntos medios de sus aristas, radio 90, winding CCW
// visto desde fuera. Ver la cabecera para la construcción y las comprobaciones.
constexpr eng::math3d::Vec3 kPolyVerts[30] = {
	p3(28, 73, 45), p3(45, 28, 73), p3(-45, 28, 73),
	p3(0, 0, 90), p3(-28, 73, 45), p3(73, 45, 28),
	p3(28, 73, -45), p3(0, 90, 0), p3(73, 45, -28),
	p3(73, -45, 28), p3(45, -28, 73), p3(90, 0, 0),
	p3(-28, 73, -45), p3(45, 28, -73), p3(0, 0, -90),
	p3(-45, 28, -73), p3(28, -73, 45), p3(73, -45, -28),
	p3(28, -73, -45), p3(0, -90, 0), p3(-45, -28, 73),
	p3(-73, 45, 28), p3(-73, -45, 28), p3(-90, 0, 0),
	p3(-28, -73, 45), p3(-73, 45, -28), p3(45, -28, -73),
	p3(-28, -73, -45), p3(-45, -28, -73), p3(-73, -45, -28),
};

// Caras con relleno a cinco índices (los triángulos usan 3).
constexpr u8 kPolyFaceIdx[32][5] = {
	{1u, 5u, 0u, 0u, 0u},	 {0u, 7u, 4u, 0u, 0u},	{3u, 10u, 1u, 0u, 0u},
	{2u, 20u, 3u, 0u, 0u},	 {4u, 21u, 2u, 0u, 0u},	{5u, 11u, 8u, 0u, 0u},
	{6u, 12u, 7u, 0u, 0u},	 {8u, 13u, 6u, 0u, 0u},	{10u, 16u, 9u, 0u, 0u},
	{9u, 17u, 11u, 0u, 0u},	 {15u, 25u, 12u, 0u, 0u}, {13u, 26u, 14u, 0u, 0u},
	{14u, 28u, 15u, 0u, 0u}, {16u, 24u, 19u, 0u, 0u}, {18u, 26u, 17u, 0u, 0u},
	{19u, 27u, 18u, 0u, 0u}, {22u, 24u, 20u, 0u, 0u}, {21u, 25u, 23u, 0u, 0u},
	{23u, 29u, 22u, 0u, 0u}, {27u, 29u, 28u, 0u, 0u},
	{3u, 1u, 0u, 4u, 2u},	{5u, 8u, 6u, 7u, 0u},	{11u, 5u, 1u, 10u, 9u},
	{6u, 13u, 14u, 15u, 12u}, {17u, 9u, 16u, 19u, 18u}, {2u, 21u, 23u, 22u, 20u},
	{16u, 10u, 3u, 20u, 24u}, {4u, 7u, 12u, 25u, 21u}, {13u, 8u, 11u, 17u, 26u},
	{14u, 26u, 18u, 27u, 28u}, {27u, 19u, 24u, 22u, 29u}, {23u, 25u, 15u, 28u, 29u},
};
constexpr u8 kPolyFaceLen[32] = {3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u,
				 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 5u, 5u,
				 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u};
// 3 bits de código con paridad par (triángulos 11/13/14 = 3/5/6 con el bit de
// entramado) e impar (pentágonos 1/2/4/7). Como triángulos y pentágonos nunca son
// adyacentes entre sí dentro de su clase, el ciclo no repite color en una arista;
// la paridad garantiza que cada arista triángulo-pentágono difiera en 1 o 3 bits.
constexpr u8 kPolyFaceColor[32] = {11u, 13u, 14u, 11u, 13u, 14u, 11u, 13u, 14u, 11u,
				   13u, 14u, 11u, 13u, 14u, 11u, 13u, 14u, 11u, 13u,
				   1u, 2u, 4u, 7u, 1u, 2u, 4u, 7u, 1u, 2u,
				   4u, 7u};

// Cubo de medio lado 24 con el winding validado por HOST-481 (CCW desde fuera).
constexpr eng::math3d::Vec3 kCubeVerts[8] = {
	p3(-kCubeHalf, -kCubeHalf, -kCubeHalf), p3(kCubeHalf, -kCubeHalf, -kCubeHalf),
	p3(kCubeHalf, kCubeHalf, -kCubeHalf), p3(-kCubeHalf, kCubeHalf, -kCubeHalf),
	p3(-kCubeHalf, -kCubeHalf, kCubeHalf), p3(kCubeHalf, -kCubeHalf, kCubeHalf),
	p3(kCubeHalf, kCubeHalf, kCubeHalf), p3(-kCubeHalf, kCubeHalf, kCubeHalf),
};
constexpr u8 kCubeFaces[6][4] = {
	{0u, 3u, 2u, 1u}, {4u, 5u, 6u, 7u}, {1u, 2u, 6u, 5u},
	{0u, 4u, 7u, 3u}, {0u, 1u, 5u, 4u}, {3u, 7u, 6u, 2u},
};
// Colores sólidos (sin la capa de entramado): caras opuestas del cubo nunca
// comparten arista, así que el ciclo 1/2/4/7/3/5 no repite color en una arista.
constexpr u8 kCubeFaceColor[6] = {1u, 2u, 4u, 7u, 3u, 5u};

constexpr u8 kMaxFaces = 38u;	  // 32 del poliedro + 6 del cubo
constexpr u8 kMaxFaceVerts = 16u; // margen para el clip de cámara de respaldo

enum : eng::u8 { kProfFrame = 0, kProfProject = 1, kProfFill = 2, kProfHud = 3,
		  kProfClear = 4, kProfLines = 5, kProfPattern = 6, kProfWait = 7,
		  kProfXform = 8, kProfAssemble = 9 };

/// Decimal de `v` (patrón de la 080: sin libcalls en el HUD).
u8 u16_to_str(u16 v, char* out) {
	char tmp[6] {};
	u8 n = 0u;
	do {
		tmp[n++] = static_cast<char>('0' + static_cast<u8>(v % 10u));
		v = static_cast<u16>(v / 10u);
	} while (v != 0u && n < 5u);
	for (u8 i = 0u; i < n; ++i) {
		out[i] = tmp[n - 1u - i];
	}
	out[n] = '\0';
	return n;
}

u8 str_cat(char* out, u8 n, const char* s) {
	while (*s != '\0') {
		out[n++] = *s++;
	}
	out[n] = '\0';
	return n;
}

/// Rellena con espacios hasta `width` caracteres: repintar la misma extensión
/// reescribe también el fondo de las palabras (no hace falta limpiar la franja).
void pad_to(char* text, u8 n, u8 width) {
	while (n < width) {
		text[n++] = ' ';
	}
	text[width] = '\0';
}

struct DirtyBounds {
	bool valid = false;
	s16 x0 = 0, y0 = 0, x1 = -1, y1 = -1;
};

/// Arista de malla con sus dos caras adyacentes (malla cerrada: siempre 2).
struct MeshEdge {
	u8 v0 = 0, v1 = 0, f0 = 0, f1 = 0xff;
};
constexpr u8 kPolyEdgeCap = 64u;
constexpr u8 kCubeEdgeCap = 16u;
constexpr u8 kEdgeCapacity = 80u; // 60 del poliedro + 12 del cubo
constexpr u8 kFaceSlots = 38u;	  // 32 + 6 caras

struct PolyhedraDemo {
	// --- Setup -------------------------------------------------------------------
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({256u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00009001u);
			return;
		}
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = kBuffers;
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::display(res), scene::palette(kPalette, 0u, 16u))) {
			eng::debug::mark_failed(
				g_eng_run_status,
				0x00009002u + static_cast<u32>(m_scene.config_error().code));
			return;
		}
		// Malla y órbita son invariantes de la escena: si fallan, no se pinta.
		if (!mesh_is_closed()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00009010u);
			return;
		}
		if (!orbit_separation_ok()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00009011u);
			return;
		}
		m_poly_edge_count = build_edges<5>(kPolyFaceIdx, kPolyFaceLen, 32u, m_poly_edges,
							  kPolyEdgeCap);
		m_cube_edge_count = build_edges<4>(kCubeFaces, nullptr, 6u, m_cube_edges,
							  kCubeEdgeCap);
		if (m_poly_edge_count != 60u || m_cube_edge_count != 12u) {
			eng::debug::mark_failed(g_eng_run_status, 0x00009014u);
			return;
		}
#if K090_CPU_FILL == 2 || K090_CPU_FILL == 3
		// Mascara 1 bit para la ruta cookie-cut (la lee el Blitter: Chip).
		m_face_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(kPlaneBytes);
		if (!m_face_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00009012u);
			return;
		}
#endif
#if 1
		// Patrón del entramado (2 filas de damero 2×2) en Chip: el Blitter lo lee.
		m_scratch = backend.memory_manager().chip().reserve<eng::MaskTag>(kDitherBytes + 2u, 16u);
		if (!m_scratch.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00009013u);
			return;
		}
		{
			u8* dp = m_scratch.view.data();
			for (u32 i = 0u; i < kRowBytes; i += 2u) {
				dp[i] = static_cast<u8>(kDitherRow0 >> 8u);
				dp[i + 1u] = static_cast<u8>(kDitherRow0 & 0xffu);
				dp[kRowBytes + i] = static_cast<u8>(kDitherRow1 >> 8u);
				dp[kRowBytes + i + 1u] = static_cast<u8>(kDitherRow1 & 0xffu);
			}
		}
#endif
		m_scene.takeover(backend);
		// El Blitter no cede slots a la CPU durante el contorno/relleno (BLTPRI, como el
		// original de flatshade-convex): el camino por plano hace muchos blits cortos y
		// con el bus compartido cada línea paga contención. La CPU reprograma entre
		// líneas, así que el modo nasty mantiene el coste por línea en el mínimo.
		backend.set_blitter_priority(true);
		for (u8 b = 0u; b < m_scene.buffer_count(); ++b) {
			const eng::PlaneBytes planes = m_scene.buffer(b);
			(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth,
						    kHeight);
			(void)m_pf.bind_raw(planes.data(), static_cast<u32>(planes.size()), kWidth,
					    kHeight, kPlanes, kPlaneBytes);
			draw_hud_static();
		}
		m_ready = true;
		ENG_PROF_INIT(10u);
		m_fps.reset(0u, eng::debug::prof_clock());
		// Completar las dos páginas antes de READY: el primer flip no muestra una
		// página vacía (cada commit publica la anterior y rota).
		for (u8 b = 0u; b < m_scene.buffer_count(); ++b) {
			update(backend, context);
		}
		eng::debug::mark_ready(g_eng_run_status, 0u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) return;
		ENG_PROF_FRAME();
		ENG_PROF_BEGIN(kProfFrame);
		// Publicación diferida al inicio del update: el raster muestra siempre la
		// página completa anterior mientras se pinta la trasera.
		m_scene.commit();
		const eng::PlaneBytes planes = m_scene.back();
		(void)m_pf.bind_raw(planes.data(), static_cast<u32>(planes.size()), kWidth, kHeight,
				    kPlanes, kPlaneBytes);

		eng::graphics::Camera3D<eng::coord> cam {};
		using CT = eng::math::scalar_traits<eng::coord>;
		cam.focal = CT::from_int(kFocal);
		cam.cx = kCx;
		cam.cy = kCy;
		cam.near_z = CT::from_int(kNearZ);
		cam.half_w = CT::from_int(kHalfW);
		cam.half_h = CT::from_int(kHalfH);

		const u16 phase = m_phase;
		const s32 sinp = eng::retro::kSinQ12[phase & 4095u];
		const s32 cosp = eng::retro::kSinQ12[(phase + 1024u) & 4095u];

		eng::graphics::PlaneFillBuilder<kMaxFaces> fb {};
		m_face_count = 0u;
		m_vis_count = 0u;
		m_cur = DirtyBounds {};
		m_pbox = DirtyBounds {};
		m_cbox = DirtyBounds {};
		for (u8 i = 0u; i < kFaceSlots; ++i) m_vis[i] = false;
		ENG_PROF_BEGIN(kProfProject);
		// --- Poliedro: giro alrededor de su centro con ejes que oscilan despacio ---
		eng::math3d::Affine3<> poly_model = eng::math3d::Affine3<>::identity();
		eng::math3d::load_rotate(
			poly_model.m,
			eng::retro::turns(static_cast<u16>(
				static_cast<s32>(phase) * 2 +
				(eng::retro::kSinQ12[(phase * 5u) & 4095u] >> 5))),
			eng::retro::turns(static_cast<u16>(
				static_cast<s32>(phase) * 3 +
				(eng::retro::kSinQ12[(phase * 7u) & 4095u] >> 6))),
			eng::retro::turns(
				static_cast<u16>(eng::retro::kSinQ12[(phase * 11u) & 4095u] >> 4)));
		eng::math3d::Vec3 world[30];
		ENG_PROF_BEGIN(kProfXform);
		eng::math3d::mesh_transform(eng::Span<const eng::math3d::Vec3>(kPolyVerts, 30u),
					    poly_model,
					    eng::Span<eng::math3d::Vec3>(world, 30u));
		const eng::coord zc = CT::from_int(kPolyZ);
		for (u8 v = 0u; v < 30u; ++v) {
			world[v].v[2] = world[v].v[2] + zc;
		}
		s16 ppx[30];
		s16 ppy[30];
		if (!project_vertices(world, 30u, cam, ppx, ppy)) {
			ENG_PROF_END(kProfXform);
			ENG_PROF_END(kProfProject);
			return;
		}
		ENG_PROF_END(kProfXform);
		ENG_PROF_BEGIN(kProfAssemble);
#if K090_CPU_FILL == 0
		// La vía Blitter no necesita ensamblar polígonos: con la visibilidad por
		// cara y la caja de vértices proyectados basta para las aristas y el fill.
		for (u8 f = 0u; f < 32u; ++f) {
			m_fcol[f] = kPolyFaceColor[f];
			m_vis[f] = face_front_facing_2d(ppx, ppy, kPolyFaceIdx[f],
							kPolyFaceLen[f]);
			if (m_vis[f]) ++m_vis_count;
		}
		m_pbox = bounds_of(ppx, ppy, 30u);
#else
		for (u8 f = 0u; f < 32u; ++f) {
			submit_face(world, ppx, ppy, kPolyFaceIdx[f], kPolyFaceLen[f],
				    kPolyFaceColor[f], f, m_pbox, fb);
		}
#endif
		ENG_PROF_END(kProfAssemble);
		// --- Cubo: órbita elíptica en el plano de pantalla con vaivén de profundidad ---
		const s16 orbit_x = static_cast<s16>((kOrbitA * cosp) >> 12);
		const s16 orbit_y = static_cast<s16>((kOrbitB * sinp) >> 12);
		const s16 orbit_z = static_cast<s16>(
			kPolyZ + ((kOrbitZWob * eng::retro::kSinQ12[(phase * 2u) & 4095u]) >> 12));
		eng::math3d::Affine3<> cube_model = eng::math3d::Affine3<>::identity();
		eng::math3d::load_rotate(
			cube_model.m,
			eng::retro::turns(static_cast<u16>(
				static_cast<s32>(phase) * 3 +
				(eng::retro::kSinQ12[(phase * 3u) & 4095u] >> 5))),
			eng::retro::turns(static_cast<u16>(
				static_cast<s32>(phase) * 5 +
				(eng::retro::kSinQ12[(phase * 9u) & 4095u] >> 6))),
			eng::retro::turns(static_cast<u16>(
				static_cast<s32>(phase) * 7 +
				(eng::retro::kSinQ12[(phase * 13u) & 4095u] >> 5))));
		eng::math3d::Vec3 cube_world[8];
		ENG_PROF_BEGIN(kProfXform);
		eng::math3d::mesh_transform(eng::Span<const eng::math3d::Vec3>(kCubeVerts, 8u),
					    cube_model,
					    eng::Span<eng::math3d::Vec3>(cube_world, 8u));
		const eng::coord ox = CT::from_int(orbit_x);
		const eng::coord oy = CT::from_int(orbit_y);
		const eng::coord oz = CT::from_int(orbit_z);
		for (u8 v = 0u; v < 8u; ++v) {
			cube_world[v].v[0] = cube_world[v].v[0] + ox;
			cube_world[v].v[1] = cube_world[v].v[1] + oy;
			cube_world[v].v[2] = cube_world[v].v[2] + oz;
		}
		s16 cpx[8];
		s16 cpy[8];
		if (!project_vertices(cube_world, 8u, cam, cpx, cpy)) {
			ENG_PROF_END(kProfXform);
			ENG_PROF_END(kProfProject);
			return;
		}
		ENG_PROF_END(kProfXform);
		ENG_PROF_BEGIN(kProfAssemble);
#if K090_CPU_FILL == 0
		for (u8 f = 0u; f < 6u; ++f) {
			m_fcol[32u + f] = kCubeFaceColor[f];
			m_vis[32u + f] = face_front_facing_2d(cpx, cpy, kCubeFaces[f], 4u);
			if (m_vis[32u + f]) ++m_vis_count;
		}
		m_cbox = bounds_of(cpx, cpy, 8u);
		m_cur = m_pbox;
		(void)unite(m_cur, m_cbox);
#else
		for (u8 f = 0u; f < 6u; ++f) {
			submit_face(cube_world, cpx, cpy, kCubeFaces[f], 4u, kCubeFaceColor[f],
				    static_cast<u8>(32u + f), m_cbox, fb);
		}
#endif
		ENG_PROF_END(kProfAssemble);
		ENG_PROF_END(kProfProject);

		// --- Borrado de la unión de posiciones (actual y dos frames anteriores: el
		// buffer trasero se usó hace dos frames) y relleno de las caras visibles. ---
		ENG_PROF_BEGIN(kProfFill);
		DirtyBounds clear = m_cur;
		unite(clear, m_prev1);
		unite(clear, m_prev2);
#if K090_CPU_FILL == 3
		// Una mascara por plano (union OR de las caras con ese bit) + un cookie-cut:
		// 3 blits por plano en total (clear de plano + clear de mascara + cookie).
		// Limpia el plano completo, asi que el rectangulo sucio no hace falta.
		if (fb.count() != 0u) {
			(void)backend.fill_polygons_by_plane_spans(
				fb.faces().data(), fb.count(), planes, kRowBytes, kPlaneBytes, kPlanes,
				kWidth, kWorldH, m_face_mask.view);
		}
#elif K090_CPU_FILL != 0
		if (clear.valid) {
			// La ruta por cara no usa paridad, pero el borrado de las posiciones
			// anteriores sigue siendo por rectángulo (una pasada por plano).
			const s16 x0 = static_cast<s16>(clear.x0 > 2 ? clear.x0 - 2 : 0);
			const s16 y0 = static_cast<s16>(clear.y0 > 2 ? clear.y0 - 2 : 0);
			const s16 x1 = static_cast<s16>(clear.x1 < static_cast<s16>(kWidth - 2u)
							  ? clear.x1 + 2
							  : static_cast<s16>(kWidth - 1u));
			const s16 y1 = static_cast<s16>(clear.y1 < static_cast<s16>(kWorldH - 2u)
							  ? clear.y1 + 2
							  : static_cast<s16>(kWorldH - 1u));
			ENG_PROF_BEGIN(kProfClear);
			const u16 wx0 = static_cast<u16>(x0) & 0xfff0u;
			const u16 wx1 = static_cast<u16>(x1) | 0x000fu;
			const u16 words = static_cast<u16>((wx1 - wx0 + 16u) >> 4u);
			for (u8 p = 0u; p < kPlanes; ++p) {
				const eng::PlaneBytes plane =
					planes.subspan(static_cast<u32>(p) * kPlaneBytes,
						       kPlaneBytes);
				(void)backend.blitter_clear_rect(plane, kRowBytes, wx0, y0,
								 words,
								 static_cast<u16>(y1 - y0 + 1),
								 true);
			}
		}
#if K090_CPU_FILL == 1
		for (u8 i = 0u; i < m_face_count; ++i) {
			(void)m_pf.fill_polygon(eng::Span<const s16>(m_fx[i], m_fn[i]),
						eng::Span<const s16>(m_fy[i], m_fn[i]),
						m_fc[i]);
		}
#else
		// Mascara 1 bit por cara (spans CPU) + cookie-cut `set/clear` por plano:
		// overwrite opaco, sin paridad ni estado de winding.
		for (u8 i = 0u; i < m_face_count; ++i) {
			(void)backend.blitter_fill_polygon(planes, kPlanes, kRowBytes, kPlaneBytes,
							   m_fx[i], m_fy[i], m_fn[i], m_fc[i],
							   m_face_mask.view);
		}
#endif
#else
		if (m_pbox.valid || m_cbox.valid) {
			// 1) Limpiar el rastro de cada sólido por separado: la unión de ambos
			// rectángulos incluiría el hueco entre ellos (grande, y siempre vacío).
			ENG_PROF_BEGIN(kProfClear);
			DirtyBounds pb = m_pbox;
			(void)unite(pb, m_pbox1);
			(void)unite(pb, m_pbox2);
			DirtyBounds cb = m_cbox;
			(void)unite(cb, m_cbox1);
			(void)unite(cb, m_cbox2);
			clear_box(backend, planes, pb);
			clear_box(backend, planes, cb);
			ENG_PROF_END(kProfClear);
			// 2) Aristas de silueta y de cambio de color, una vez por arista (la
			// tabla de adyacencia evita recortar y deduplicar por plano). El color
			// es la XOR de las dos caras visibles o el de la única visible.
			eng::retro::OutlineEdge edges[kEdgeCapacity] {};
			ENG_PROF_BEGIN(kProfLines);
			const auto fill_solid = [&](const MeshEdge* es, u8 en, u8 base, const s16* px,
						    const s16* py, const DirtyBounds& box) {
				const u8 ne = gather_edges(es, en, base, px, py, edges,
							   kEdgeCapacity);
				if (ne == 0u || !box.valid) return;
				// Caja del contorno de ESTE sólido (2 px de margen): cada area
				// fill barre solo su objeto y no el hueco entre ambos.
				const s16 bx0 = static_cast<s16>(box.x0 > 2 ? box.x0 - 2 : 0);
				const s16 by0 = static_cast<s16>(box.y0 > 2 ? box.y0 - 2 : 0);
				const s16 bx1 = static_cast<s16>(
					box.x1 < static_cast<s16>(kWidth - 2u) ? box.x1 + 2
									       : static_cast<s16>(kWidth - 1u));
				const s16 by1 = static_cast<s16>(
					box.y1 < static_cast<s16>(kWorldH - 2u) ? box.y1 + 2
										: static_cast<s16>(kWorldH - 1u));
				const s16 fill_bbox[4] = {bx0, bx1, by0, by1};
				eng::retro::flat_shade_xor(
					backend, planes, kRowBytes, kPlaneBytes, kPlanes, kWidth,
					kWorldH,
					eng::Span<const eng::retro::OutlineEdge>(edges, ne),
					/*area_fill_wait=*/false, fill_bbox,
					/*per_plane_fill=*/true,
					/*line_d_base=*/m_scratch.view.data() + kDitherBytes);
			};
			fill_solid(m_poly_edges, m_poly_edge_count, 0u, ppx, ppy, m_pbox);
			fill_solid(m_cube_edges, m_cube_edge_count, 32u, cpx, cpy, m_cbox);
			ENG_PROF_END(kProfLines);
			if (m_cur.valid) {
#if K090_DITHER
				// 3) Damero de la capa de entramado (plano 3). Solo borra ese bit:
				// los planos 0-2 conservan el color base en todo el objeto, así que
				// el borde de la silueta alterna entre las dos entradas de la cara
				// sin dejar huecos de fondo. Se aplica solo a las filas de los
				// objetos, empezando en fila PAR para que la fase sea estable.
				s16 dy0 = static_cast<s16>(m_cur.y0 > 2 ? m_cur.y0 - 2 : 0);
				dy0 = static_cast<s16>(dy0 & ~1);
				const s16 dy1 = static_cast<s16>(
					m_cur.y1 < static_cast<s16>(kWorldH - 2u) ? m_cur.y1 + 2
										  : static_cast<s16>(kWorldH - 1u));
				const u32 pat_off = static_cast<u32>(dy0) * kRowBytes;
				ENG_PROF_BEGIN(kProfPattern);
				(void)backend.blitter_apply_pattern(
					planes.subspan(static_cast<u32>(3) * kPlaneBytes + pat_off,
						       kPlaneBytes - pat_off),
					kRowBytes, kWidth, static_cast<u16>(dy1 - dy0 + 1),
					eng::MaskBytes {m_scratch.view.data(), kDitherBytes}, 2u);
				ENG_PROF_END(kProfPattern);
#endif
				ENG_PROF_BEGIN(kProfWait);
				(void)backend.wait_blitter();
				ENG_PROF_END(kProfWait);
			}
		}
#endif
		ENG_PROF_END(kProfFill);
		m_prev2 = m_prev1;
		m_prev1 = m_cur;
		m_pbox2 = m_pbox1;
		m_pbox1 = m_pbox;
		m_cbox2 = m_cbox1;
		m_cbox1 = m_cbox;
		g_eng_run_status.detail = m_vis_count != 0u ? m_vis_count : m_face_count;

		const u32 frame = context.frame.frame_index;
		if ((frame % kHudPeriod) == 0u) {
			draw_hud_dynamic(frame);
		}
		ENG_PROF_END(kProfFrame);
#if !K090_FREEZE
		m_phase = static_cast<u16>((m_phase + kPhaseStep) & 4095u);
#endif
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	static bool unite(DirtyBounds& dst, const DirtyBounds& b) {
		if (!b.valid) return false;
		if (!dst.valid) {
			dst = b;
			return true;
		}
		if (b.x0 < dst.x0) dst.x0 = b.x0;
		if (b.y0 < dst.y0) dst.y0 = b.y0;
		if (b.x1 > dst.x1) dst.x1 = b.x1;
		if (b.y1 > dst.y1) dst.y1 = b.y1;
		return true;
	}

	/// Caja de un sólido a partir de sus vértices YA proyectados (contiene la silueta;
	/// un convexo no tiene píxeles fuera de la caja de sus vértices).
	static DirtyBounds bounds_of(const s16* px, const s16* py, u8 n) {
		DirtyBounds b {true, px[0], py[0], px[0], py[0]};
		for (u8 i = 1u; i < n; ++i) {
			if (px[i] < b.x0) b.x0 = px[i];
			if (py[i] < b.y0) b.y0 = py[i];
			if (px[i] > b.x1) b.x1 = px[i];
			if (py[i] > b.y1) b.y1 = py[i];
		}
		return b;
	}

	static bool face_front_facing_2d(const s16* px, const s16* py, const u8* idx, u8 n) {
#if K090_CULL2D
		// Signo del área proyectada: con y hacia abajo, una cara exterior CCW queda
		// con área negativa. Evita el producto mixto 3D (6 `muls.w` más por cara).
		s32 area = 0;
		for (u8 i = 0u; i < n; ++i) {
			const u8 j = static_cast<u8>((i + 1u == n) ? 0u : (i + 1u));
			area += eng::math::mul_wide(px[idx[i]], py[idx[j]]) -
				eng::math::mul_wide(px[idx[j]], py[idx[i]]);
		}
		return area < 0;
#else
		(void)px;
		(void)py;
		(void)idx;
		(void)n;
		return true;
#endif
	}

	/// Limpia el rectángulo de `b` (+2 px de margen) en los cuatro planos, esperando
	/// cada blit. El rectángulo debe cubrir el objeto y su rastro anterior.
	void clear_box(eng::amiga::AmigaBackend& backend, eng::PlaneBytes planes,
		       const DirtyBounds& b) {
		if (!b.valid) return;
		const s16 x0 = static_cast<s16>(b.x0 > 2 ? b.x0 - 2 : 0);
		const s16 y0 = static_cast<s16>(b.y0 > 2 ? b.y0 - 2 : 0);
		const s16 x1 = static_cast<s16>(
			b.x1 < static_cast<s16>(kWidth - 2u) ? b.x1 + 2
							     : static_cast<s16>(kWidth - 1u));
		const s16 y1 = static_cast<s16>(
			b.y1 < static_cast<s16>(kWorldH - 2u) ? b.y1 + 2
							      : static_cast<s16>(kWorldH - 1u));
		const u16 wx0 = static_cast<u16>(x0) & 0xfff0u;
		const u16 wx1 = static_cast<u16>(x1) | 0x000fu;
		const u16 words = static_cast<u16>((wx1 - wx0 + 16u) >> 4u);
		for (u8 p = 0u; p < kPlanes; ++p) {
			const eng::PlaneBytes plane =
				planes.subspan(static_cast<u32>(p) * kPlaneBytes, kPlaneBytes);
			(void)backend.blitter_clear_rect(plane, kRowBytes, wx0, y0, words,
							 static_cast<u16>(y1 - y0 + 1), true);
		}
	}

	/// Culling barato en cámara: (b-a)x(c-a) · (origen-a) > 0. Las coordenadas de la
	/// escena caben en s16 tras la resta (~180) y el producto mixto en s32, así que
	/// se usa `mul_wide` (muls.w en 68000) sin la aritmética Fixed ancha de
	/// `mesh_traits::face_signed_area` (que enlaza `__muldi3`).
	static bool face_front_facing(const eng::math3d::Vec3& a, const eng::math3d::Vec3& b,
				      const eng::math3d::Vec3& c) {
		const s16 abx = static_cast<s16>(b.v[0].v - a.v[0].v);
		const s16 aby = static_cast<s16>(b.v[1].v - a.v[1].v);
		const s16 abz = static_cast<s16>(b.v[2].v - a.v[2].v);
		const s16 acx = static_cast<s16>(c.v[0].v - a.v[0].v);
		const s16 acy = static_cast<s16>(c.v[1].v - a.v[1].v);
		const s16 acz = static_cast<s16>(c.v[2].v - a.v[2].v);
		const s32 nx = eng::math::mul_wide(aby, acz) - eng::math::mul_wide(abz, acy);
		const s32 ny = eng::math::mul_wide(abz, acx) - eng::math::mul_wide(abx, acz);
		const s32 nz = eng::math::mul_wide(abx, acy) - eng::math::mul_wide(aby, acx);
		const s32 facing = eng::math::mul_wide(static_cast<s16>(nx),
						       static_cast<s16>(-a.v[0].v)) +
				   eng::math::mul_wide(static_cast<s16>(ny),
						       static_cast<s16>(-a.v[1].v)) +
				   eng::math::mul_wide(static_cast<s16>(nz),
						       static_cast<s16>(-a.v[2].v));
		return facing > 0;
	}

	/// Transforma+proyecta todos los vértices de un sólido una sola vez. Devuelve
	/// `false` si algún vértice queda detrás de near (no ocurre con esta cámara:
	/// z del poliedro ≥ 429 y del cubo ≥ 480).
	static bool project_vertices(const eng::math3d::Vec3* world, u8 n,
				     const eng::graphics::Camera3D<eng::coord>& cam, s16* px,
				     s16* py) {
		for (u8 v = 0u; v < n; ++v) {
			if (world[v].v[2] < cam.near_z) return false;
			const eng::graphics::CamVertex<eng::coord> cv {world[v].v[0], world[v].v[1],
								      world[v].v[2]};
			if (!eng::graphics::project_camera_vertex(cv, cam, px[v], py[v])) {
				return false;
			}
		}
		return true;
	}

	/// Ensambla una cara desde las proyecciones, la cullea por normal 3D y, si es
	/// visible, la añade al lote (Blitter/CPU) y marca su ranura para la tabla de
	/// aristas (silueta y cambio de color).
	void submit_face(const eng::math3d::Vec3* world, const s16* px, const s16* py,
			 const u8* idx, u8 n, u8 color, u8 slot, DirtyBounds& box,
			 eng::graphics::PlaneFillBuilder<kMaxFaces>& fb) {
		m_vis[slot] = false;
		if (n < 3u || n > kMaxFaceVerts || m_face_count >= kMaxFaces) return;
		if (!face_front_facing(world[idx[0]], world[idx[1]], world[idx[2]])) return;
		s16* xs = m_fx[m_face_count];
		s16* ys = m_fy[m_face_count];
		s16 bx0 = px[idx[0]], bx1 = px[idx[0]];
		s16 by0 = py[idx[0]], by1 = py[idx[0]];
		for (u8 k = 0u; k < n; ++k) {
			const u8 v = idx[k];
			xs[k] = px[v];
			ys[k] = py[v];
			if (xs[k] < bx0) bx0 = xs[k];
			if (xs[k] > bx1) bx1 = xs[k];
			if (ys[k] < by0) by0 = ys[k];
			if (ys[k] > by1) by1 = ys[k];
		}
		// Un sliver de 1 px deja una arista suelta en el contorno ONEDOT.
		if (bx1 - bx0 < 2 || by1 - by0 < 2) return;
		if (eng::graphics::screen_area2(xs, ys, n) == 0) return;
		if (!fb.submit(xs, ys, n, color)) return;
		m_fn[m_face_count] = n;
		m_fc[m_face_count] = color;
		++m_face_count;
		m_vis[slot] = true;
		m_fcol[slot] = color;
		const DirtyBounds face {true, bx0, by0, bx1, by1};
		(void)unite(m_cur, face);
		(void)unite(box, face);
	}

	/// Construye la tabla de aristas (v0,v1,f0,f1) de una malla cerrada. Devuelve 0 si
	/// una arista aparece en más de dos caras o no cabe (la malla no es cerrada).
	template <u8 Cols>
	static u8 build_edges(const u8 (&faces)[][Cols], const u8* lens, u8 n_faces,
			      MeshEdge* out, u8 cap) {
		u8 count = 0u;
		for (u8 f = 0u; f < n_faces; ++f) {
			const u8 n = lens != nullptr ? lens[f] : Cols;
			for (u8 i = 0u; i < n; ++i) {
				const u8 a = faces[f][i];
				const u8 b = faces[f][i + 1u == n ? 0u : i + 1u];
				u8 e = 0u;
				for (; e < count; ++e) {
					if ((out[e].v0 == a && out[e].v1 == b) ||
					    (out[e].v0 == b && out[e].v1 == a)) {
						break;
					}
				}
				if (e == count) {
					if (count >= cap) return 0u;
					out[count++] = MeshEdge {a, b, f, 0xff};
				} else {
					if (out[e].f1 != 0xff) return 0u;
					out[e].f1 = f;
				}
			}
		}
		return count;
	}

	/// Reúne las aristas visibles: color = XOR de las caras adyacentes visibles o el de
	/// la única visible (frontera contra el fondo). `base` = primera ranura del sólido.
	u8 gather_edges(const MeshEdge* es, u8 n, u8 base, const s16* px, const s16* py,
			eng::retro::OutlineEdge* out, u8 cap) const {
		u8 w = 0u;
		for (u8 i = 0u; i < n && w < cap; ++i) {
			const MeshEdge& e = es[i];
			if (e.f1 == 0xff) continue;
			const bool a = m_vis[base + e.f0];
			const bool b = m_vis[base + e.f1];
			if (!a && !b) continue;
			u8 c = 0u;
			if (a && b) {
				c = static_cast<u8>(m_fcol[base + e.f0] ^ m_fcol[base + e.f1]);
			} else {
				c = m_fcol[base + (a ? e.f0 : e.f1)];
			}
			if (c == 0u) continue;
			out[w++] = eng::retro::OutlineEdge {px[e.v0], py[e.v0], px[e.v1],
							    py[e.v1], c};
		}
		return w;
	}

	/// Valida la tabla del poliedro: índices, cada arista en dos caras, Euler=2,
	/// caras planas, normales exteriores y radio constante.
	bool mesh_is_closed() const {
		u8 elo[60] {};
		u8 ehi[60] {};
		u8 use[60] {};
		u16 edges = 0u;
		for (u8 f = 0u; f < 32u; ++f) {
			const u8 n = kPolyFaceLen[f];
			if (n != 3u && n != 5u) return false;
			for (u8 i = 0u; i < n; ++i) {
				const u8 a = kPolyFaceIdx[f][i];
				if (a >= 30u) return false;
				const u8 b = kPolyFaceIdx[f][(i + 1u) % n];
				const u8 lo = a < b ? a : b;
				const u8 hi = a < b ? b : a;
				u16 found = 0xffffu;
				for (u16 e = 0u; e < edges; ++e) {
					if (elo[e] == lo && ehi[e] == hi) {
						found = e;
						break;
					}
				}
				if (found == 0xffffu) {
					if (edges >= 60u) return false;
					elo[edges] = lo;
					ehi[edges] = hi;
					use[edges] = 1u;
					++edges;
				} else {
					++use[found];
				}
			}
			// Normal del primer triángulo: exterior y plano para los demás vértices.
			const eng::math3d::Vec3& p0 = kPolyVerts[kPolyFaceIdx[f][0]];
			const eng::math3d::Vec3& p1 = kPolyVerts[kPolyFaceIdx[f][1]];
			const eng::math3d::Vec3& p2 = kPolyVerts[kPolyFaceIdx[f][2]];
			const s32 e1x = static_cast<s32>(p1.v[0].v) - p0.v[0].v;
			const s32 e1y = static_cast<s32>(p1.v[1].v) - p0.v[1].v;
			const s32 e1z = static_cast<s32>(p1.v[2].v) - p0.v[2].v;
			const s32 e2x = static_cast<s32>(p2.v[0].v) - p0.v[0].v;
			const s32 e2y = static_cast<s32>(p2.v[1].v) - p0.v[1].v;
			const s32 e2z = static_cast<s32>(p2.v[2].v) - p0.v[2].v;
			const s32 nx = e1y * e2z - e1z * e2y;
			const s32 ny = e1z * e2x - e1x * e2z;
			const s32 nz = e1x * e2y - e1y * e2x;
			if (nx * p0.v[0].v + ny * p0.v[1].v + nz * p0.v[2].v <= 0) return false;
			for (u8 i = 1u; i < n; ++i) {
				const eng::math3d::Vec3& q = kPolyVerts[kPolyFaceIdx[f][i]];
				const s32 dx = static_cast<s32>(q.v[0].v) - p0.v[0].v;
				const s32 dy = static_cast<s32>(q.v[1].v) - p0.v[1].v;
				const s32 dz = static_cast<s32>(q.v[2].v) - p0.v[2].v;
				const s32 plane = nx * dx + ny * dy + nz * dz;
				// |n| ~ 2,6k; 12k equivale a ~4,6 unidades de desvío.
				if (plane > 12000 || plane < -12000) return false;
			}
			const s32 r2 = p0.v[0].v * p0.v[0].v + p0.v[1].v * p0.v[1].v +
				       p0.v[2].v * p0.v[2].v;
			if (r2 < 89 * 89 || r2 > 92 * 92) return false;
		}
		if (edges != 60u) return false;
		for (u16 e = 0u; e < edges; ++e) {
			if (use[e] != 2u) return false;
		}
		return true;
	}

	/// Comprueba que las dos siluetas no se tocan en ninguna fase de la órbita y
	/// que todo cabe en el viewport (cotas conservadoras, con 2 px de margen).
	bool orbit_separation_ok() const {
		const s32 poly_r = (kPolyR * kFocal) / kPolyZ + 2;
		const s32 cube_r_world = (kCubeHalf * 1732) / 1000 + 1; // 24·√3 redondeado
		for (u16 i = 0u; i < 512u; ++i) {
			const u16 phase = static_cast<u16>(i * 8u);
			const s32 sinp = eng::retro::kSinQ12[phase & 4095u];
			const s32 cosp = eng::retro::kSinQ12[(phase + 1024u) & 4095u];
			const s32 x = (kOrbitA * cosp) >> 12;
			const s32 y = (kOrbitB * sinp) >> 12;
			const s32 z = kPolyZ +
				      ((kOrbitZWob *
					eng::retro::kSinQ12[(phase * 2u) & 4095u]) >>
				       12);
			if (z <= kNearZ) return false;
			const s32 px = (x * kFocal) / z;
			const s32 py = (y * kFocal) / z;
			const s32 cube_r = (cube_r_world * kFocal) / z + 2;
			const s32 need = cube_r + poly_r;
			if (px * px + py * py < need * need) return false;
			if (px + cube_r > kHalfW - 1 || py + cube_r > kHalfH - 1) return false;
			if (px - cube_r < -static_cast<s32>(kHalfW) + 1 ||
			    py - cube_r < -static_cast<s32>(kHalfH) + 1) {
				return false;
			}
		}
		return true;
	}

	// --- HUD ----------------------------------------------------------------------
	void draw_hud_static() {
		playfield::Surface hud {m_pf,
					playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		char label[20] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		char text[64] {};
		u8 n = str_cat(text, 0u, "090 POLYHEDRA ORBIT ");
		n = str_cat(text, n, label);
		pad_to(text, n, kHudChars);
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY + 4), text, kHudColor);
	}

	void draw_hud_dynamic(u32 frame) {
		ENG_PROF_BEGIN(kProfHud);
		playfield::Surface hud {m_pf,
					playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		u32 fps10 = 0u;
		if (m_fps.update(frame, eng::debug::prof_clock(), fps10)) {
			m_fps10 = static_cast<u16>(fps10);
		}
		char buf[8] {};
		char text[64] {};
		u8 n = str_cat(text, 0u, "FRAME ");
		(void)u16_to_str(static_cast<u16>(frame & 0xffffu), buf);
		n = str_cat(text, n, buf);
		n = str_cat(text, n, " FPS ");
		(void)u16_to_str(static_cast<u16>(m_fps10 / 10u), buf);
		n = str_cat(text, n, buf);
		n = str_cat(text, n, ".");
		(void)u16_to_str(static_cast<u16>(m_fps10 % 10u), buf);
		n = str_cat(text, n, buf);
		n = str_cat(text, n, " V");
		(void)u16_to_str(static_cast<u16>(m_vis_count), buf);
		n = str_cat(text, n, buf);
		pad_to(text, n, kHudChars);
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY + 22), text, kHudColor);
		ENG_PROF_END(kProfHud);
	}

	bool m_ready = false;
	scene::Scene m_scene {};
	playfield::ContiguousPlayfield m_pf {};
	eng::debug::HudFpsWindow m_fps {};
#if K090_CPU_FILL == 2 || K090_CPU_FILL == 3
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_face_mask {};
#endif
	// Patrón del entramado (80 B) + palabra de descarte para el primer píxel de
	// cada línea del modo EOR (2 B). El bloque existe siempre: el descarte también.
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_scratch {};
	MeshEdge m_poly_edges[kPolyEdgeCap] {};
	MeshEdge m_cube_edges[kCubeEdgeCap] {};
	u8 m_poly_edge_count = 0u;
	u8 m_cube_edge_count = 0u;
	bool m_vis[kFaceSlots] {};
	u8 m_fcol[kFaceSlots] {};
	u16 m_phase = K090_START_PHASE;
	u16 m_fps10 = 0u;
	u8 m_vis_count = 0u;
	u8 m_face_count = 0u;
	DirtyBounds m_cur {};
	DirtyBounds m_pbox {};
	DirtyBounds m_cbox {};
	DirtyBounds m_pbox1 {};
	DirtyBounds m_pbox2 {};
	DirtyBounds m_cbox1 {};
	DirtyBounds m_cbox2 {};
	DirtyBounds m_prev1 {};
	DirtyBounds m_prev2 {};
	s16 m_fx[kMaxFaces][kMaxFaceVerts] {};
	s16 m_fy[kMaxFaces][kMaxFaceVerts] {};
	u8 m_fn[kMaxFaces] {};
	u8 m_fc[kMaxFaces] {};
};

} // namespace

int main() {
	eng::amiga::ensure_boot();
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	PolyhedraDemo demo {};
	eng::Engine engine {backend, demo};
	// Bajo el SO del runner la espera por IRQ puede perderse (familia DT-011): el
	// sondeo de VPOSR es el modo estable de las demos largas (080/119).
	engine.run_frames_polling(0xffffu);
	return 0;
}

