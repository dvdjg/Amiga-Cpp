// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/079_wireframe --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/079_wireframe --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/079_wireframe --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/079_wireframe --keep-running

// Demo 079 - wireframe (PORTE de demoscene-repo-orig/effects/wireframe/wireframe.c)
//
// Dibuja el objeto `pilka` (malla `obj2c`) en ALAMBRE con la LINEA POR BLITTER, como la
// demo original. El modelo de objeto (formato `obj2c` + `Object3D`) se ha portado fiel a
// `lib3d` en `eng/platform/amiga/object3d.hpp`; la matematica 4.12 viene de
// `lib2d`/`math3d` (HOST-010/011); la visibilidad y el transform son el port verbatim de
// `wireframe.c`; y el trazado de lineas vive en la capa reutilizable
// `eng::retro::wire_lines` (`engine/include/eng/retro/wireframe.hpp`), que fija el
// preludio del modo linea UNA vez y programa por arista el Bresenham ya preparado
// (`blitter_line_prepare`/`blitter_line_draw`), igual que el `DrawObject` original.
//
// Display y doble buffer: ventana 256x256x4 con los registros del original
// (`SetupPlayfield(MODE_LORES,4,X(32),Y(0),256,256)` + `SetupBitplaneFetch`) y doble
// buffer con swap de copperlist por frame (el original rota 5 planos y parchea
// BPLxPT; aqui se usan 2 buffers de 4 planos, equivalente sin tearing).
//
// Franja de depuracion (norma del repo): las 16 filas inferiores de la ventana muestran
// nombre + version + contador de frames, pintadas con `Surface::draw_text_words` (por
// palabras de 16 px, nunca pixel a pixel) DIRECTAMENTE en los planos del anillo, en las
// filas que el balon no alcanza. Con los 4 planos visibles a 1 en el texto, el color de la
// franja es el 15 de la paleta del alambre (0xCEF). Sin plano extra ni zona de Copper.
#include <eng/platform/amiga/object3d.hpp>
#include <eng/api/api.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/composition/limits.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/lib3d.hpp>
#include <eng/retro/wireframe.hpp>

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

// --- Datos del efecto (copiados TAL CUAL del demoscene) ----------------------
using eng::object3d::Mesh3D;
#define __data
using u_short = eng::u16;
#include "data/pilka.c"
#include "data/wireframe-pal.c"
#undef __data

// Angulo FIJO para capturas deterministas (0 = anima). Con esto la captura no depende de
// la fase de giro del balon y dos builds se pueden comparar PIXEL A PIXEL (`freeze-diff`).
#ifndef WIREFRAME_FREEZE_ANGLE
#define WIREFRAME_FREEZE_ANGLE 0
#endif

