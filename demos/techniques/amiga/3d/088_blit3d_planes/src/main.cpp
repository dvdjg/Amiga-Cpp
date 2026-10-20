// ============================================================================
// Demo 088 — Sonda estática de clipping 3D y relleno por plano
// ============================================================================
//
// La sonda descarta caras traseras y entrega las visibles al relleno compuesto por plano.
// La ruta O1 predeterminada proyecta una vez los ocho vértices del cubo y recorta en el
// rectángulo seguro del backend; `K_CLIP_PROJECT_BEFORE_SCREEN_CLIP=0` conserva el camino
// de referencia que recorta cada cara en espacio de cámara antes de proyectarla.
// `K_CLIP_CASE` fija un borde/esquina por build; `K_CLIP_ORIENTATION=1` mantiene una
// inclinación policromada fija para comprobar las caras, sin mezclarlo con giro temporal.
//
// Escena: un cubo de seis caras con color alterno sobre DOS bitplanes:
//
//   índice 0 = negro (fondo)   bitplanes 00
//   índice 1 = azul            bitplane 0
//   índice 2 = rojo            bitplane 1
//   índice 3 = magenta         bitplanes 0 y 1
//
// El relleno por plano agrupa las aristas según el bit de color y hace un contorno
// ONEDOT/EOR más un FILL_XOR por plano. Las aristas compartidas del mismo plano se
// cancelan por paridad; una cara magenta participa en los dos planos. El trazador se
// restringe al rectángulo seguro (`min_x=1`) para que la coordenada previa a x=0 no
// escriba en la fila anterior; convención y fuente: `../WinUAE-DBG/blitter.cpp:929-969`
// y `docs/reference/amiga/techniques/blitter-line-subpixel-fill.md` §2.1.
//
// Dentro de cada pasada los interiores visibles del cubo convexo no se solapan; las
// caras comparten solo aristas, que son el contorno de la silueta o una junta real.
//
// HUD: franja inferior (filas 232..255) en el mismo bitmap de 2 planos, texto por
// palabras con `DiffText` para el contador de frames.

#include <eng/api/api.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/diff_text.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/retro/fixed_mesh.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/gfx3d.hpp>

#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::u16;
using eng::s32;
using eng::u32;
using eng::u8;

// --- Display: 320x256, DOS planos + franja inferior en el mismo bitmap ---------
constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u8 kPlanes = 2u;
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kWorldH = 232u;
constexpr u16 kBandY = kWorldH;
constexpr u16 kBandH = kHeight - kWorldH;

// Colores del ejemplo: negro 0, azul 1, rojo 2, magenta 3.
constexpr eng::Palette32 kPalette {{
	0x000, 0x00f, 0xf00, 0xf0f,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
}};

constexpr eng::debug::Version kVersion {1u, 0u}; // build inyectado por build-demo.sh



enum : eng::u32 {
	kBootMemory = 0x00008801u,
	kBootCompose = 0x00008802u,
	kBootMask = 0x00008803u,
	kBufferCheckEqual = 1u << 8u, // detail: las dos páginas contienen los mismos píxeles 3D
	kBufferCheckDifferent = 1u << 9u, // detail: el redraw estático diverge entre páginas
};

/// Cubo 3D de medio lado `kHalf` en espacio de camara (+x derecha, +y abajo, +z
/// delante), centrado en z = kZ0. Caras CCW vistas desde fuera con un color por cara
/// (1..3) sobre 2 planos: las caras visibles de un convexo son disjuntas en pantalla,
/// que es el contrato del relleno por plano.
constexpr s16 kHalf = 150;
constexpr s16 kZ0 = 900;
constexpr s16 kFocal = 340;
constexpr s16 kNearZ = 64;

