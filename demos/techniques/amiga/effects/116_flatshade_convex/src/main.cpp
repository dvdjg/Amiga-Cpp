// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/116_flatshade_convex --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/116_flatshade_convex --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/116_flatshade_convex --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/116_flatshade_convex --keep-running

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
#include <eng/retro/flat_shade_xor.hpp>
#include <eng/api/api.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/composition/limits.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/graphics/frame_plan.hpp> // plan async (FLATSHADE_ASYNC, DT-026)
#include <eng/graphics/blit_job.hpp>   // make_area_fill_rect / BlitJob del plan async

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

// Perfilado por secciones: se usa el bloque del ENGINE (`eng::debug::g_eng_prof`,
// `ProfBlock`: magic + cycles[16]⬦), publicado en el `.map` y leído por
// `tools/debug/profile.mjs` (la demo escribe `cycles[N]` a mano en los tramos que mide).
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
namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;

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

// Franja de depuracion (norma del repo): nombre + version + contador de frames en una ZONA
// APARTE con un 5º plano de 1 bpp de tamano COMPLETO (8 KB) activo TODO el frame (BPLCON0
// de 5 planos + puntero en la cabecera de la lista, sin WAITs: una zona `>255` no se puede
// cerrar — el comparador usa semantica `>=` sobre V[7:0], `WinUAE-DBG/custom.cpp:4416`
// `coppercomp` — y el plano quedaba encendido mostrando basura; fallo real 2026-10-17). El
// texto se pinta por palabras (`Surface::draw_text_words`) y se ACTUALIZA SOLO AL CAMBIO:
// coste por frame ~0 (fetch fijo del plano aparte). La franja va en la banda superior
// (filas de plano 6..13 -> filas ~16..23 de la ventana; el balon empieza en la ~27) y la
// paleta 16..31 es un ESPEJO de la del sombreado (16 = tinta) para que el +16 que el 5º
// plano suma a toda la pantalla sea transparente fuera del texto.
constexpr eng::u16 kHudRows = 16;
constexpr eng::u32 kHudBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight; // plano completo
constexpr eng::s32 kHudY = 6; // fila de PLANO del texto (-> fila 16 de la ventana)
constexpr eng::u8 kHudColor = 1u;
constexpr eng::s32 kHudCounterX = 176; // multiplo de 16, tras "FLATSHADE vX.Y.BBBBB"
/// X del campo de fps del ultimo segundo (multiplo de 16; tras el contador de 6
/// caracteres). Valor con UN decimal, p. ej. "18.7" (ver `eng::debug::HudFpsWindow`).
constexpr eng::s32 kHudFpsX = 224;
constexpr eng::debug::Version kVersion {2u, 0u};
eng::u16 g_hud_band_colors[32] {};

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
// Tabla de offsets de VERTICE (recorrido plano de `points()`), construida una vez en
// init: la usan las variantes por rango de `lib3d` para el interleaving cooperativo
// (el transform del frame siguiente se reparte en los huecos de espera del Blitter).
eng::s16 g_vert_off[64];
eng::u32 g_vert_off_n = 0;

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
void draw_faces(obj::Object3D& object, eng::PlaneBytes planes, eng::amiga::AmigaBackend& backend,
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
		const eng::Span<obj::FaceIndex> idx = obj::face_indices(face);
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
//   -DFLATSHADE_ASYNC=1     -> referee async del `FramePlan` (DT-026): contorno EOR +
//                              area fill ACOTADO a la bbox + clear del buffer siguiente
//                              encolados en UN plan por frame; los encadena la IRQ de
//                              fin de blit y la CPU queda libre (el `commit` y el
//                              reciclado del plan van al arranque del update siguiente).
//                              Ignora los SKIP_* (perfilado por secciones de la ruta sync).
//   -DFLATSHADE_SKIP_CLEAR/EDGES/FILL=1 -> omite esa seccion (perfilar por diferencia).
#ifndef FLATSHADE_FAITHFUL
#define FLATSHADE_FAITHFUL 1
#endif
#ifndef FLATSHADE_LINE_OR
#define FLATSHADE_LINE_OR 0
#endif
#ifndef FLATSHADE_ASYNC
#define FLATSHADE_ASYNC 0
#endif
// Diagnostico de ablacion del plan async (nombres CORTOS: el CONFIG_ID del build
// concatena los defines y una ruta larga revienta MAX_PATH en Windows).
#ifndef FLATSHADE_NOFILL
#define FLATSHADE_NOFILL 0 ///< plan async sin jobs de fill
#endif
#ifndef FLATSHADE_NOCLEAR
#define FLATSHADE_NOCLEAR 0 ///< plan async sin jobs de clear
#endif
#if FLATSHADE_ASYNC && (FLATSHADE_SKIP_CLEAR || FLATSHADE_SKIP_EDGES || FLATSHADE_SKIP_FILL)
#error "FLATSHADE_ASYNC no combina con los FLATSHADE_SKIP_* (usa la ruta sincrona para perfilar secciones)"
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
// Barrido del area fill ACORTADO por la bbox del objeto (`blitter_area_fill(..., bbox)`,
// un solo blit contiguo: semilla bajo la caja del ultimo plano, fin tras la caja del
// plano 0; las filas externas son limpias y omitirlas es NO-OP de pixel). Medido
// 2026-10: el intento antiguo de 4 jobs por plano empeoraba (se serializaban); este
// ahorra el barrido de las filas externas (~6k) con un solo blit. `=0` vuelve al
// barrido completo para A/B.
#define FLATSHADE_FILL_BBOX 1
#endif
#ifndef FLATSHADE_PUMP
// Interleaving cooperativo (DT-026, propuesta final de Grok): el transform del frame
// siguiente repartido en las esperas del Blitter via `set_blit_service`. **MEDIDO Y
// REFUTADO** (2026-10-17): bomba global (todas las esperas) = 14,7 fps (3,40 campos);
// bomba con ventana (solo la espera grande del inicio) = 14,8 fps — ambos muy peores
// que 21,62. Causa: el bloque antiguo del transform YA corria SOLAPADO con el fill
// (131k de ventana con el Blitter ocupado); un elemento por vuelta de espera se
// ejecuta bajo nasty (~1,5-2x), desborda las esperas cortas de linea, y la parte que
// no absorbe la espera grande se DRENA en bloque con el Blitter ya idle (serial puro).
// Se conserva como experimento documentado detras del flag; el default es 0.
#define FLATSHADE_PUMP 0
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

// Ruta de dibujo de la via fiel:
//   0 (defecto) = `eng::retro::flat_shade_xor` (contorno EOR + area fill XOR viven en el
//                 engine; `main` solo reune las aristas visibles).
//   1           = ruta legacy `draw_edges` + `area_fill_planes` (para perfilar por
//                 secciones con los flags FLATSHADE_SKIP_*).
// Los builds de perfilado por secciones fuerzan la legacy (los SKIP_* no aplican al helper).
#if FLATSHADE_SKIP_EDGES || FLATSHADE_SKIP_FILL || FLATSHADE_SKIP_CLEAR
#undef FLATSHADE_LEGACY_DRAW
#define FLATSHADE_LEGACY_DRAW 1
#endif
#ifndef FLATSHADE_LEGACY_DRAW
#define FLATSHADE_LEGACY_DRAW 0
#endif

/// Reune las aristas VISIBLES (`edge->flags > 0`) del objeto en `out` (coordenadas ya
/// proyectadas + mascara de color) y limpia el flag para que el XOR del frame siguiente
/// parta de cero. Devuelve cuantas. Es el unico punto que toca el `Object3D` para dibujar:
/// la secuencia de Blitter la pone `eng::retro::flat_shade_xor`.
inline eng::u32 gather_visible_edges(obj::Object3D& object, eng::retro::OutlineEdge* out,
				     eng::u32 cap) {
	eng::u32 n = 0u;
	for (const eng::Ref<obj::Edge>& er : object.edges()) {
		obj::Edge* edge = er.get();
		const eng::s8 edgeColor = edge->flags;
		if (edgeColor > 0 && n < cap) {
			edge->flags = 0;
			const obj::Point3D* a = object.vertex(edge->point[0]);
			const obj::Point3D* b = object.vertex(edge->point[1]);
			out[n++] = eng::retro::OutlineEdge {
				a->x.v, a->y.v, b->x.v, b->y.v, static_cast<eng::u8>(edgeColor)};
		}
	}
	return n;
}


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
		eng::amiga::AmigaBackend& backend) {
	const eng::u32 t0 = rcycles();
	eng::u32 n_edges = 0u;
	eng::u32 n_lines = 0u;
	eng::u32 px_total = 0u;
#if !FLATSHADE_SKIP_EDGES
	// Setup com??n del modo l??nea EOR (ONEDOT) UNA vez por frame, como el preludio de
	// `DrawObject` del original (`bltafwm/alwm=-1, bltadat=0x8000, bltbdat=0xffff,
	// bltcmod/bltdmod=WIDTH/8`). `blitter_line_eor_continue` solo reprograma los 8
	// registros de cada arista/plano (macro `DRAWLINE`). No espera aqu??: el primer
	// `continue` sincroniza con el clear.
	backend.blitter_lines_begin(kBytesPerRow);
	for (const eng::Ref<obj::Edge>& er : object.edges()) {
		obj::Edge* edge = er.get();
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
			eng::graphics::LineEor line;
			if (backend.blitter_line_eor_prepare(line, kBytesPerRow, x0, y0, x1, y1)) {
				for (eng::u8 p = 0; p < kPlanes; ++p) {
					if ((edgeColor & (1 << p)) != 0) {
						++n_lines;
						backend.blitter_line_draw(line, planes.data() +
									       static_cast<eng::u32>(p) * kPlaneBytes,
									       planes.data());
					}
				}
			}
#endif
		}
	}
