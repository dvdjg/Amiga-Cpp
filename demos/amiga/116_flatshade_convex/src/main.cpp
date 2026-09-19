// Demo 116 - flatshade-convex (IMPORTE FIEL de demoscene-repo-orig/effects/flatshade-convex)
//
// Objeto CONVEXO `pilka` girando, con SOMBREADO PLANO: se calcula la luz de cada
// cara (`update_face_visibility`: producto escalar normal??vista normalizado con la
// tabla `kInvSqrt`, sin sqrt en runtime), la visibilidad de aristas del solido
// convexo (`update_edge_visibility_convex`: XOR de la luz de las caras adyacentes,
// que cancela las aristas internas y deja silueta + aristas visibles) y se dibujan
// esas aristas por Blitter (`blitter_line_eor`, ONEDOT+EOR) replicadas en cada plano
// segun el color; despues se rellena el hueco con `blitter_area_fill` (area fill
// XOR). El recorrido del object model (`Object3D` + grupos por offsets de byte) y la
// proyeccion (`transform_vertices`) son el port 1:1 de `lib3d` y viven en
// `eng/platform/amiga/lib3d.hpp` (API reutilizable, con test host); aqui queda el dibujo Amiga
// (Blitter de lineas + area fill) y la orquestacion del efecto.
//
// Display y doble buffer: 256x256x4 con los registros del original
// (`SetupPlayfield(MODE_LORES,4,X(32),Y(0),256,256)`); doble buffer con swap de
// copperlist por frame.
#include <eng/platform/amiga/lib3d.hpp>
#include <eng/retro/lib2d.hpp>
#include <eng/platform/amiga/object3d.hpp>
#include <eng/platform/amiga/object3d_poly.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/ham_scene.hpp>
#include <eng/graphics/drivers/multi_buffered.hpp>
#include <eng/memory/arena.hpp>
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

// Perfilado por secciones: cabecera magica para localizarla por canal lateral y
// deltas de ciclos de CPU (contador del periferico de depuracion 0xB7E928).
// v[0]=clear v[1]=transform/culling v[2]=edges v[3]=fill v[4]=draw(edges+fill) v[5]=update total
struct EngProf {
	eng::u32 magic;
	eng::u32 v[16];
};
__attribute__((used)) volatile EngProf g_eng_prof { 0x50524f46u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u} };
}

// Argumentos de las rutinas asm de support/flatshade_asm.s. GLOBAL y `extern "C"`
// (fuera del namespace anonimo) para que el asm lo referencie, como g_fire_args.
extern "C" {
struct FlatShadeAsmArgs {
	void* obj;            // Object3D* (objdat=0, vertexGroups=4, edgeGroups=8, faceGroups=12, objectToWorld=38, camera=86)
	eng::u8* planes;      // base del buffer de bitplanes activo (fs_draw_edges)
	const eng::u16* invsqrt; // kInvSqrt[512]
	eng::s16* bbox;       // g_bbox[4] (bx0,bx1,by0,by1)
};
FlatShadeAsmArgs g_fs_args {};
void fs_update_face_visibility(void);
void fs_update_edge_visibility_convex(void);
void fs_transform_vertices(void);
void fs_draw_edges(void);
}

#ifndef K_FLATSHADE_ASM
// Rutinas calientes en ASM (support/flatshade_asm.s): port fiel del original con
// registros fijos, como fire_loop.s. POR DEFECTO 0 (version C++ canonica, la que
// renderiza bien): la ruta asm dibuja el contorno con un desfase ~2 px respecto a la
// C++ y, como el area fill es XOR (paridad por scanline), eso rompe el relleno (bandas
// y triangulos). Pasa verify-116 pero NO es visualmente correcta: pendiente de fix
// (ver docs/guides/optimization/OPTIMIZACION_GPP_68000.md). Activar con
// -DK_FLATSHADE_ASM=1 solo para reproducir/depurar.
#define K_FLATSHADE_ASM 0
#endif

namespace {
inline eng::u32 rcycles() {
	return *reinterpret_cast<volatile eng::u32*>(0xB7E928u);
}
}

// --- Datos del efecto (copiados TAL CUAL del demoscene) ----------------------
using eng::object3d::Mesh3D;
#define __data
using u_short = eng::u16;
#include "data/pilka.c"
#include "data/flatshade-pal.c"
#undef __data