#ifndef K_CLIP_CASE
#define K_CLIP_CASE 0
#endif
#ifndef K_CLIP_ANIMATE
#define K_CLIP_ANIMATE 0
#endif
#ifndef K_CLIP_BUFFERS
#if K_CLIP_ANIMATE
#define K_CLIP_BUFFERS 2
#else
#define K_CLIP_BUFFERS 1
#endif
#endif
#ifndef K_CLIP_REPEAT
#define K_CLIP_REPEAT 0
#endif
#ifndef K_CLIP_ORIENTATION
#define K_CLIP_ORIENTATION 0
#endif
#ifndef K_CLIP_PROJECT_BEFORE_SCREEN_CLIP
#define K_CLIP_PROJECT_BEFORE_SCREEN_CLIP 1
#endif
#ifndef K_CLIP_HUD_UPDATE_SHIFT
#define K_CLIP_HUD_UPDATE_SHIFT 3
#endif

constexpr s16 kEdgeClipX = 450; // cruza el plano lateral con el cubo de radio 150 y z=900±150
constexpr s16 kEdgeClipLeftX = 560; // O1=45°: compensa la perspectiva y hace cruzar la esquina izquierda.
constexpr s16 kEdgeClipTopY = 336; // O1=45° cruza el frustum superior en la pose centrada.
constexpr s16 kEdgeClipBottomY = 288; // La perspectiva necesita menos traslación para cruzar abajo.
constexpr s16 kOutsideRightX = 800; // x mínimo=650 supera x visible máximo≈494 incluso a z=1050
constexpr s16 kOutsideVerticalY = 700; // |y| mínimo=550 supera y visible máximo≈358 incluso a z=1050
constexpr u16 kStaticTilt = 512u; // 1/8 de 4096 pasos por vuelta = 45° (`retro/fixed_trig.hpp`).
constexpr s16 kPathStepX = 8; // avance horizontal por update (unidades de cámara, sin división).
constexpr s16 kPathStepY = 4; // avance vertical por update (unidades de cámara, sin división).
constexpr u16 kRotateStepX = 11u; // incremento de ángulo por update, índice de `retro::Turns`.
constexpr u16 kRotateStepY = 17u; // incremento de ángulo por update, índice de `retro::Turns`.
constexpr u16 kRotateStepZ = 5u; // incremento de ángulo por update, índice de `retro::Turns`.

/// Traslación estática en coordenadas de cámara para aislar una frontera por ejecución.
struct ClipPose {
	s16 x; ///< Desplazamiento horizontal en unidades del mundo de cámara.
	s16 y; ///< Desplazamiento vertical en unidades del mundo de cámara.
};

/// Casos pequeños que recorren centro, cada borde, cada esquina y una salida completa.
constexpr ClipPose kClipCases[] = {
	{0, 0},
	{kEdgeClipX, 0}, {-kEdgeClipX, 0}, {0, -kEdgeClipTopY}, {0, kEdgeClipBottomY},
	{kEdgeClipX, -kEdgeClipTopY}, {-kEdgeClipLeftX, -kEdgeClipTopY},
	{kEdgeClipX, kEdgeClipBottomY}, {-kEdgeClipLeftX, kEdgeClipBottomY},
	{kOutsideRightX, 0}, {-kOutsideRightX, 0}, {0, -kOutsideVerticalY},
	{0, kOutsideVerticalY}};
/// Etiquetas HUD que permiten comprobar qué postura está compilada sin inferirla de la imagen.
constexpr const char* kClipCaseLabels[] = {"CASE 0", "CASE 1", "CASE 2", "CASE 3", "CASE 4",
						  "CASE 5", "CASE 6", "CASE 7", "CASE 8", "CASE 9",
						  "CASE 10", "CASE 11", "CASE 12"};
/// Etiqueta corta de HUD con el número de páginas que `Scene::compose` aceptó realmente.
constexpr const char* kBufferLabels[] = {"", "B1", "B2"};
/// Etiqueta del modo de actualización usado por la sonda estática.
constexpr const char* kRepeatLabels[] = {"R0", "R1"};
/// Etiqueta de la trayectoria temporal activada en el build animado.
constexpr const char* kMotionLabel = "PATH";
/// Dígitos de hexadecimal para formatear el contador sin división en el bucle de frame.
constexpr char kHexDigits[] = "0123456789ABCDEF";
/// Etiqueta de orientación: `O0` frontal e `O1` inclinada, ambas inmóviles.
constexpr const char* kOrientationLabels[] = {"O0", "O1"};
static_assert(K_CLIP_CASE >= 0 && K_CLIP_CASE < static_cast<int>(sizeof(kClipCases) / sizeof(kClipCases[0])),
	      "K_CLIP_CASE debe seleccionar una postura estática de kClipCases");