#endif
	const eng::u32 t1 = rcycles();
	eng::debug::g_eng_prof.cycles[10] = n_edges;
	eng::debug::g_eng_prof.cycles[11] = n_lines;
	eng::debug::g_eng_prof.cycles[12] = px_total;
	eng::debug::g_eng_prof.cycles[2] = t1 - t0;
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
void area_fill_planes(eng::PlaneBytes planes, eng::amiga::AmigaBackend& backend, bool wait) {
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
	eng::debug::g_eng_prof.cycles[3] = t1 - t0;
	eng::debug::g_eng_prof.cycles[4] = t1 - t0;
}

#if K_FLATSHADE_ASM
/// Rellena `g_fs_args` para las rutinas asm. `obj`/`invsqrt`/`bbox` son constantes;
/// `planes` cambia por frame (buffer activo de los edges).
inline void prepare_fs_args(eng::PlaneBytes planes, obj::Object3D& object) {
	g_fs_args.obj = &object;
	g_fs_args.planes = planes.data();
	g_fs_args.invsqrt = eng::lib3d::kInvSqrt.data();
	g_fs_args.bbox = g_bbox;
}
#endif

struct FlatShadeDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({128u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011601u);
			return;
		}

		m_mask_block = backend.memory_manager().chip().reserve<eng::MaskTag>(kPlaneBytes, 16);
		if (!m_mask_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011602u);
			return;
		}
		m_hud_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kHudBytes, 16);
		if (!m_hud_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011604u);
			return;
		}

		// Paleta de la banda del HUD (16..31): 16 = tinta de la franja (blanco); 17..31
		// replican 1..15 para que los pixeles del balon dentro de la banda no cambien.
		g_hud_band_colors[16] = 0xFFFu;
		for (eng::u8 k = 1u; k < 16u; ++k) {
			g_hud_band_colors[16u + k] = flatshade_colors[k];
		}
		// Lienzo de la franja: su propio bloque (1 bpp, 256x16). El `plane_stride` de
		// `bind_raw` es el tamano de PLANO (kHudBytes), no el de fila.
		(void)m_hud_pf.bind_raw(m_hud_block.view.data(),
					static_cast<eng::u32>(m_hud_block.view.size()),
					kWidth, kHeight, 1, kHudBytes);
		(void)backend.blitter_clear(m_hud_block.view.subspan(0u, kHudBytes), 1u, kBytesPerRow,
					    kHudBytes, kWidth, kHeight);
		draw_hud_static();
		// Arma la ventana de fps y pinta los campos dinamicos en su estado inicial.
		m_hud_fps.reset(0u, eng::debug::prof_clock());
		draw_hud_fields(0u, 0u);

		// Triple buffer sobre `scene::compose`: 3 buffers (4 planos contiguos cada uno) con
		// triple buffer por parcheo de `BPLxPT` en `commit()`. La semantica coincide con la
		// pipeline del original: se dibuja en `back()`, se lanza el fill y `commit()`
		// publica el recien dibujado (se vera en el swap del frame siguiente) y rota.
		// Geometria propia del original (256x256): DIW/DDF y BPLCON0 crudos. Las dos ultimas
		// etapas añaden la zona del HUD (5º plano 1 bpp) y su paleta de banda: la lista sale
		// con geometria 4 planos + tramo final de 5 (estatico, sin coste por frame).
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = static_cast<eng::u8>(kBuffers);
		if (!scene::compose(m_scene, backend.memory_manager(), res,
				    scene::ocs_a500,
				    scene::display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBplcon0),
				    scene::palette(eng::PaletteWords {flatshade_colors, 16u}, 0u, 16u),
				    [](scene::Scene& sc) {
					    sc.scheduler().emit_palette(
						    eng::PaletteWords {g_hud_band_colors, 32u}, 16u, 16u);
				    },
				    [this](scene::Scene& sc) {
					    // 5º plano (franja) como BANDA superior LEGAL: se enciende
					    // en el WAIT de la raster 44 (inicio de la ventana) y se
					    // apaga en la 58 — ambas < 255, asi que el comparador `>=`
					    // de WinUAE (`custom.cpp:4416` `coppercomp`) las cumple en
					    // su linea (la zona `>255` de la primera pasada no era
					    // cerrable y dejaba el plano encendido; ver DT-027). El
					    // fetch del 5º plano cubre solo ~14 filas en vez de ~290:
					    // con puntero fijado en raster 44 la fila de plano P se ve
					    // en la P+10 de la ventana, igual que antes -> el texto
					    // (filas de plano 6..13) sigue saliendo en las filas
					    // ~16..23 de la ventana.
					    auto& sched = sc.scheduler();
					    sched.wait_line(44u);
					    sched.move(copper::Register::BPLCON0,
						       eng::graphics::composition::bplcon0_for(
							       eng::graphics::composition::SceneMode::Standard,
							       5u));
					    sched.move_bitplane_pointer(
						    4, m_hud_block.mem_view_chip().address(0));
					    sched.wait_line(58u);
					    sched.move(copper::Register::BPLCON0, kBplcon0);
				    })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011603u);
			return;
		}
		m_scene.takeover(backend);

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
			const auto fgr = m_object.faces();
			for (auto it = fgr.begin(); it != fgr.end(); ++it) {
				g_face_off[g_face_off_n++] = it.offset();
			}
		}
		g_vert_off_n = 0;
		{
			const auto pgr = m_object.points();
			for (auto it = pgr.begin(); it != pgr.end(); ++it) {
				g_vert_off[g_vert_off_n++] = it.offset();
			}
		}
		// Servicio de espera del Blitter = bomba del transform cooperativo: cada
		// vuelta del sondeo BBUSY procesa UN elemento (cara/vertice) del armado
		// (ver `blit_pump`). El engine no instala el suyo (`set_blit_service_enabled(false)`
		// en `main`): este slot es nuestro.