namespace {

namespace obj = eng::object3d;
namespace copper = eng::copper;
namespace drivers = eng::graphics::drivers;

// Geometria del original (256x256, 4 planos).
constexpr eng::u16 kWidth = 256;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8; // 32
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u8 kBuffers = 3;               // triple buffer (pipeline clear/fill solapado)
constexpr eng::u32 kBitmapBytes = kPlaneBytes * kPlanes * kBuffers;
constexpr eng::u32 kCopperPerList = 512;

// Registros del display del original (MOD_LORES, X(32), Y(0), 256x256), identicos
// al port del `wireframe` (mismos `SetupPlayfield`/`SetupBitplaneFetch`).
constexpr eng::u16 kDiwstrt = 0x2ca1;
constexpr eng::u16 kDiwstop = 0x2ca1;
constexpr eng::u16 kDdfstrt = 0x0048;
constexpr eng::u16 kDdfstop = 0x00c0;
constexpr eng::u16 kBplcon0 = 0x4000; // 4 planos, color
constexpr eng::u16 kBplcon1 = 0x0000;

/// Maximo de vertices por cara del modelo `pilka` (poligonos de hasta 8 lados).
constexpr eng::u16 kMaxFaceVerts = 8;

/// Bounding-box de pantalla del objeto (actualizada en `transform_vertices`); acota
/// el `area fill` a la zona del objeto en vez de barrer las 1024 lineas del bitmap.
/// `g_bbox[4]` = {bx0, bx1, by0, by1}; lo escribe tambien `fs_transform_vertices`
/// (rutina asm, via `g_fs_args.bbox`).
eng::s16 g_bbox[4] = {32767, -32768, 32767, -32768};

/// Ruta n-gon (ruta B): vista `PolyMeshView` de la `pilka` (con la normal por cara del
/// `obj2c`) + offset de byte de cada cara. El orden usa `mesh_patches_order_lit` (cull por
/// normal, sin 64 bits) con la cámara en espacio objeto; el relleno, la coordenada de
/// pantalla ya proyectada.
eng::math3d::Vec3 g_poly_verts[64];
eng::s16 g_poly_offs[64];
eng::u16 g_poly_idx[256];
eng::math3d::FaceSpan g_poly_faces[64];
eng::math3d::Vec3 g_poly_norm[64];
eng::math3d::PolyMeshView g_poly {};
eng::s16 g_face_off[64];
eng::u32 g_face_off_n = 0;

// ============================================================================
//  Perfil de coste (demo 116, WinUAE-DBG, 68000, -O1; frame representativo)
// ============================================================================
//  Reparto del `update` (~238k ciclos, 2 vblanks) y a quien culpar de cada parte:
//
//   - transform (~127k, CPU): lib3d puro (visibilidad + transform+proyeccion).
//     ES el punto donde el codegen de g++ mas se nota frente al original: ~9
//     `mul_wide` + 2 `div_wide` por vertice, y los punteros a `objdat` no caben en
//     registros de direccion (GCC-15 ignora `register ... asm("aN")`). El detalle
//     de que emite g++ y cual seria el ideal en asm esta en `eng/platform/amiga/lib3d.hpp`.
//   - edges (~114k) + fill (~132k) + clear (~89k, Blitter): NO es CPU nuestro.
//     Cada blit tiene un coste fijo (~1.4k ciclos de arranque) y cada escritura a
//     registro custom ~57 ciclos; el area fill cuesta lo mismo que en el original
//     (131.8k vs 131k), asi que no es un artefacto del emulador.
//
//  Consecuencia practica: optimizar el transform mueve el fps poco a poco, pero el
//  frame NO cruza a 2 vblanks (25 fps) mientras edges+fill+clear no bajen, y esos
//  los paga el original igual. Por eso la via real es codegen (asm) del transform
//  y de la visibilidad, no micro-optimizar el dibujo.
//
// Las rutinas puras de lib3d (visibilidad de caras con luz, visibilidad de aristas
// de un solido convexo y transform+proyeccion de vertices) viven en
// eng/platform/amiga/lib3d.hpp (API reutilizable, con test host); aqui solo queda el dibujo
// Amiga (Blitter de lineas / area fill) y la orquestacion del efecto.
/// Ruta ALTERNATIVA (ruta B, `-DFLATSHADE_FAITHFUL=0`): rellena cada cara VISIBLE
/// (poligono proyectado) con su color de luz mediante `blitter_fill_polygon`
/// (mascara + contorno ONEDOT + area fill inclusivo + cookie-cut). Da caras solidas
/// sin depender de la paridad del area fill XOR, a cambio de mas blits por cara.
void draw_faces(obj::Object3D& object, eng::PlaneBytes planes, eng::amiga::MinimalBackend& backend,
		eng::MaskBuffer mask) {
	// Orden n-gon por math3d: culling por la normal almacenada (sin 64 bits) + pintor.
	eng::math3d::FaceOrder order[64];
	const eng::math3d::Vec3 cam {{object.camera.x, object.camera.y, object.camera.z}};
	const eng::u32 n = eng::math3d::mesh_patches_order_lit(
		g_poly, g_poly.vertices, cam, eng::Span<eng::math3d::FaceOrder>(order, 64));
	eng::s16 xs[kMaxFaceVerts];
	eng::s16 ys[kMaxFaceVerts];
	for (eng::u32 oi = 0; oi < n; ++oi) {
		obj::Face* face = object.face(g_face_off[order[oi].index]);
		if (face->count < 3 || face->count > kMaxFaceVerts) {
			continue;
		}
		const obj::FaceIndex* idx = obj::face_indices(face);
		for (eng::s16 k = 0; k < face->count; ++k) {
			const obj::Point3D* v = object.vertex(idx[k].vertex);
			xs[k] = v->x.v;
			ys[k] = v->y.v;
		}
		backend.blitter_fill_polygon(planes, kPlanes, kBytesPerRow, kPlaneBytes, xs, ys,
					     static_cast<eng::u8>(face->count),
					     static_cast<eng::u8>(face->flags), mask);
	}
}

// Seleccion de ruta y perfilado por secciones (todo por defecto a 0/1).
//   -DFLATSHADE_FAITHFUL=0  -> ruta alternativa por cara (blitter_fill_polygon).
//   -DFLATSHADE_LINE_OR=1   -> lineas OR en vez de EOR (solo ruta fiel).
//   -DFLATSHADE_SKIP_CLEAR/EDGES/FILL=1 -> omite esa seccion (perfilar por diferencia).
#ifndef FLATSHADE_FAITHFUL
#define FLATSHADE_FAITHFUL 1
#endif
#ifndef FLATSHADE_LINE_OR
#define FLATSHADE_LINE_OR 0
#endif
#ifndef FLATSHADE_SKIP_CLEAR
#define FLATSHADE_SKIP_CLEAR 0
#endif
#ifndef FLATSHADE_SKIP_EDGES
#define FLATSHADE_SKIP_EDGES 0
#endif
#ifndef FLATSHADE_SKIP_FILL
#define FLATSHADE_SKIP_FILL 0
#endif
#ifndef FLATSHADE_FILL_BBOX
#define FLATSHADE_FILL_BBOX 0
#endif
#ifndef FLATSHADE_FREEZE_ANGLE
// Angulo FIJO para capturas deterministas (0 = anima). Con esto la captura no depende
// de la fase de giro del balon y dos builds se pueden comparar PIXEL A PIXEL.
#define FLATSHADE_FREEZE_ANGLE 0
#endif
#ifndef FLATSHADE_PROFILE
// Metricas extra por arista (longitud total) para el perfilado; fuera de la build normal.
#define FLATSHADE_PROFILE 0
#endif
#ifdef FLATSHADE_SKIP_ALL
#undef FLATSHADE_SKIP_CLEAR
#undef FLATSHADE_SKIP_EDGES
#undef FLATSHADE_SKIP_FILL
#define FLATSHADE_SKIP_CLEAR 1
#define FLATSHADE_SKIP_EDGES 1
#define FLATSHADE_SKIP_FILL 1
#endif

/// Dibuja las aristas VISIBLES (`edgeColor > 0`) con `blitter_line_eor` (ONEDOT+EOR)
/// replicadas en cada plano seg??n el color de arista (camino FIEL del original
/// `DrawObject`). `edge->flags > 0` => arista visible; se limpia tras dibujarla para
/// que el XOR del siguiente frame parta de cero. Las aristas con `edgeColor == 0`
/// (canceladas por dos caras visibles de igual luz) se saltan. `bltdpt` apunta a la
/// BASE del bitmap (`planes.data()`, como el original), NO a la direccion calculada:
/// en modo linea el primer pixel va por D, y dejarlo siempre en la base mantiene la
/// paridad par/impar del contorno correcta en los v??rtices (sin ello el area fill
/// filtra una raya horizontal por v??rtice). NO rellena: el fill se lanza aparte
/// (`area_fill_planes`) para poder solaparlo con el transform del frame siguiente.
///
/// Coste medido: ~114k ciclos/frame con ~34 aristas y ~57 blits de linea; el gasto
/// es del **Blitter** (arranque ~1.4k por blit + ~57 ciclos por escritura a
/// registro custom), no de este bucle. Por eso aqui no hay nada que "optimizar en
/// C++": el margen esta en reducir el numero de blits, no en el codegen.
void draw_edges(obj::Object3D& object, eng::PlaneBytes planes,
		eng::amiga::MinimalBackend& backend) {
	const eng::u32 t0 = rcycles();
	eng::u32 n_edges = 0u;
	eng::u32 n_lines = 0u;
	eng::u32 px_total = 0u;
	eng::s16* group = object.edgeGroups;
	eng::s16 e;
#if !FLATSHADE_SKIP_EDGES
	// Setup com??n del modo l??nea EOR (ONEDOT) UNA vez por frame, como el preludio de
	// `DrawObject` del original (`bltafwm/alwm=-1, bltadat=0x8000, bltbdat=0xffff,
	// bltcmod/bltdmod=WIDTH/8`). `blitter_line_eor_continue` solo reprograma los 8
	// registros de cada arista/plano (macro `DRAWLINE`). No espera aqu??: el primer
	// `continue` sincroniza con el clear.
	backend.blitter_lines_eor_begin(kBytesPerRow);
	do {
		while ((e = *group++)) {
			obj::Edge* edge = object.edge(e);
			const eng::s8 edgeColor = edge->flags;
			if (edgeColor > 0) {
				++n_edges;
				edge->flags = 0;
				const obj::Point3D* a = object.vertex(edge->point[0]);
				const obj::Point3D* b = object.vertex(edge->point[1]);
				eng::s16 x0 = a->x.v;
				eng::s16 y0 = a->y.v;
				eng::s16 x1 = b->x.v;
				eng::s16 y1 = b->y.v;
				if (y0 == y1) {
					continue;
				}
				if (y0 > y1) {
					eng::s16 t = x0; x0 = x1; x1 = t;
					t = y0; y0 = y1; y1 = t;
				}
#if FLATSHADE_PROFILE
				// Solo diagnostico (perfilado): fuera del bucle caliente en la build normal.
				{
					const eng::s16 dx = static_cast<eng::s16>(x1 - x0);
					const eng::s16 dy = static_cast<eng::s16>(y1 - y0);
					px_total += static_cast<eng::u32>(dx > dy ? dx : dy);
				}
#endif
#if FLATSHADE_LINE_OR
				for (eng::u8 p = 0; p < kPlanes; ++p) {
					if ((edgeColor & (1 << p)) != 0) {
						++n_lines;
						backend.blitter_line(planes.subspan(
							static_cast<eng::u32>(p) * kPlaneBytes, kPlaneBytes),
							kBytesPerRow, x0, y0, x1, y1);
					}
				}
#else
				// Par??metros Bresenham calculados UNA vez por arista (independientes
				// del plano) y reutilizados en los N planos del color, como el original
				// (avanza `bltcpt += plane_bytes` sin recalcular el octante).
				eng::amiga::MinimalBackend::LineEorParams line;
				if (backend.blitter_line_eor_prepare(line, kBytesPerRow, x0, y0, x1, y1)) {
					for (eng::u8 p = 0; p < kPlanes; ++p) {
						if ((edgeColor & (1 << p)) != 0) {
							++n_lines;
							backend.blitter_line_eor_draw(line, planes.data() +
										       static_cast<eng::u32>(p) * kPlaneBytes,
										       planes.data());
						}
					}
				}
#endif
			}
		}
	} while (*group);
#else
	(void)group; (void)e;
#endif
	const eng::u32 t1 = rcycles();
	g_eng_prof.v[10] = n_edges;
	g_eng_prof.v[11] = n_lines;
	g_eng_prof.v[12] = px_total;
	g_eng_prof.v[2] = t1 - t0;
}

/// Lanza (o espera) el area fill `XOR` sobre `planes`. `wait=false` lo lanza sin
/// esperarlo: el buffer no se muestra hasta el swap del siguiente update, as?? que el
/// wait final se absorbe en la espera de VBlank del engine (o en el primer wait del
/// update siguiente), igual que el `WaitBlitter` pegado a `TaskWaitVBlank` del
/// original. Con `FLATSHADE_FILL_BBOX` se acota a la bounding-box del objeto.
///
/// Coste medido: ~132k ciclos/frame. Es **Blitter puro** (barrido XOR descendente de
/// los 4 planos contiguos) y coincide con el del original (131.8k vs 131k), asi que
/// no hay margen en C++ aqui: cualquier recorte vendria de tocar la tecnica (acotar
/// el barrido), no el codegen.
void area_fill_planes(eng::PlaneBytes planes, eng::amiga::MinimalBackend& backend, bool wait) {
	const eng::u32 t0 = rcycles();
#if !FLATSHADE_SKIP_FILL
#if FLATSHADE_FILL_BBOX
	if (g_bbox[1] >= g_bbox[0] && g_bbox[3] >= g_bbox[2]) {
		eng::s16 x0 = static_cast<eng::s16>(g_bbox[0] - 2); if (x0 < 0) x0 = 0;
		eng::s16 y0 = static_cast<eng::s16>(g_bbox[2] - 2); if (y0 < 0) y0 = 0;
		eng::s16 x1 = static_cast<eng::s16>(g_bbox[1] + 2); if (x1 > 255) x1 = 255;
		eng::s16 y1 = static_cast<eng::s16>(g_bbox[3] + 2); if (y1 > 255) y1 = 255;
		const eng::u16 wx0 = static_cast<eng::u16>(x0) & 0xfff0u;
		const eng::u16 wx1 = static_cast<eng::u16>(x1) | 0x000fu;
		const eng::u16 words = static_cast<eng::u16>((static_cast<eng::u16>(wx1 - wx0) + 16u) >> 4);
		const eng::u16 rows = static_cast<eng::u16>(y1 - y0 + 1);
		for (eng::u8 p = 0; p < kPlanes; ++p) {
			backend.blitter_area_fill_rect(
				planes.subspan(static_cast<eng::u32>(p) * kPlaneBytes, kPlaneBytes),
				kBytesPerRow, wx0, y0, words, rows, wait || static_cast<eng::u8>(p + 1u) == kPlanes);
		}
	}
#else
	// El area fill XOR se lanza sobre el contorno (`draw_edges`). El buffer se muestra
	// en el pr??ximo swap; `wait=false` deja que el wait final lo absorba la espera de
	// VBlank o el primer wait del update siguiente (el buffer nunca se ve a medias).
	backend.blitter_area_fill(planes, kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight, wait);
#endif
#endif
	const eng::u32 t1 = rcycles();
	g_eng_prof.v[3] = t1 - t0;
	g_eng_prof.v[4] = t1 - t0;
}

#if K_FLATSHADE_ASM
/// Rellena `g_fs_args` para las rutinas asm. `obj`/`invsqrt`/`bbox` son constantes;
/// `planes` cambia por frame (buffer activo de los edges).
inline void prepare_fs_args(eng::PlaneBytes planes, obj::Object3D& object) {
	g_fs_args.obj = &object;
	g_fs_args.planes = planes.data();
	g_fs_args.invsqrt = eng::lib3d::kInvSqrt;
	g_fs_args.bbox = g_bbox;
}
#endif

struct FlatShadeDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({128u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011601u);
			return;
		}