static_assert(K_CLIP_BUFFERS == 1 || K_CLIP_BUFFERS == 2,
	      "K_CLIP_BUFFERS solo admite una o dos superficies en esta sonda");
static_assert(K_CLIP_ANIMATE == 0 || K_CLIP_ANIMATE == 1,
	      "K_CLIP_ANIMATE solo admite 0 (postura estática) o 1 (trayectoria rotatoria)");
static_assert(!K_CLIP_ANIMATE || K_CLIP_BUFFERS == 2,
	      "la trayectoria rotatoria requiere doble buffer para evitar tearing");
static_assert(K_CLIP_REPEAT == 0 || K_CLIP_REPEAT == 1,
	      "K_CLIP_REPEAT debe ser 0 (una pasada) o 1 (redibujar la pose fija)");
static_assert(K_CLIP_ORIENTATION == 0 || K_CLIP_ORIENTATION == 1,
	      "K_CLIP_ORIENTATION debe ser 0 (frontal) o 1 (pose policromada fija)");
static_assert(K_CLIP_PROJECT_BEFORE_SCREEN_CLIP == 0 || K_CLIP_PROJECT_BEFORE_SCREEN_CLIP == 1,
	      "K_CLIP_PROJECT_BEFORE_SCREEN_CLIP debe ser 0 o 1");
static_assert(K_CLIP_HUD_UPDATE_SHIFT >= 0 && K_CLIP_HUD_UPDATE_SHIFT <= 4,
	      "K_CLIP_HUD_UPDATE_SHIFT selecciona un refresco del HUD cada 1, 2, 4, 8 o 16 frames");
constexpr ClipPose kClipPose = kClipCases[K_CLIP_CASE];
using CamVec = eng::math3d::Vec3t<eng::coord>;

constexpr CamVec kCube[8] = {
	{-kHalf, -kHalf, -kHalf}, {kHalf, -kHalf, -kHalf}, {kHalf, kHalf, -kHalf}, {-kHalf, kHalf, -kHalf},
	{-kHalf, -kHalf, kHalf},  {kHalf, -kHalf, kHalf},  {kHalf, kHalf, kHalf},  {-kHalf, kHalf, kHalf},
};
// near(0,1,2,3) far(5,4,7,6) right(1,5,6,2) left(4,0,3,7) top(4,5,1,0) bottom(3,2,6,7)
// Caras con winding CCW visto desde FUERA (normal = cross(e1,e2) hacia afuera).
constexpr u8 kCubeFaces[6][4] = {
	{0, 3, 2, 1}, {4, 5, 6, 7}, {1, 2, 6, 5}, {0, 4, 7, 3}, {0, 1, 5, 4}, {3, 7, 6, 2},
};
constexpr u8 kCubeColors[6] = {1u, 2u, 3u, 1u, 2u, 3u};

struct Blit3dPlanesDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = K_CLIP_BUFFERS; // la animación usa dos páginas; la sonda estática permite una.
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::display(res), scene::palette(kPalette, 0u, 4u))) {
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<eng::u32>(m_scene.config_error().code));
			return;
		}
		// Máscara 1 bit de cookie-cut (Chip: la lee el Blitter; una por buffer de plano).
		m_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(kPlaneBytes);
		if (!m_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootMask);
			return;
		}
		if constexpr (!K_CLIP_ANIMATE && K_CLIP_ORIENTATION == 1) {
			// Una sola postura isométrica fija para verificar varias caras sin añadir giro temporal.
			eng::math3d::load_rotate(m_model.m, eng::retro::turns(kStaticTilt),
						 eng::retro::turns(kStaticTilt), eng::retro::turns(0u));
		}
		m_scene.takeover(backend);
		for (u8 b = 0u; b < m_scene.buffer_count(); ++b) {
			const eng::PlaneBytes planes = m_scene.buffer(b);
			(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);
			(void)m_pf.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth,
					    kHeight, kPlanes, kPlaneBytes);
			draw_hud_static();
		}
		m_ready = true;
		ENG_PROF_INIT(13u); // 0..2 demo + 10..12 interior del relleno (engine)
		// `Scene::commit` publica `m_back` y rota la página (scene.hpp:396-410): completar
		// cada buffer antes de READY impide que el primer flip muestre una página vacía.
		for (u8 b = 0u; b < m_scene.buffer_count(); ++b) {
			update(backend, context);
		}
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) return;
#if !K_CLIP_REPEAT && !K_CLIP_ANIMATE
		if (m_probe_draws >= m_scene.buffer_count()) return;