#if FLATSHADE_PUMP
		(void)backend.set_blit_service(&FlatShadeDemo::blit_pump, *this);
#endif

		// Pipeline de doble/triple buffer: el estado del objeto para el primer dibujo se
		// precalcula aqu?? (lo que en `update` ocurre durante el fill del frame previo).
		m_angle = 0;
		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			eng::retro::turns(static_cast<eng::u16>(m_angle));
		obj::update_object_transformation(m_object);
#if K_FLATSHADE_ASM
		prepare_fs_args(m_scene.buffer(0), m_object);
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
			backend.blitter_clear(m_scene.buffer(b), kPlanes, kBytesPerRow, kPlaneBytes, kWidth, kHeight, true);
		}

		ENG_PROF_INIT(13); // bloque del engine (profile.mjs); la demo escribe cycles[] a mano
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
#if FLATSHADE_ASYNC
	/// Construye el plan del frame para el **referee async** (DT-026). El ORDEN importa:
	///  1. contorno EOR (un job de linea por arista x plano de su mascara): fija la
	///     paridad que el area fill convierte en interior;
	///  2. area fill XOR **acotado a la bbox del objeto** (4 jobs, uno por plano): en la
	///     cadena async el recorte no serializa la CPU (la leccion del intento sincrono),
	///     asi que se cobra el recorte de DMA (~56k);
	///  3. clear del buffer **siguiente** (1 job multi-plano): tras el `commit` del update
	///     proximo sera el trasero a dibujar; ahora ni se muestra ni se dibuja, asi que el
	///     clear puede ir al final de la cadena (recortarlo por filas es la deuda viva).
	/// Las direcciones via `BlitPtr::from_storage`: los buffers de la escena son `Block`
	/// Chip por construccion (`scene::compose`), y la via tipada `chip_planes()` solo
	/// cubre el buffer `back()` (aqui hacen falta tambien planos y el buffer siguiente).
	void build_chain_plan(eng::PlaneBytes planes, const eng::retro::OutlineEdge* edges,
			      eng::u32 ne) {
		const auto ptr = [](eng::u8* p) {
			return eng::graphics::BlitPtr::from_storage(
				reinterpret_cast<eng::u16*>(p));
		};
		for (eng::u32 i = 0u; i < ne; ++i) {
			const eng::retro::OutlineEdge& e = edges[i];
			// Las aristas HORIZONTALES se descartan, como en la ruta sincrona
			// (`draw_edges`/`flat_shade_xor`): no aportan contorno y, dibujadas, meten
			// pixeles extra en los vertices que descuadran el area fill. En el plan es
			// ADEMAS obligatorio: `blitter_line_eor_prepare` devuelve false para ellas,
			// el job no arranca ningun blit y la cadena esperaria una IRQ que no llega
			// (DT-026: cuelgue reproducible de la cadena async).
			if (e.y0 == e.y1) {
				continue;
			}
			for (eng::u8 p = 0u; p < kPlanes; ++p) {
				if ((e.color & (1u << p)) == 0u) {
					continue;
				}
				eng::graphics::BlitJob& j =
					m_plan.begin_line_job(eng::graphics::BlitJobKind::LineEor);
				j.destination = ptr(planes.data() +
						    static_cast<eng::u32>(p) * kPlaneBytes);
				j.line.base = ptr(planes.data());
				j.line.row_bytes = kBytesPerRow;
				j.line.x0 = e.x0;
				j.line.y0 = e.y0;
				j.line.x1 = e.x1;
				j.line.y1 = e.y1;
				j.bitplane_count = 1u;
				(void)m_plan.commit_line_job();
			}
		}
		// Rango del barrido del fill: bbox proyectada + 2 px de margen (como la ruta
		// sincrona con FLATSHADE_FILL_BBOX); sin bbox valida, el ancho completo.
		eng::s16 x0 = 0;
		eng::s16 y0 = 0;
		eng::s16 x1 = static_cast<eng::s16>(kWidth - 1u);
		eng::s16 y1 = static_cast<eng::s16>(kHeight - 1u);
		if (g_bbox[1] >= g_bbox[0] && g_bbox[3] >= g_bbox[2]) {
			x0 = static_cast<eng::s16>(g_bbox[0] - 2); if (x0 < 0) x0 = 0;
			y0 = static_cast<eng::s16>(g_bbox[2] - 2); if (y0 < 0) y0 = 0;
			x1 = static_cast<eng::s16>(g_bbox[1] + 2); if (x1 > 255) x1 = 255;
			y1 = static_cast<eng::s16>(g_bbox[3] + 2); if (y1 > 255) y1 = 255;
		}
		const eng::u16 wx0 = static_cast<eng::u16>(x0) & 0xfff0u;
		const eng::u16 wx1 = static_cast<eng::u16>(x1) | 0x000fu;
		const eng::u16 words = static_cast<eng::u16>((static_cast<eng::u16>(wx1 - wx0) + 16u) >> 4);
		const eng::u16 rows = static_cast<eng::u16>(y1 - y0 + 1);
#if !FLATSHADE_NOFILL
		for (eng::u8 p = 0u; p < kPlanes; ++p) {
			eng::graphics::BlitJob j {};
			eng::graphics::make_area_fill_rect(
				j, ptr(planes.data() + static_cast<eng::u32>(p) * kPlaneBytes),
				kBytesPerRow, wx0, y0, words, rows);
			(void)m_plan.add_area_fill(j);
		}
#else
		(void)words;
		(void)rows;
#endif
		// Clear del buffer siguiente (kBuffers = 3: uno mostrado, uno en dibujo, el
		// tercero es el que este clear deja listo para dentro de dos updates).
		//
		// UN job por plano (cada uno = 1 blit): las vias del feeder estan probadas con
		// jobs de un solo blit (lineas y fills); los multi-blit pagan esperas internas
		// dentro de la ISR con el gate INTENA cerrado. El recorte por filas del clear
		// (no barrer 1024 filas) es deuda viva (misma leccion que el fill).
		const eng::u8 next =
			static_cast<eng::u8>((m_scene.back_index() + 1u) % m_scene.buffer_count());
		eng::PlaneBytes next_planes = m_scene.buffer(next);
#if !FLATSHADE_NOCLEAR
		for (eng::u8 p = 0u; p < kPlanes; ++p) {
			eng::graphics::BlitJob c {};
			c.kind = eng::graphics::BlitJobKind::ClearRect;
			c.destination = ptr(next_planes.data() +
					    static_cast<eng::u32>(p) * kPlaneBytes);
			c.words_per_row = static_cast<eng::u16>(kBytesPerRow / 2u);
			c.height = kHeight;
			c.bitplane_count = 1u;
			c.destination_plane_stride_bytes = kPlaneBytes;
			(void)m_plan.add_clear_rect(c);
		}
#else
		(void)next_planes;
#endif
	}