		m_mask_block = backend.memory().chip.allocate_block<eng::MaskTag>(kPlaneBytes, 16);
		if (!m_mask_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011602u);
			return;
		}

		// Triple buffer generico: `MultiBuffered<HamScene, 3>` reserva los 3 bitmaps
		// (4 planos contiguos cada uno) y sus 3 copperlists. La semantica coincide con la
		// pipeline del original: se dibuja en `back()`, se lanza el fill y `commit()`
		// publica el recien dibujado (se vera en el swap del frame siguiente) y rota.
		drivers::HamSceneConfig scene_cfg {};
		scene_cfg.bytes_per_row = kBytesPerRow;
		scene_cfg.rows = kHeight;
		scene_cfg.planes = kPlanes;
		scene_cfg.bplcon0 = kBplcon0;
		scene_cfg.diwstrt = kDiwstrt;
		scene_cfg.diwstop = kDiwstop;
		scene_cfg.ddfstrt = kDdfstrt;
		scene_cfg.ddfstop = kDdfstop;
		scene_cfg.row_repeat = 1u;
		scene_cfg.bplcon1_shift = kBplcon1;
		// `HamScene` emite la lista POR LINEA (WAIT + BPLMOD + BPLCON1 por cada una de las
		// 256 lineas) aunque `row_repeat=1`; son ~2 KB por bloque (el original usaba una
		// lista plana de 512 B). Se reserva de sobra.
		scene_cfg.copper_bytes = 6144u;
		scene_cfg.palette = eng::PaletteWords {flatshade_colors, 16u};
		scene_cfg.palette_first = 0;
		scene_cfg.palette_count = 16;
		if (!m_scenes.init(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011603u);
			return;
		}
		m_scenes.takeover(backend);

		// El original activa `DMAF_BLITHOG` (BLTPRI): el Blitter no cede slots de bus a
		// la CPU durante el fill/lines. Con la pipeline, el transform corre durante el
		// fill; si el emulador congela la CPU con BLITHOG, este override se desactiva
		// en la build (ver bitacora). Se deja activo para medir ambos efectos.
		backend.set_blitter_priority(true);

		obj::new_object3d(m_object, pilka);
		m_object.translate.z = eng::retro::q0 {-4000}; // fx4i(-250)

		// Ruta B: vista n-gon de la pilka (con normales) + offset de byte de cada cara.
		obj::build_poly_mesh(m_object, eng::Span<eng::math3d::Vec3>(g_poly_verts, 64),
				     eng::Span<eng::s16>(g_poly_offs, 64),
				     eng::Span<eng::u16>(g_poly_idx, 256),
				     eng::Span<eng::math3d::FaceSpan>(g_poly_faces, 64),
				     eng::Span<eng::math3d::Vec3>(g_poly_norm, 64), g_poly);
		g_face_off_n = 0;
		{
			const eng::s16* fg = m_object.faceGroups;
			if (fg != nullptr) {
				do {
					eng::s16 off;
					while ((off = *fg++) != 0) {
						g_face_off[g_face_off_n++] = off;
					}
				} while (*fg != 0);
			}
		}

		// Pipeline de doble/triple buffer: el estado del objeto para el primer dibujo se
		// precalcula aqu?? (lo que en `update` ocurre durante el fill del frame previo).
		m_angle = 0;
		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			eng::retro::turns(static_cast<eng::u16>(m_angle));
		obj::update_object_transformation(m_object);