#endif
		ENG_PROF_FRAME();
		// **Publicacion diferida al INICIO del update**: el raster muestra siempre el buffer
		// completo anterior; el trasero se pinta despues (evita ver buffers en construccion).
		ENG_PROF_BEGIN(0u);
		m_scene.commit();
		const eng::PlaneBytes planes = m_scene.back();
		(void)m_pf.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth, kHeight,
				    kPlanes, kPlaneBytes);

		const u32 frame = context.frame.frame_index;
		if constexpr (K_CLIP_ANIMATE) {
			// La matriz sale de los ángulos acumulados, nunca de un producto 32-bit por frame.
			eng::math3d::load_rotate(m_model.m, eng::retro::turns(m_rotate_x),
						 eng::retro::turns(m_rotate_y), eng::retro::turns(m_rotate_z));
		}
		// mesh_transform sobrescribe los ocho vértices; no limpiar el scratch cuesta un memset por frame.
		CamVec world[8];
		eng::math3d::mesh_transform(eng::Span<const CamVec>(kCube, 8u), m_model,
					    eng::Span<CamVec>(world, 8u));
		// Centro en camara: +z0 y la camara con focal/centro de pantalla.
		using CT = eng::math::scalar_traits<eng::coord>;
		eng::graphics::Camera3D<eng::coord> cam {};
		cam.focal = CT::from_int(kFocal);
		cam.cx = 160;
		cam.cy = 116;
		cam.near_z = CT::from_int(kNearZ);
		cam.half_w = CT::from_int(160);
		cam.half_h = CT::from_int(116);
		// Cada ejecución usa una traslación fija; ambas rutas conservan el límite del bitmap
		// y difieren solo en si los laterales se recortan en cámara o tras la proyección.
		#if K_CLIP_ANIMATE
		const s16 ox = m_path_x;
		const s16 oy = m_path_y;
		#else
		const s16 ox = kClipPose.x;
		const s16 oy = kClipPose.y;
		#endif
		// Vertices de camara como Vec3t (dominio): el culling por normal de mesh3d sirve
		// para CUALQUIER convexo con winding CCW hacia fuera (icosaedro irregular incluido).
		eng::math3d::Vec3t<eng::coord> wv[8];
		for (u8 i = 0u; i < 8u; ++i) {
			wv[i] = eng::math3d::Vec3t<eng::coord> {{world[i].v[0] + CT::from_int(ox),
							    world[i].v[1] + CT::from_int(oy),
							    world[i].v[2] + CT::from_int(kZ0)}};
		}
		// Fast path: si las 8 esquinas estan dentro del frustum, ninguna cara necesita clip
		// y la proyeccion se hace UNA vez por vertice (8), no por cara (hasta 24).
		bool all_inside = eng::graphics::camera_contains_vertices(cam, wv, 8u);
		s16 px[8], py[8];
		#if K_CLIP_PROJECT_BEFORE_SCREEN_CLIP
		// El test de near protege la proyección; el backend vuelve a recortar cada cara al
		// rectángulo antes del trazador de línea/area fill (WinUAE-DBG `blitter.cpp:785-803,
		// 929-969`; AHRM 3.ª, cap. 6, “Area Fill Mode” y “Line Mode”).
		bool screen_projectable = true;
		for (u8 i = 0u; i < 8u; ++i) {
			const eng::graphics::CamVertex<eng::coord> cv {wv[i].v[0], wv[i].v[1], wv[i].v[2]};
			screen_projectable = eng::graphics::project_camera_vertex(cv, cam, px[i], py[i]) &&
					     (wv[i].v[2] >= cam.near_z) && screen_projectable;
		}
		#endif
		if (all_inside) {
#if !K_CLIP_PROJECT_BEFORE_SCREEN_CLIP
			for (u8 i = 0u; i < 8u; ++i) {
				const eng::graphics::CamVertex<eng::coord> cv {wv[i].v[0], wv[i].v[1],
									      wv[i].v[2]};
				all_inside = all_inside && eng::graphics::project_camera_vertex(
								  cv, cam, px[i], py[i]);
				// Contrato del Blitter: NINGUN vertice fuera del bitmap (x=320 corrompe).
				if (px[i] < 0) px[i] = 0;
				if (px[i] > static_cast<s16>(kWidth - 1u)) px[i] = static_cast<s16>(kWidth - 1u);
				if (py[i] < 0) py[i] = 0;
				if (py[i] > static_cast<s16>(kWorldH - 1u)) py[i] = static_cast<s16>(kWorldH - 1u);
			}
#else
			for (u8 i = 0u; i < 8u; ++i) {
				if (px[i] < 0) px[i] = 0;
				if (px[i] > static_cast<s16>(kWidth - 1u)) px[i] = static_cast<s16>(kWidth - 1u);
				if (py[i] < 0) py[i] = 0;
				if (py[i] > static_cast<s16>(kWorldH - 1u)) py[i] = static_cast<s16>(kWorldH - 1u);
			}
#endif
		}

		// Vértices por cara: el builder guarda VISTAS, no copia; deben vivir hasta el fill.
		// Cada cara inicializa sus primeros `m` vértices antes de area/culling/submit.
		s16 xs[6][16];
		s16 ys[6][16];
		ENG_PROF_END(0u);
		ENG_PROF_BEGIN(1u);
		eng::graphics::PlaneFillBuilder<6> fb {};
		m_submitted = 0u;
		s16 bx0 = 32767, by0 = 32767, bx1 = -32768, by1 = -32768;
		const eng::math3d::Vec3t<eng::coord> cam_origin {};
		for (u8 fi = 0u; fi < 6u; ++fi) {
			// Culling por NORMAL 3D (camara en el origen): cara trasera fuera ANTES de
			// proyectar/recortar; general para todo convexo con caras CCW desde fuera.
			if (!eng::math3d::face_visible(wv[kCubeFaces[fi][0]], wv[kCubeFaces[fi][1]],
						  wv[kCubeFaces[fi][2]], cam_origin)) {
				continue;
			}
			u8 m = 4u;
			if (all_inside) {
				for (u8 k = 0u; k < 4u; ++k) {
					const u8 v = kCubeFaces[fi][k];
					xs[fi][k] = px[v];
					ys[fi][k] = py[v];
				}
			} else {
				eng::graphics::CamVertex<eng::coord> cv[4];
				for (u8 k = 0u; k < 4u; ++k) {
					const auto& v = wv[kCubeFaces[fi][k]];
					cv[k] = eng::graphics::CamVertex<eng::coord> {v.v[0], v.v[1], v.v[2]};
				}
				#if K_CLIP_PROJECT_BEFORE_SCREEN_CLIP
				if (screen_projectable) {
					for (u8 k = 0u; k < 4u; ++k) {
						const u8 v = kCubeFaces[fi][k];
						xs[fi][k] = px[v];
						ys[fi][k] = py[v];
					}
				} else {
				#endif
					eng::graphics::CamVertex<eng::coord> cl[16];
					m = eng::graphics::clip_camera_polygon(cv, 4u, cam, cl, 16u);
					if (m < 3u) continue;
					bool ok = true;
					for (u8 k = 0u; k < m; ++k) {
						ok = ok && eng::graphics::project_camera_vertex(
								cl[k], cam, xs[fi][k], ys[fi][k]);
					}
					if (!ok) continue;
				#if K_CLIP_PROJECT_BEFORE_SCREEN_CLIP
				}
				#endif
			}
			// Back-face culling por winding proyectado: en este sistema (+y abajo) una cara que mira a la camara proyecta area NEGATIVA (HOST-481); area >= 0 es trasera o degenerada.
			// Descarta slivers (1 px de ancho/alto): el contorno ONEDOT deja su arista
			// como linea suelta en el borde.
			s16 sx0 = xs[fi][0], sx1 = xs[fi][0], sy0 = ys[fi][0], sy1 = ys[fi][0];
			for (u8 k = 1u; k < m; ++k) {
				if (xs[fi][k] < sx0) sx0 = xs[fi][k];
				if (xs[fi][k] > sx1) sx1 = xs[fi][k];
				if (ys[fi][k] < sy0) sy0 = ys[fi][k];
				if (ys[fi][k] > sy1) sy1 = ys[fi][k];
			}
			if (sx1 - sx0 < 2 || sy1 - sy0 < 2) continue;
			if (eng::graphics::screen_area2(xs[fi], ys[fi], m) >= 0) continue;
			if (fb.submit(xs[fi], ys[fi], m, kCubeColors[fi])) {
				++m_submitted;
				for (u8 k = 0u; k < m; ++k) {
					if (xs[fi][k] < bx0) bx0 = xs[fi][k];
					if (xs[fi][k] > bx1) bx1 = xs[fi][k];
					if (ys[fi][k] < by0) by0 = ys[fi][k];
					if (ys[fi][k] > by1) by1 = ys[fi][k];
				}
			}
		}
		ENG_PROF_END(1u);
		ENG_PROF_BEGIN(2u);
		// Dirty rect = union del bbox actual y el del frame anterior (borra el rastro).
		s16 dx0 = bx0, dy0 = by0, dx1 = bx1, dy1 = by1;
		// Doble buffer: el buffer de este frame se uso hace DOS frames. Hay que limpiar
		// la union de las dos posiciones anteriores (N-1 y N-2) ademas de la actual.
		auto grow = [&](s16 x0, s16 y0, s16 x1, s16 y1) {
			if (x0 < dx0) dx0 = x0;
			if (y0 < dy0) dy0 = y0;
			if (x1 > dx1) dx1 = x1;
			if (y1 > dy1) dy1 = y1;
		};
		if (m_dirty_x1 >= m_dirty_x0 && m_dirty_y1 >= m_dirty_y0) {
			grow(m_dirty_x0, m_dirty_y0, m_dirty_x1, m_dirty_y1);
		}
		if (m_prev2_x1 >= m_prev2_x0 && m_prev2_y1 >= m_prev2_y0) {
			grow(m_prev2_x0, m_prev2_y0, m_prev2_x1, m_prev2_y1);
		}
		if (dx1 >= dx0 && dy1 >= dy0) {
			// El contorno ONEDOT/EOR puede escribir un píxel a la izquierda y una fila abajo
			// del bbox proyectado; limpiar también ese soporte evita rastros al alternar páginas
			// (WinUAE-DBG `blitter.cpp:929-969`; contrato y medición: `L1-003`).
			if (dx0 > 0) --dx0;
			if (dy1 < static_cast<s16>(kWorldH - 1u)) ++dy1;
			if (dx0 < 0) dx0 = 0;
			if (dy0 < 0) dy0 = 0;
			if (dx1 > static_cast<s16>(kWidth - 1u)) dx1 = static_cast<s16>(kWidth - 1u);
			if (dy1 > static_cast<s16>(kWorldH - 1u)) dy1 = static_cast<s16>(kWorldH - 1u);
			(void)backend.fill_polygons_by_plane_dirty(
				fb.faces().data(), fb.count(), planes, kRowBytes, kPlaneBytes, kPlanes,
				kWidth, kWorldH, static_cast<u16>(dx0), static_cast<u16>(dy0),
				static_cast<u16>(dx1), static_cast<u16>(dy1));
		}
		if (bx1 >= bx0) {
			m_prev2_x0 = m_dirty_x0; m_prev2_y0 = m_dirty_y0;
			m_prev2_x1 = m_dirty_x1; m_prev2_y1 = m_dirty_y1;
			m_dirty_x0 = bx0; m_dirty_y0 = by0; m_dirty_x1 = bx1; m_dirty_y1 = by1;
		}
		g_eng_run_status.detail = fb.count();