#endif

#if FLATSHADE_PUMP
	/// Fases del transform cooperativo (DT-026): el estado del frame SIGUIENTE se
	/// procesa por elementos en los huecos de espera del Blitter.
	enum class PumpPhase : eng::u8 { Idle, Faces, Edges, Verts, Done };

	/// Servicio de espera del Blitter (instalado en `init`): avanza UN elemento del
	/// armado por cada vuelta del sondeo BBUSY, SOLO mientras `m_pump_active` — la
	/// ventana es la espera GRANDE del inicio del update (la cola del fill/clear,
	/// ~77k ciclos medidos). En las esperas cortas de linea NO se bombea: un elemento
	/// (~800 cc) desborda el hueco y estiraria la fase de lineas (medido: 14,7 fps
	/// con bomba global vs 21,6 sin ella). Fuera de ventana es un no-op de ~4 ciclos.
	static void blit_pump(FlatShadeDemo& self, eng::u16) noexcept {
		if (self.m_pump_active && self.m_phase != PumpPhase::Done &&
		    self.m_phase != PumpPhase::Idle) {
			(void)self.pump_step(1u);
		}
	}

	/// ARMA el transform cooperativo del frame siguiente: matriz (sincrona, ~9k: es
	/// dependencia inmediata del cache del proyector) + estado de fases a cero.
	/// El trabajo pesado (caras ~24k, aristas ~13k, vertices ~44k) lo reparte la bomba.
	void pump_arm() {
		const eng::u32 t0 = rcycles();
#if FLATSHADE_FREEZE_ANGLE
		m_angle = static_cast<eng::s16>(FLATSHADE_FREEZE_ANGLE);
#else
		m_angle = static_cast<eng::s16>(m_angle + 8);
#endif
		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z =
			eng::retro::turns(static_cast<eng::u16>(m_angle));
		obj::update_object_transformation(m_object);
		m_pc = Proj::make(m_object.objectToWorld);
		m_pump_i = 0u;
		m_phase = PumpPhase::Faces;
		m_phase_t0 = rcycles();
		g_bbox[0] = 32767; g_bbox[1] = -32768; g_bbox[2] = 32767; g_bbox[3] = -32768;
		eng::debug::g_eng_prof.cycles[6] = rcycles() - t0; // [6] = matriz (armado)
	}

	/// Procesa hasta `budget` elementos del armado (uno por llamada desde la bomba).
	/// Devuelve los consumidos. Registra por-fase al cerrar cada una ([7]/[8]/[9]).
	eng::u16 pump_step(eng::u16 budget) noexcept {
		eng::u16 done = 0u;
		while (done < budget) {
			if (m_phase == PumpPhase::Faces) {
				if (m_pump_i >= g_face_off_n) {
					eng::debug::g_eng_prof.cycles[7] = rcycles() - m_phase_t0;
					m_phase = PumpPhase::Edges;
					m_pump_i = 0u;
					m_phase_t0 = rcycles();
					continue;
				}
				eng::lib3d::face_visibility_one(m_object,
								m_object.face(g_face_off[m_pump_i++]));
			} else if (m_phase == PumpPhase::Edges) {
				if (m_pump_i >= g_face_off_n) {
					eng::debug::g_eng_prof.cycles[8] = rcycles() - m_phase_t0;
					m_phase = PumpPhase::Verts;
					m_pump_i = 0u;
					m_phase_t0 = rcycles();
					continue;
				}
				eng::lib3d::edge_visibility_convex_one(
					m_object, m_object.face(g_face_off[m_pump_i++]));
			} else if (m_phase == PumpPhase::Verts) {
				if (m_pump_i >= g_vert_off_n) {
					eng::debug::g_eng_prof.cycles[9] = rcycles() - m_phase_t0;
					m_phase = PumpPhase::Done;
					break;
				}
				eng::lib3d::transform_vertex_one(
					m_pc, m_object.node(g_vert_off[m_pump_i++]),
					static_cast<eng::s16>(kWidth / 2),
					static_cast<eng::s16>(kHeight / 2), g_bbox);
			} else {
				break;
			}
			++done;
		}
		return done;
	}

	/// Cierra el armado en el punto de uso (antes del gather): normalmente ya lo
	/// absorbieron las esperas del Blitter; si no, procesa lo que falte en bloque.
	void pump_drain() noexcept {
		while (m_phase != PumpPhase::Done && m_phase != PumpPhase::Idle) {
			(void)pump_step(8u);
		}
	}