#if K_FLATSHADE_ASM
		prepare_fs_args(m_scenes.slot(0).bitplanes(), m_object);
		fs_update_face_visibility();
		fs_update_edge_visibility_convex();
		fs_transform_vertices();
#else
		eng::lib3d::update_face_visibility(m_object);
		eng::lib3d::update_edge_visibility_convex(m_object);
		eng::lib3d::transform_vertices(m_object, static_cast<eng::s16>(kWidth / 2), static_cast<eng::s16>(kHeight / 2), g_bbox);
#endif

		// Pre-clear todos los buffers: el primer `update` dibuja sobre `back()` sin esperar.
		for (eng::u8 b = 0; b < kBuffers; ++b) {
			backend.blitter_clear(m_scenes.slot(b).bitplanes(), kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight, true);
		}

		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(pilka.faces));
	}

	/// Pipeline de 3 buffers: el `update` dibuja sobre `back()` (ya pre-clearado),
	/// lanza su fill sin esperarlo y, MIENTRAS el Blitter lo hace, precalcula el estado
	/// (transform/culling/luz) del frame siguiente y pre-cleara el buffer que ese frame
	/// usar??. Con 3 buffers el buffer a limpiar ni se muestra ni se dibuja, as?? que el
	/// clear queda escondido bajo el fill. El wait del fill se absorbe en la espera de
	/// VBlank del engine o en el primer wait del siguiente update: el buffer nunca se ve
	/// a medias (se muestra en el swap del update siguiente).
	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_init_ok) {
			return;
		}

		// El buffer en el que se dibuja este frame (el trasero del triple buffer).
		eng::PlaneBytes planes = m_scenes.back().bitplanes();

		const eng::u32 t0 = rcycles();

		// 1) Esperar el fill del buffer dibujado el frame pasado (se muestra en el swap
		//    de este frame); la espera de VBlank ya lo ha absorbido casi siempre.
		backend.wait_blitter();

		// 2) El swap de display lo hace `commit()` al final del update: publica el buffer
		//    recien dibujado (se vera en el VBlank siguiente) y rota el trasero.

		// 3) Contorno sobre el buffer pre-clearado (estado del objeto ya precalculado).