#if K_CLIP_BUFFERS == 2 && K_CLIP_REPEAT == 1 && !K_CLIP_ANIMATE
		// Solo el experimento de redraw calcula la igualdad completa de las páginas; el barrido
		// de 18 KiB excluye el HUD y se publica como bit diagnóstico para separar blit de captura.
		const bool pages_match = world_buffers_match();
		g_eng_run_status.detail |= pages_match ? kBufferCheckEqual : kBufferCheckDifferent;
#endif
		ENG_PROF_END(2u);
		draw_hud_dynamic(frame, m_scene.back_index());
#if K_CLIP_BUFFERS == 2 && K_CLIP_REPEAT == 1 && !K_CLIP_ANIMATE
		draw_buffer_match_status(pages_match);
#endif
		if (m_probe_draws < m_scene.buffer_count()) ++m_probe_draws;
		eng::debug::probe_when_ready(g_eng_run_status, frame);
		if constexpr (K_CLIP_ANIMATE) advance_animation();
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	/// Compara los píxeles del área 3D en ambas páginas, excluyendo la banda HUD que actualiza F<num>.
	bool world_buffers_match() const {
		const eng::PlaneBytes first = m_scene.buffer(0u);
		const eng::PlaneBytes second = m_scene.buffer(1u);
		const eng::u32 world_bytes = static_cast<eng::u32>(kRowBytes) * kWorldH;
		for (u8 p = 0u; p < kPlanes; ++p) {
			const eng::u32 plane_offset = static_cast<eng::u32>(p) * kPlaneBytes;
			for (eng::u32 i = 0u; i < world_bytes; ++i) {
				if (first.data()[plane_offset + i] != second.data()[plane_offset + i]) return false;
			}
		}
		return true;
	}