#endif

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}

#if FLATSHADE_ASYNC
		// ── Arranque del frame ASYNC (DT-026) ───────────────────────────────────
		// La cadena del frame anterior (contorno + fill acotado + clear encadenados
		// por la IRQ de fin de blit) termina aqui: espera explicita (glFinish),
		// publicacion del buffer ya dibujado (`commit`, mismo swap de `BPLxPT`) y
		// reciclado del plan. El buffer de ESTE frame es el que los jobs de clear de
		// esa cadena dejaron limpio.
		//
		// VEREDICTO MEDIDO (2026-10-17): la cadena async FUNCIONA (esta variante no
		// cuelga) pero con ~60 jobs cortos por frame sale MAS LENTA que la pipeline
		// sincrona en A500 ciclo-exacto: 8,9 fps (5,6 campos) vs 18,05 (2,77). Cada
		// job paga arranque de Blitter (~1,4k) + IRQ de nivel 3 + re-programacion en
		// la ISR (~2,5-4k en total), y con 51 blits de linea eso son ~200k/frame que
		// la via sincrona NO paga (encadena en el mismo hilo sin round-trip de IRQ).
		// El referee async es para cadenas CORTAS de blits LARGOS (los runs de
		// cookie-cut de la 066/212), no para listas de lineas. Se conserva como via
		// documentada + test de sintomas; el default sigue siendo la sincrona.
		if (m_chain_launched) {
			eng::u32 spins = 0u;
			eng::u32 spin_t0 = rcycles();
			while (backend.frame_plan_async_busy()) {
				// El giro cede el bus al Blitter (BLTPRI nasty via `wait_blitter`).
				(void)backend.wait_blitter();
				++spins;
			}
			// Diagnostico (visible en el `detail` del run-status): ciclos esperando
			// la cadena del frame anterior — la porcion no solapada del 5,6 campos.
			g_eng_run_status.detail = rcycles() - spin_t0;
			(void)spins;
			m_scene.commit();
			m_plan.clear();
			m_chain_launched = false;
		}
		g_eng_run_status.detail = 0x11620000u;
		eng::PlaneBytes planes = m_scene.back();

		const eng::u32 t0 = rcycles();