#if FLATSHADE_FAITHFUL
#if K_FLATSHADE_ASM
		prepare_fs_args(planes, m_object);
		const eng::u32 td0 = rcycles();
		fs_draw_edges();
		g_eng_prof.v[2] = rcycles() - td0;
#else
		draw_edges(m_object, planes, backend);
#endif
#else
		draw_faces(m_object, planes, backend, m_mask_block.view);
#endif
		const eng::u32 t1 = rcycles();

		// 4) Lanzar el fill SIN esperarlo: se mostrar?? en el swap del pr??ximo update.
		area_fill_planes(planes, backend, false);

		// 5) Durante el fill (Blitter ocupado), precalcular el estado del frame siguiente
		//    (transform + luz + visibilidad de aristas) sobre el object model.
#if FLATSHADE_FREEZE_ANGLE
		m_angle = static_cast<eng::s16>(FLATSHADE_FREEZE_ANGLE);
#else
		m_angle = static_cast<eng::s16>(m_angle + 8);
#endif
		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			eng::retro::turns(static_cast<eng::u16>(m_angle));
		obj::update_object_transformation(m_object);
		const eng::u32 ta = rcycles();
		#if K_FLATSHADE_ASM
		prepare_fs_args(planes, m_object);
		fs_update_face_visibility();
		const eng::u32 tb = rcycles();
		fs_update_edge_visibility_convex();
		const eng::u32 tc = rcycles();
		fs_transform_vertices();