namespace {

namespace obj = eng::object3d;
namespace copper = eng::copper;
namespace playfield = eng::playfield;

// Geometria del original (256x256, 4 planos).
constexpr eng::u16 kWidth = 256;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8; // 32
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u8 kRing = static_cast<eng::u8>(kPlanes + 1); // anillo de 5 (DEPTH+1)
constexpr eng::u32 kBitmapBytes = kPlaneBytes * kRing;
constexpr eng::u32 kCopperPerList = 512;

// Franja de depuracion (norma del repo): nombre + version + contador de frames en una
// ZONA APARTE de la pantalla, sobre un quinto plano de 1 bpp que la copperlist activa con
// un WAIT (`wait_line_safe`, par de overflow del builder). El texto se pinta con
// `Surface::draw_text_words` (por palabras de 16 px, dos glifos por palabra) en su propio
// bloque de 512 B y se ACTUALIZA SOLO AL CAMBIO: la parte estatica (nombre + version) una
// vez en init y el contador ~1 vez/segundo (24 escrituras de palabra). Coste por frame:
// CERO (la zona y la paleta son estaticas en la copperlist).
//
// GEOMETRIA (medida en emulador; el comparador de WAIT de WinUAE usa semantica `>=` sobre
// V[7:0], custom.cpp:4416 `coppercomp`): un WAIT por encima de la 255 cae SIEMPRE en la
// raster 255 (H~222), no en la linea pedida. Con el WAIT del builder (`wait_line_safe`,
// par 0xffdf/0xfffe) la zona se activa ahi: el plano 5 empieza a fetchear en la raster
// 256 = fila 212 de la ventana visible (VSTART=0x2c=44) y la franja queda en las filas
// ~216..224, por debajo del balon. La paleta 16..31 replica la del alambre (16 = tinta de
// la franja) para que los pixeles del balon dentro de la banda no cambien de color.
constexpr eng::u16 kHudRows = 16;
constexpr eng::u32 kHudBytes = static_cast<eng::u32>(kBytesPerRow) * kHudRows;
constexpr eng::u16 kHudZoneWait = 284; // linea pedida al builder; dispara en 255 (>=)
constexpr eng::u8 kHudColor = 1u;      // 1 bpp: patron del plano de la franja
/// X del campo del contador en el lienzo del HUD (multiplo de 16 para `draw_text_words`;
/// queda tras "WIREFRAME vX.Y.BBBBB").
constexpr eng::s32 kHudCounterX = 176;

/// Paleta de la BANDA del HUD (indices 16..31, la zona anade +16 al indice de color):
/// 16 = tinta de la franja; 17..31 = copia de 1..15 para que los pixeles del balon que
/// caen dentro de la banda (rastro con varios planos) conserven su color. La llena `init`
/// con los valores de `wireframe_colors` y se emite en cada copperlist (16 MOVEs estaticos).
eng::u16 g_hud_band_colors[32] {};

/// Version de la demo (major.minor; el build lo inyecta `build-demo.sh`).
constexpr eng::debug::Version kVersion {2u, 0u};

// Registros del display del original (SetupMode/BitplaneFetch/DisplayWindow para
// MODE_LORES, X(32), Y(0), 256x256). Recalculado de SetupBitplaneFetchImpl.c:
// xs = (0x81+32)<<2 - 4 = 640; fetchstart=64,prefetch=64,w=1024; ddfstrt=(640&-64)-64=576;
// ddfstop=576+1024-64=1536; BPLCON1=(xs&15)*0x11=0. DIW: xs=161, xe=(161+256)&0xff=161
// (ventana de 256 por wrap), ys=0x2c, ye=(0x2c+256)&0xff=0x2c.
constexpr eng::u16 kDiwstrt = 0x2ca1;
constexpr eng::u16 kDiwstop = 0x2ca1;
constexpr eng::u16 kDdfstrt = 0x0048;
constexpr eng::u16 kDdfstop = 0x00c0;
constexpr eng::u16 kBplcon0 = 0x4000;    // 4 planos, color
constexpr eng::u16 kBplcon1 = 0x0000;    // fine scroll 0 (calculado)

// --- Recorrido del object model (PORTADO VERBATIM de wireframe.c) ------------

void update_face_visibility_fast(obj::Object3D& object) {
	const eng::s16 cx = object.camera.x.v;
	const eng::s16 cy = object.camera.y.v;
	const eng::s16 cz = object.camera.z.v;
	for (const eng::Ref<obj::Face>& fr : object.faces()) {
		obj::Face* face = fr.get();
		eng::s16 px, py, pz;
		{
			const eng::s16 i = obj::face_indices(face)[0].vertex;
			const obj::Point3D* p = object.point(i);
			px = static_cast<eng::s16>(cx - p->x.v);
			py = static_cast<eng::s16>(cy - p->y.v);
			pz = static_cast<eng::s16>(cz - p->z.v);
		}
		{
			// mul_wide (= muls.w nativo): el producto 16x16 con operandos promovidos a s32
			// acabaria en __mulsi3 (multiplicacion 32x32 por software).
			const eng::s32 v = eng::math::mul_wide(face->normal[0].v, px) +
					   eng::math::mul_wide(face->normal[1].v, py) +
					   eng::math::mul_wide(face->normal[2].v, pz);
			face->flags = static_cast<eng::s8>(v >= 0 ? 0 : -1);
		}
	}
}

void update_edge_visibility(obj::Object3D& object) {
	const eng::s16 s = 1;
	for (const eng::Ref<obj::Face>& fr : object.faces()) {
		obj::Face* face = fr.get();
		if (face->flags >= 0) {
			const eng::Span<obj::FaceIndex> fi = obj::face_indices(face);
			object.node(fi[0].vertex)->flags = static_cast<eng::s8>(s);
			object.edge(fi[0].edge)->flags = static_cast<eng::s8>(s);
			object.node(fi[1].vertex)->flags = static_cast<eng::s8>(s);
			object.edge(fi[1].edge)->flags = static_cast<eng::s8>(s);
			for (eng::s16 k = 2; k < face->count; ++k) {
				object.node(fi[k].vertex)->flags = static_cast<eng::s8>(s);
				object.edge(fi[k].edge)->flags = static_cast<eng::s8>(s);
			}
		}
	}
}

// El transform+proyeccion es `eng::lib3d::transform_vertices` (API reutilizable del engine,
// promovida desde este port y validada por HOST-014/047): la identidad empaquetada
// `(m0+y)·(m1+x) + m2·z − x·y` con `muls.w`/`divs.w` nativos vive en el proyector del
// engine (`eng::math::projector`), no duplicada aqui con macros (los productos 32x32 de
// los macros acababan en `__mulsi3`).

/// Reune las aristas VISIBLES (`edge->flags != 0`) ya proyectadas y limpia el flag (el
/// original lo limpia en `DrawObject`). La mascara de color es `1` (el plano activo del
/// anillo): la franja de 5 planos hace el rastro con los frames anteriores.
inline eng::u32 gather_visible_edges(obj::Object3D& object, eng::retro::OutlineEdge* out,
				     eng::u32 cap) {
	eng::u32 n = 0u;
	for (const eng::Ref<obj::Edge>& er : object.edges()) {
		obj::Edge* edge = er.get();
		if (edge->flags != 0 && n < cap) {
			edge->flags = 0;
			const obj::Point3D* a = object.vertex(edge->point[0]);
			const obj::Point3D* b = object.vertex(edge->point[1]);
			out[n++] = eng::retro::OutlineEdge {a->x.v, a->y.v, b->x.v, b->y.v, 1u};
		}
	}
	return n;
}

struct WireframeDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		ENG_PROF_INIT(4);
		m_memory_ok = backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007901u);
			return;
		}

		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitmapBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(kRing * kCopperPerList, 16);
		m_hud_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kHudBytes, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_hud_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007902u);
			return;
		}

		// Paleta de la banda del HUD (16..31): 16 = tinta de la franja (blanco); 17..31
		// replican 1..15 para que los pixeles del balon dentro de la banda conserven su
		// color (la zona anade +16 al indice). Se emite una vez por lista (estatico).
		g_hud_band_colors[16] = 0xFFFu;
		for (eng::u8 k = 1u; k < 16u; ++k) {
			g_hud_band_colors[16u + k] = wireframe_colors[k];
		}

		// Lienzo de la franja: su PROPIO bloque de 1 plano (256x16, 512 B). El
		// `plane_stride` de `bind_raw` es el tamano de PLANO (kHudBytes), no el de fila.
		(void)m_hud_pf.bind_raw(m_hud_block.view.data(),
					static_cast<eng::u32>(m_hud_block.view.size()),
					kWidth, kHudRows, 1, kHudBytes);
		// El lienzo no nace limpio: se borra UNA vez; despues solo se redibuja al cambio.
		(void)backend.blitter_clear(m_hud_block.view.subspan(0u, kHudBytes), 1u, kBytesPerRow,
					    kHudBytes, kWidth, kHudRows);
		draw_hud_static();
		draw_hud_counter(0u);

		for (eng::u8 a = 0; a < kRing; ++a) {
			if (!build_copper(a)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00007903u);
				return;
			}
		}
		backend.takeover_display(m_copper_views[0]);

		// Pre-clear de TODO el anillo: cada update dibuja en el plano siguiente y solo
		// lanza (sin esperar) el clear del que se dibujara la proxima vez; ese plano esta
		// OCULTO en la copperlist (los 4 mostrados son active, active+2..active+4), asi
		// que su borrado se solapa con la CPU del frame y nunca se ve a medias.
		for (eng::u8 a = 0; a < kRing; ++a) {
			eng::PlaneBytes p = m_bitplane_block.view.subspan(
				static_cast<eng::u32>(a) * kPlaneBytes, kPlaneBytes);
			(void)backend.blitter_clear(p, 1, kBytesPerRow, kPlaneBytes, kWidth, kHeight);
		}

		obj::new_object3d(m_object, pilka);
		// fx4i(-250) = -250 * 16 = -4000 (4.12).
		m_object.translate.z = eng::retro::q0 {-4000};

		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(pilka.vertices));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (m_bitplane_block.view.data() == nullptr) {
			return;
		}

		// Anillo de 5 planos (como el original): dibuja SOLO el plano `active` (ya
		// pre-limpiado en el update anterior, oculto entonces); la copperlist lo muestra
		// como bit 3 y deja el rastro de los 3 frames anteriores en los bits 2..0
		// (colores 8/4/2/1).
		const eng::u8 active = m_active;
		eng::PlaneBytes plane = m_bitplane_block.view.subspan(static_cast<eng::u32>(active) * kPlaneBytes, kPlaneBytes);

		ENG_PROF_BEGIN(1);