#else
		// El buffer en el que se dibuja este frame (el trasero del triple buffer).
		eng::PlaneBytes planes = m_scene.back();

		const eng::u32 t0 = rcycles();

		// 1) Esperar el fill + pre-clear del frame anterior. Con `FLATSHADE_PUMP` la
		//    ventana de la bomba es esta espera GRANDE (la cola del fill/clear); el
		//    default (0, ver el flag) NO bombea: transform en bloque bajo el fill.
#if FLATSHADE_PUMP
		m_pump_active = true;
#endif
		backend.wait_blitter();
#if FLATSHADE_PUMP
		pump_drain();
		m_pump_active = false;
#endif

		// 2) El swap de display y el pre-clear del buffer siguiente van AL FINAL del
		//    update (tras el transform): ahi el `commit` publica lo dibujado y el clear
		//    queda colgado detras del fill. El orden «clear al principio y transform
		//    encima» (el del original) se midio en 2026-10: 20,43 vs 20,90 — con nuestro
		//    reparto de trabajo dejaba el FILL sin solape y salia peor; la absorcion del
		//    clear bajo el transform no compensa. Se conserva el orden propio.
#endif

		// 3) Contorno (+ fill) sobre el buffer pre-clearado (estado ya precalculado).
#if FLATSHADE_FAITHFUL
#if K_FLATSHADE_ASM
		prepare_fs_args(planes, m_object);
		const eng::u32 td0 = rcycles();
		fs_draw_edges();
		eng::debug::g_eng_prof.cycles[2] = rcycles() - td0;
#elif !FLATSHADE_LEGACY_DRAW
		// Via del engine: reunir las aristas visibles y lanzar contorno EOR + area fill
		// XOR con `retro::flat_shade_xor` (sin esperar el fill; se solapa con el
		// transform del frame siguiente). No hay primitivas de Blitter en `main`.
		eng::retro::OutlineEdge edges[64];
		const eng::u32 td0 = rcycles();
		const eng::u32 ne = gather_visible_edges(m_object, edges, 64u);