#else
		eng::lib3d::update_face_visibility(m_object);
		const eng::u32 tb = rcycles();
		eng::lib3d::update_edge_visibility_convex(m_object);
		const eng::u32 tc = rcycles();
		eng::lib3d::transform_vertices(m_object, static_cast<eng::s16>(kWidth / 2), static_cast<eng::s16>(kHeight / 2), g_bbox);
#endif
		const eng::u32 t2 = rcycles();
		g_eng_prof.v[6] = ta - t1; // update_object_transformation
		g_eng_prof.v[7] = tb - ta; // update_face_visibility
		g_eng_prof.v[8] = tc - tb; // update_edge_visibility_convex
		g_eng_prof.v[9] = t2 - tc; // transform_vertices

		// 6) Publicar y rotar: `commit` instala la copperlist del buffer recien dibujado y
		//    pasa al siguiente; despues se pre-cleara ESE (ni en pantalla ni en dibujo
		//    ahora), sin esperar, para que quede colgado tras el fill.
		m_scenes.commit(backend);
#if !FLATSHADE_SKIP_CLEAR
		backend.blitter_clear(m_scenes.back().bitplanes(), kPlanes, kBytesPerRow, kPlaneBytes,
				      kWidth, kHeight, false);
#endif

		const eng::u32 t3 = rcycles();

		g_eng_prof.v[0] = t1 - t0;
		g_eng_prof.v[1] = t2 - t1;
		g_eng_prof.v[5] = t3 - t0;
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool m_init_ok = false;
	bool m_memory_ok = false;
	eng::s16 m_angle = 0;
	drivers::MultiBuffered<drivers::HamScene, kBuffers> m_scenes {};
	eng::Block<eng::MaskTag> m_mask_block {};
	eng::object3d::Object3D m_object {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	FlatShadeDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}