#if K_CLIP_BUFFERS == 2 && K_CLIP_REPEAT == 1 && !K_CLIP_ANIMATE
	/// Muestra el resultado del comparador de páginas en la segunda línea del HUD diagnóstico.
	void draw_buffer_match_status(bool matches) {
		playfield::Surface hud {m_pf, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		(void)hud.draw_text_words(256, static_cast<s16>(kBandY + 10), matches ? "OK" : "DIFF", 3u);
	}
#endif

	void draw_hud_static() {
		playfield::Surface hud {m_pf, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY), "088 BLIT3D PLANES", 3u);
#if K_CLIP_ANIMATE
		(void)hud.draw_text_words(192, static_cast<s16>(kBandY), kMotionLabel, 3u);
		(void)hud.draw_text_words(288, static_cast<s16>(kBandY), "AN", 3u);
#else
		(void)hud.draw_text_words(192, static_cast<s16>(kBandY), kClipCaseLabels[K_CLIP_CASE], 3u);
		(void)hud.draw_text_words(288, static_cast<s16>(kBandY), kRepeatLabels[K_CLIP_REPEAT], 3u);
		(void)hud.draw_text_words(304, static_cast<s16>(kBandY), kOrientationLabels[K_CLIP_ORIENTATION], 3u);
#endif
		(void)hud.draw_text_words(256, static_cast<s16>(kBandY), kBufferLabels[m_scene.buffer_count()], 3u);
#if K_CLIP_ANIMATE
		(void)hud.draw_text_words(304, static_cast<s16>(kBandY), "R1", 3u);
#endif
		char label[40] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY + 10), label, 3u);
	}

	/// Formats a low-word frame count as fixed-width hexadecimal on this back page's bucket change.
	void draw_hud_dynamic(u32 frame, u8 buffer_index) {
		const u32 bucket = frame >> K_CLIP_HUD_UPDATE_SHIFT;
		if (m_hud_bucket[buffer_index] == bucket) return;
		m_hud_bucket[buffer_index] = bucket;
		playfield::Surface hud {m_pf, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		const u32 interval = 1u << K_CLIP_HUD_UPDATE_SHIFT;
		const u16 value = static_cast<u16>(frame & ~(interval - 1u));
		char f[6] = {'F', kHexDigits[(value >> 12u) & 15u], kHexDigits[(value >> 8u) & 15u],
			     kHexDigits[(value >> 4u) & 15u], kHexDigits[value & 15u], '\0'};
		(void)m_frame_diff[buffer_index].paint(hud, 192, static_cast<s16>(kBandY + 10), f, 3u);
	}

	/// Advances a closed square path around the four viewport edges using additions only.
	void advance_animation() {
		m_rotate_x = static_cast<u16>(m_rotate_x + kRotateStepX);
		m_rotate_y = static_cast<u16>(m_rotate_y + kRotateStepY);
		m_rotate_z = static_cast<u16>(m_rotate_z + kRotateStepZ);
		switch (m_path_segment) {
		case 0u: // derecha → esquina superior derecha
			m_path_y = static_cast<s16>(m_path_y - kPathStepY);
			if (m_path_y <= -kEdgeClipTopY) {
				m_path_y = static_cast<s16>(-kEdgeClipTopY);
				m_path_segment = 1u;
			}
			break;
		case 1u: // superior derecha → superior izquierda
			m_path_x = static_cast<s16>(m_path_x - kPathStepX);
			if (m_path_x <= -kEdgeClipLeftX) {
				m_path_x = static_cast<s16>(-kEdgeClipLeftX);
				m_path_segment = 2u;
			}
			break;
		case 2u: // superior izquierda → inferior izquierda
			m_path_y = static_cast<s16>(m_path_y + kPathStepY);
			if (m_path_y >= kEdgeClipBottomY) {
				m_path_y = kEdgeClipBottomY;
				m_path_segment = 3u;
			}
			break;
		default: // inferior izquierda → inferior derecha → segmento superior
			m_path_x = static_cast<s16>(m_path_x + kPathStepX);
			if (m_path_x >= kEdgeClipX) { m_path_x = kEdgeClipX; m_path_segment = 0u; }
			break;
		}
	}

	scene::Scene m_scene {};
	/// Playfield de la escena, enlazado en init (mismo buffer que pinta el Copper).
	playfield::ContiguousPlayfield m_pf {};
	/// Una caché de diferencias de HUD por buffer evita alternar contadores viejos en doble buffer.
	playfield::DiffText<16> m_frame_diff[2] {};
	/// Each back page remembers its last HUD bucket so both pages advance without front writes.
	u32 m_hud_bucket[2] {0xffffffffu, 0xffffffffu};
	eng::Block<eng::MaskTag> m_mask {};
	u32 m_submitted = 0u;
	/// Transformación actual; estática en la sonda y reconstruida con `load_rotate` al animar.
	eng::math::Affine<3, eng::real, eng::coord> m_model =
		eng::math::Affine<3, eng::real, eng::coord>::identity();
	/// Páginas ya renderizadas; detiene la sonda al llenar cada buffer exactamente una vez.
	u8 m_probe_draws = 0u;
	/// Ángulos de 4096 pasos por vuelta; la suma de 16 bits envuelve según la tabla `retro::Turns`.
	u16 m_rotate_x = kStaticTilt, m_rotate_y = kStaticTilt, m_rotate_z = 0u;
	/// Traslación actual sobre la trayectoria cuadrada, en unidades de cámara.
	s16 m_path_x = kEdgeClipX, m_path_y = 0;
	/// Segmento actual: derecha→arriba→izquierda→abajo→derecha.
	u8 m_path_segment = 0u;
	s16 m_dirty_x0 = 1, m_dirty_y0 = 1, m_dirty_x1 = 0, m_dirty_y1 = 0;
	s16 m_prev2_x0 = 1, m_prev2_y0 = 1, m_prev2_x1 = 0, m_prev2_y1 = 0;
	bool m_ready = false;
};

Blit3dPlanesDemo g_game {};

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