#if FLATSHADE_ASYNC
		if (ne != 0u) {
			// Cadena async: contorno + fill acotado + clear del buffer siguiente. El
			// fill acotado (la ganancia de DMA) ya no serializa la CPU: la encadena la
			// IRQ de fin de blit mientras este update sigue con el transform.
			build_chain_plan(planes, edges, ne);
			g_eng_run_status.detail = 0x1163u | (static_cast<eng::u32>(m_plan.blit_job_count()) << 16u);
			m_chain_launched = backend.execute_frame_plan_async(m_plan);
			g_eng_run_status.detail = m_chain_launched
							  ? (0x11640000u |
							     ((backend.blit_async_launches() & 0xffu) << 8u) |
							     (backend.blit_async_ends() & 0xffu))
							  : 0x11640F00u; // bit 8: execute devolvio false
			if (!m_chain_launched) {
				// Backend sin servicio de IRQ (o plan rechazado): caida al camino
				// sincrono con el mismo contorno (sin fill acotado: no hay cadena).
				m_plan.clear();
				eng::retro::flat_shade_xor(
					backend, planes, kBytesPerRow, kPlaneBytes, kPlanes, kWidth,
					kHeight,
					eng::Span<const eng::retro::OutlineEdge> {edges, ne},
					/*area_fill_wait=*/false);
			}
		}
#else
		if (ne != 0u) {
			// Contorno + area fill XOR. Con FLATSHADE_FILL_BBOX=1 el fill se acota a la
			// caja del objeto (barrido contiguo acortado, UN blit). El CONTRATO DE
			// PARIDAD completo (invariantes del relleno XOR — hoist de BLTDPT, orden
			// contorno->fill, caja limpia — con la regresión de rayas 2026-10-17) vive
			// en la cabecera de `eng/retro/flat_shade_xor.hpp`: LEER antes de tocar el
			// camino de render.
#if FLATSHADE_FILL_BBOX
			const eng::s16* fill_bbox = g_bbox;
#else
			const eng::s16* fill_bbox = nullptr;
#endif
			eng::retro::flat_shade_xor(
				backend, planes, kBytesPerRow, kPlaneBytes, kPlanes, kWidth, kHeight,
				eng::Span<const eng::retro::OutlineEdge> {edges, ne},
				/*area_fill_wait=*/false, fill_bbox);
		}
#endif
		eng::debug::g_eng_prof.cycles[2] = rcycles() - td0;
		eng::debug::g_eng_prof.cycles[10] = ne;
#else
		draw_edges(m_object, planes, backend);
#endif
#else
		draw_faces(m_object, planes, backend, m_mask_block.view);
#endif
		const eng::u32 t1 = rcycles();

		// 4) El fill lo lanza la via del engine dentro de `flat_shade_xor`; la ruta legacy
		//    (y la asm) lo lanzan aqui SIN esperarlo.
#if FLATSHADE_LEGACY_DRAW || K_FLATSHADE_ASM
		area_fill_planes(planes, backend, false);
#endif

		// 5) (El `commit` y el pre-clear van tras el transform: ver abajo.)
		const eng::u32 t2a = rcycles();

		// 5) Transform del frame SIGUIENTE (angle++, luz, visibilidad, proyeccion):
		//    su estado lo consumira el draw del proximo update.
#if FLATSHADE_PUMP
		// Interleaving cooperativo (DT-026): solo la matriz aqui (dependencia inmediata
		// del cache del proyector); caras/aristas/vertices los reparte la bomba en las
		// esperas del Blitter (la cola del fill/clear al inicio del update siguiente +
		// las esperas de linea del draw).
		pump_arm();
		const eng::u32 t2 = rcycles(); // [1]/[5] de la telemetria siguen midiendo el update
#else
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
		eng::lib3d::transform_vertices(m_object, static_cast<eng::s16>(kWidth / 2),
					       static_cast<eng::s16>(kHeight / 2), g_bbox);
#endif
		const eng::u32 t2 = rcycles();
		eng::debug::g_eng_prof.cycles[6] = ta - t2a; // update_object_transformation
		eng::debug::g_eng_prof.cycles[7] = tb - ta;  // update_face_visibility
		eng::debug::g_eng_prof.cycles[8] = tc - tb;  // update_edge_visibility_convex
		eng::debug::g_eng_prof.cycles[9] = t2 - tc;  // transform_vertices
#endif

		// 6) Publicar y rotar: `commit` instala la copperlist del buffer recien dibujado
		//    y pasa al siguiente; despues se pre-cleara ESE (ni en pantalla ni en dibujo
		//    ahora), sin esperar, para que quede colgado tras el fill (un solo blit de
		//    1024 filas: los 4 planos son contiguos, como `BitmapClearFast` del original).
#if FLATSHADE_ASYNC
		// En async el `commit` va al arranque del update siguiente (tras
		// `frame_plan_async_wait`) y el clear es el ultimo job de la cadena: aqui no se
		// toca el display.
#else
		m_scene.commit();
#if !FLATSHADE_SKIP_CLEAR
		backend.blitter_clear(m_scene.back(), kPlanes, kBytesPerRow, kPlaneBytes,
				      kWidth, kHeight, false);