#if WIREFRAME_FREEZE_ANGLE
		const eng::u16 turns = static_cast<eng::u16>(WIREFRAME_FREEZE_ANGLE);
#else
		const eng::u16 turns = static_cast<eng::u16>(context.frame.frame_index * 8u);
#endif
		m_object.rotate.x = m_object.rotate.y = m_object.rotate.z = eng::retro::turns(turns);

		obj::update_object_transformation(m_object);
		update_face_visibility_fast(m_object);
		update_edge_visibility(m_object);
		// Transform+proyeccion del engine (mismo algoritmo del port; el `bbox` es scratch
		// de la rutina, aqui no se usa para recortar nada).
		eng::s16 bbox[4];
		eng::lib3d::transform_vertices(m_object, static_cast<eng::s16>(kWidth / 2),
					       static_cast<eng::s16>(kHeight / 2), bbox);
		ENG_PROF_END(1);

		// Aristas visibles -> alambre por Blitter (`retro::wire_lines`): preludio comun
		// fijado UNA vez y por arista el Bresenham preparado (`blitter_line_prepare`)
		// programado directamente (`blitter_line_draw`), como el `DrawObject` original.
		// Sincrono (espera al final) porque el buffer se publica en este mismo frame.
		ENG_PROF_BEGIN(2);
		eng::retro::OutlineEdge edges[96];
		const eng::u32 ne = gather_visible_edges(m_object, edges, 96u);
		if (ne != 0u) {
			(void)eng::retro::wire_lines(backend, plane, kBytesPerRow, kPlaneBytes, 1u,
						     eng::Span<const eng::retro::OutlineEdge> {edges, ne},
						     /*wait=*/true);
		}
		ENG_PROF_END(2);


		ENG_PROF_BEGIN(3);
		backend.install_copper_list(m_copper_views[active]);
		// Pre-clear del plano OCULTO que se dibujara en el update siguiente: se lanza sin
		// esperar y se solapa con la CPU del frame (el primer `blitter_line_draw` del
		// update siguiente sincroniza con el Blitter; el plano no se ve hasta 4 frames
		// despues, asi que no hay riesgo de mostrarlo a medias).
		const eng::u8 next = static_cast<eng::u8>((active + 1u) % kRing);
		eng::PlaneBytes next_plane = m_bitplane_block.view.subspan(
			static_cast<eng::u32>(next) * kPlaneBytes, kPlaneBytes);
		(void)backend.blitter_clear(next_plane, 1, kBytesPerRow, kPlaneBytes, kWidth, kHeight,
					    /*wait=*/false);
		ENG_PROF_END(3);
		m_active = next;
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		ENG_PROF_FRAME();
		// Contador de frames del HUD: 1 actualizacion por segundo sobre un campo de ancho
		// FIJO (3 palabras = 24 escrituras), coste ~0 por frame. Ver `draw_hud_counter`.
		if ((context.frame.frame_index & 63u) == 0u) {
			draw_hud_counter(context.frame.frame_index);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Texto estatico de la franja: nombre + version (norma del repo). Se pinta UNA vez en
	/// init sobre el lienzo de 1 bpp (`m_hud_pf`). `Surface::draw_text_words` escribe por
	/// palabras de 16 px (dos glifos por palabra; nunca pixel a pixel).
	void draw_hud_static() {
		auto hud = playfield::Surface {m_hud_pf, playfield::SurfaceRect {0, 0, kWidth, kHudRows}};
		char line[32] = "WIREFRAME ";
		eng::u8 n = 10u;
		char label[16];
		const eng::u8 vlen = eng::debug::version_label(kVersion, label, sizeof(label));
		for (eng::u8 i = 0u; i < vlen && n + 1u < sizeof(line); ++i) {
			line[n++] = label[i];
		}
		line[n] = '\0';
		(void)hud.draw_text_words(0, 4, line, kHudColor);
	}

	/// Contador de frames del HUD: campo FIJO de 6 caracteres ("F 1234", relleno con
	/// espacios) en `kHudCounterX` (multiplo de 16 para `draw_text_words`). Se redibuja
	/// solo cuando el numero cambia (~1 vez/segundo): 3 palabras x 8 filas = 24 escrituras.
	void draw_hud_counter(eng::u32 frames) {
		auto hud = playfield::Surface {m_hud_pf, playfield::SurfaceRect {0, 0, kWidth, kHudRows}};
		char field[7] = "F     ";
		char tmp[8];
		eng::u8 t = 0u;
		do {
			tmp[t++] = static_cast<char>('0' + (frames % 10u));
			frames /= 10u;
		} while (frames != 0u && t < 7u);
		eng::u8 n = 2u;
		while (t > 0u && n < 6u) {
			field[n++] = tmp[--t];
		}
		field[6] = '\0';
		(void)hud.draw_text_words(kHudCounterX, 4, field, kHudColor);
	}

	/// Construye la copperlist para el plano `active`: bit3=planes[active],
	/// bit2=planes[active-1], bit1=planes[active-2], bit0=planes[active-3] (mod 5),
	/// como el parcheo de `BPLxPT` del original; mas la ZONA de la franja (5º plano,
	/// 1 bpp) activada por el WAIT del builder al final de la lista.
	bool build_copper(eng::u8 active) {
		// Anillo de copperlists: cada tramo del bloque es una lista independiente.
		const eng::Bytes<eng::CopperTag> slice = m_copper_block.view.subspan(
			static_cast<eng::u32>(active) * kCopperPerList, kCopperPerList);
		copper::Scheduler sched { eng::Block<eng::CopperTag> { slice, m_copper_block.kind } };
		sched.emit_planes_display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBytesPerRow, kBplcon0,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		for (eng::u8 n = 0; n < kPlanes; ++n) {
			const eng::u8 idx = static_cast<eng::u8>((active + 2u + n) % kRing);
			sched.move_bitplane_pointer(n, m_bitplane_block.mem_view_chip().address(static_cast<eng::s32>(idx) * static_cast<eng::s32>(kPlaneBytes)));
		}
		// 5º plano (franja): el puntero se fija al principio de la lista (como el build
		// original del port; con la zona activada solo en el tramo final, ver abajo).
		sched.move_bitplane_pointer(kPlanes, m_hud_block.mem_view_chip().address(0));
		sched.move(copper::Register::BPLCON1, kBplcon1);
		sched.emit_palette(wireframe_colors, 0, 16);
		// Paleta de la banda 16..31 (16 = tinta de la franja, 17..31 = color del balon sin
		// cambiar dentro de la banda; ver `g_hud_band_colors` en `init`).
		sched.emit_palette(eng::PaletteWords {g_hud_band_colors, 32u}, 16, 16);
		// Zona del HUD: WAIT del builder (par de overflow; dispara en la raster 255 por la
		// semantica `>=` del comparador, ver la nota de geometria arriba) + BPLCON0 con 5
		// planos. La zona dura hasta el final de la lista.
		sched.wait_line_safe(kHudZoneWait);
		sched.move(copper::Register::BPLCON0,
			   eng::graphics::composition::bplcon0_for(
				   eng::graphics::composition::SceneMode::Standard,
				   static_cast<eng::u8>(kPlanes + 1u)));
		sched.end();
		m_copper_views[active] = sched.chip_view();
		return sched.ok();
	}

	bool m_memory_ok = false;
	eng::u8 m_active = 0;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::PlaneTag> m_hud_block {};
	playfield::ContiguousPlayfield m_hud_pf {}; ///< lienzo de la franja (1 bpp, texto por palabras)
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::ChipView<eng::CopperTag> m_copper_views[kRing] {};
	eng::object3d::Object3D m_object {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	WireframeDemo game {};
	eng::Engine engine {backend, game};
	// La demo no usa la cola de fondo: sin servicio de Blitter, las esperas del Blitter
	// son un sondeo apretado sin llamadas de servicio por vuelta (mas barato por linea).
	engine.set_blit_service_enabled(false);
	// Bucle por defecto: VBlank por IRQ + espera bloqueada en `Wait()` (sin sondeo de
	// VPOSR); el marco donde el balon no puede pintar (las esperas) queda para el fondo.
	engine.run_frames(0xffff);

	return 0;
}