#endif
#endif
		const eng::u32 t3 = rcycles();

		eng::debug::g_eng_prof.cycles[0] = t1 - t0;
		eng::debug::g_eng_prof.cycles[1] = t2 - t1;
		eng::debug::g_eng_prof.cycles[5] = t3 - t0;
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		ENG_PROF_FRAME();
		// Campos dinamicos del HUD (contador de frames + fps del ultimo segundo): la
		// ventana se cierra ~1 vez/segundo; el resto de frames solo cuesta la
		// comparacion de `update` (~10-15 ciclos, ver `HudFpsWindow`).
		eng::u32 fps10 = 0u;
		if (m_hud_fps.update(context.frame.frame_index, eng::debug::prof_clock(), fps10)) {
			draw_hud_fields(context.frame.frame_index, fps10);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Texto estatico de la franja: nombre + version (norma del repo), pintado UNA vez.
	void draw_hud_static() {
		auto hud = playfield::Surface {m_hud_pf, playfield::SurfaceRect {0, 0, kWidth, kHudRows}};
		char line[32] = "FLATSHADE ";
		eng::u8 n = 10u;
		char label[16];
		const eng::u8 vlen = eng::debug::version_label(kVersion, label, sizeof(label));
		for (eng::u8 i = 0u; i < vlen && n + 1u < sizeof(line); ++i) {
			line[n++] = label[i];
		}
		line[n] = '\0';
		(void)hud.draw_text_words(0, kHudY, line, kHudColor);
	}

	/// Campos dinamicos del HUD (se pintan juntos ~1 vez/segundo, 2 campos de ancho FIJO):
	///  - contador de frames totales en `kHudCounterX` ("F 12345", 6 caracteres);
	///  - fps del ultimo segundo en `kHudFpsX` ("18.7", 4 caracteres; `fps10` = fps x 10).
	/// Coste por actualizacion: 5 palabras x 8 filas (contador) + 2 palabras x 8 filas
	/// (fps) = ~56 escrituras por segundo; nada por frame (`HudFpsWindow::update`).
	void draw_hud_fields(eng::u32 frames, eng::u32 fps10) {
		auto hud = playfield::Surface {m_hud_pf, playfield::SurfaceRect {0, 0, kWidth, kHudRows}};
		// Contador de frames: campo de 6 caracteres, digitos alineados a la derecha
		// tras la 'F' (hasta 5 digitos; a partir de ahi se recorta el mas alto).
		char field[7] = "F     ";
		char tmp[8];
		eng::u8 t = 0u;
		do {
			tmp[t++] = static_cast<char>('0' + (frames % 10u));
			frames /= 10u;
		} while (frames != 0u && t < 7u);
		eng::u8 n = 1u;
		while (t > 0u && n < 6u) {
			field[n++] = tmp[--t];
		}
		field[6] = '\0';
		(void)hud.draw_text_words(kHudCounterX, kHudY, field, kHudColor);

		// Fps con un decimal, alineado a la derecha en 4 caracteres ("25.0", " 5.4").
		// El tope de formato evita que un valor absurdo (nunca en PAL) desborde el campo.
		eng::u32 v = fps10 > 999u ? 999u : fps10;
		char fps[5] = "    ";
		fps[3] = static_cast<char>('0' + (v % 10u));
		v /= 10u;
		fps[2] = '.';
		fps[1] = static_cast<char>('0' + (v % 10u));
		v /= 10u;
		if (v != 0u) {
			fps[0] = static_cast<char>('0' + (v % 10u));
		}
		fps[4] = '\0';
		(void)hud.draw_text_words(kHudFpsX, kHudY, fps, kHudColor);
	}

	bool m_init_ok = false;
	bool m_memory_ok = false;
	eng::s16 m_angle = 0;
	eng::debug::HudFpsWindow m_hud_fps {}; ///< ventana de 1 s para el campo de fps del HUD
#if FLATSHADE_PUMP
	/// Estado del transform cooperativo (DT-026): tipo del cache del proyector,
	/// fase/cursor del armado y marca de tiempo de la fase en curso ([7..9]).
	using Proj = eng::math::projector<eng::math3d::Affine3<>>;
	PumpPhase m_phase = PumpPhase::Idle;
	eng::u32 m_pump_i = 0u;
	eng::u32 m_phase_t0 = 0u;
	Proj::cache m_pc {};
	bool m_pump_active = false; ///< ventana de bombeo (la espera grande del inicio)
#endif
#if FLATSHADE_ASYNC
	/// Plan de blits del frame para el referee async (DT-026): contorno EOR + area fill
	/// acotado + clear del buffer siguiente. Se recicla cada frame tras `async_wait`.
	eng::graphics::FramePlan m_plan {};
	/// `true` entre `execute_frame_plan_async` y el `frame_plan_async_wait` + `commit`
	/// del update siguiente (alli se publica y se limpia el plan).
	bool m_chain_launched = false;
#endif
	scene::Scene m_scene {};
	eng::Block<eng::MaskTag> m_mask_block {};
	eng::Block<eng::PlaneTag> m_hud_block {};
	playfield::ContiguousPlayfield m_hud_pf {}; ///< lienzo de la franja (1 bpp, por palabras)
	eng::object3d::Object3D m_object {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	FlatShadeDemo game {};
	eng::Engine engine {backend, game};
	// La demo no usa la cola de fondo: sin servicio de Blitter, las esperas del Blitter
	// son un sondeo apretado sin llamadas de servicio por vuelta (mas barato por linea).
	engine.set_blit_service_enabled(false);
	// Bucle por defecto: VBlank por IRQ + espera bloqueada en `Wait()` (sin sondeo de VPOSR).
	engine.run_frames(0xffff);

	return 0;
}

