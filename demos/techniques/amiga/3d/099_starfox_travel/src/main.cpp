// ============================================================================
// Demo 099 - Corneria rail travel (Star Fox style)
// ============================================================================
//
// Laboratorio de relleno de poligonos por bitplane: solo foreground 3D
// (Arwing, dos cazas, arco y edificio) sobre negro. No hay fondo.
//
// Render: 4 bitplanes de display en 3 buffers, pero SOLO se renderizan los
// planos 0..2 con los 7 colores opacos 1..7; el plano 3 queda a 0. Cada frame
// limpia esos 3 planos y hace UN unico fill por plano (3 pasadas de Blitter):
// el contorno de cada plano es la union de las caras cuyo color tiene ese bit.
// El HUD de las filas 208..255 se pinta una vez por buffer.
//
// La camara mantiene yaw/pitch/roll; los quads de las naves se triangulan y
// las normales se orientan por el centro del modelo.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release

#include <eng/api/3d.hpp>
#include <eng/api/api.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/core/data/polygon.hpp>
#include <eng/core/math/isqrt.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/platform/amiga/polygon_fill.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/retro/mesh_primitives.hpp>

namespace {

namespace comp = eng::graphics::composition;
namespace copper = eng::copper;
namespace math3d = eng::math3d;
namespace scene3d = eng::scene3d;
namespace retro = eng::retro;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

// --- Display -----------------------------------------------------------------
constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
// 4 bitplanes de display; los objetos 3D usan SOLO 0..2 (7 colores opacos),
// el plano 3 es un fondo estatico de 1 bit (color 8).
constexpr u8 kFgPlanes = 3u;
constexpr u8 kTotalPlanes = 4u;
constexpr u8 kBuffers = 3u;

// La franja inferior del HUD de bitplanes queda estatica; el resto de la
// pantalla es el fondo de vuelo que se repinta cada frame.
constexpr u16 kHudY = 208u;
constexpr u16 kHudH = kHeight - kHudY;
constexpr u16 kFlightBottom = kHudY - 1u; // filas 0..207

// --- Camara ------------------------------------------------------------------
// Espacio de camara: +x derecha, +y abajo, +z delante; camara en el origen.
constexpr s16 kFocal = 280;
constexpr s16 kCx = 160;
constexpr s16 kCy = 104;
constexpr s16 kNearZ = 48;
constexpr s16 kHalfW = 160;
constexpr s16 kHalfH = 104;

// --- Paleta de 4 planos -----------------------------------------------------
// 0..7: los SIETE colores de todos los poligonos 3D (0 = negro/transparente).
// 8: fondo estatico (unico bit del plano 3). 9..15 sin uso.
constexpr eng::Palette32 kPalette {{
	0x000, // 0 negro / fuera de objeto
	0xfff, // 1 blanco
	0xccc, // 2 gris claro
	0x888, // 3 gris
	0x48f, // 4 azul
	0x247, // 5 azul oscuro
	0xf65, // 6 salmon
	0xa22, // 7 granate
	0x123, // 8 fondo azul muy oscuro
	0x000, // 9
	0x000, // 10
	0x000, // 11
	0x000, // 12
	0x000, // 13
	0x000, // 14
	0x000, // 15
}};

constexpr u8 kColorPlayerBase = 0u;
constexpr u8 kColorEnemyBase = 3u;
constexpr u8 kColorEnemyBolt = 6u;
constexpr u8 kColorPlayerBolt = 1u;
constexpr u8 kColorEngine = 4u;
constexpr u8 kColorHudTitle = 1u;
constexpr u8 kColorHudSubtitle = 2u;

// Sol comun de la escena (espacio de mundo).
constexpr s16 kSunX = -220;
constexpr s16 kSunY = -240;
constexpr s16 kSunZ = -120;

constexpr eng::debug::Version kVersion {1u, 0u};

enum : u8 { kProfBg = 0u, kProfUpdate = 1u, kProfSolids = 2u,
	   kProfGlow = 3u, kProfBolts = 4u, kProfHud = 5u,
	   kProfShips = 13u, kProfScenery = 14u };

#ifndef K099_SKIP_SOLIDS
#define K099_SKIP_SOLIDS 0
#endif
#ifndef K099_SKIP_BOLTS
#define K099_SKIP_BOLTS 0
#endif
// 0 = CPU exacto, 1 = per-plane Blitter, 2 = router automatico.
#ifndef K099_FILL_MODE
#define K099_FILL_MODE 1
#endif
enum : u8 { kRouteCpu = 0u, kRoutePlanes = 1u };

// --- Limites de contenido ----------------------------------------------------
constexpr u16 kEnemies = 2u;
constexpr u16 kScenery = 2u;
constexpr u16 kBoltSlots = 8u;
constexpr u16 kMaxItems = 1u + kEnemies + kScenery;
constexpr u16 kMaxDrawFaces = kMaxItems * 32u;

constexpr s16 kPlayerZ = 300;
constexpr s16 kPlayerY = 55;
constexpr s16 kGroundY = 110;
constexpr s16 kEnemyResetZ = 210;
constexpr s16 kEnemySpeed = 5;
constexpr s16 kSceneryResetZ = 120;

// --- Codigos de fallo de arranque -------------------------------------------
enum : u32 {
	kBootMemory = 0x00009901u,
	kBootCompose = 0x00009902u,
	kBootModels = 0x00009903u,
};

using Camera = eng::scene3d::Camera;
using CT = eng::math::scalar_traits<eng::coord>;

constexpr Camera kCamera {CT::from_int(kFocal), kCx, kCy, CT::from_int(kNearZ),
			  CT::from_int(kHalfW), CT::from_int(kHalfH)};

constexpr retro::Rect kFlightClip = retro::rect(0, 0, kWidth, kHudY);


// --- Mallas ------------------------------------------------------------------
constexpr u16 kMaxVerts = 32u;
constexpr u16 kMaxIndices = 128u;
constexpr u16 kMaxFaces = 32u;
using MeshData = retro::MeshData<eng::coord, kMaxVerts, kMaxIndices, kMaxFaces>;
using ModelHandle = scene3d::ModelHandle;

/// Grupos de color por cara. El nivel 0 es sombra, 1 medio, 2 luz.
enum : u8 { kGroupHull = 0u, kGroupBlue = 1u, kGroupCanopy = 2u,
	    kGroupEnemy = 3u, kGroupBuilding = 4u, kGroupStone = 5u,
	    kGroupGreen = 6u, kGroupMetal = 7u };

constexpr u8 kShade[8][3] = {
	{3u, 2u, 1u},   // casco blanco/gris
	{5u, 4u, 4u},   // azul
	{3u, 2u, 2u},   // cabina / gris oscuro
	{7u, 6u, 6u},   // enemigo
	{3u, 3u, 2u},   // edificio
	{2u, 2u, 1u},   // piedra
	{4u, 4u, 2u},   // verde
	{5u, 5u, 4u},   // metal
};

struct Rng {
	u32 state = 0x1234abcdU;

	[[nodiscard]] u16 next() noexcept {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		return static_cast<u16>(state);
	}
};

/// Una cara de construccion: n = 3/4 indices y grupo de color.
struct FaceDef {
	u8 n;
	u16 a;
	u16 b;
	u16 c;
	u16 d;
	u8 group;
};

/// Vertex helper with integer scale and inverted Y (0 = base, negative = up).
void add_v(MeshData& mesh, int x, int y, int z, int sx, int sy, int sz,
	   int sdx = 1, int sdy = 1, int sdz = 1) noexcept {
	(void)mesh.add_vertex(math3d::vec3((x * sx) / sdx, -(y * sy) / sdy, (z * sz) / sdz));
}

void add_tri(MeshData& mesh, u8* groups, u8& count, u16 a, u16 b, u16 c,
	     u8 group) noexcept {
	const u16 ids[3] = {a, b, c};
	(void)mesh.add_face(ids, 3u);
	if (count < kMaxFaces) groups[count++] = group;
}

void add_quad(MeshData& mesh, u8* groups, u8& count, u16 a, u16 b, u16 c, u16 d,
	      u8 group) noexcept {
	const u16 ids[4] = {a, b, c, d};
	(void)mesh.add_face(ids, 4u);
	if (count < kMaxFaces) groups[count++] = group;
}

void add_quad_split(MeshData& mesh, u8* groups, u8& count, u16 a, u16 b,
		    u16 c, u16 d, u8 group) noexcept {
	add_tri(mesh, groups, count, a, b, c, group);
	add_tri(mesh, groups, count, a, c, d, group);
}

void add_faces(MeshData& mesh, u8* groups, u8& count, const FaceDef* defs,
	       u16 n, bool split_quads = false) noexcept {
	for (u16 i = 0u; i < n; ++i) {
		if (defs[i].n == 4u) {
			if (split_quads) {
				add_quad_split(mesh, groups, count, defs[i].a, defs[i].b,
					       defs[i].c, defs[i].d, defs[i].group);
			} else {
				add_quad(mesh, groups, count, defs[i].a, defs[i].b,
					 defs[i].c, defs[i].d, defs[i].group);
			}
		} else {
			add_tri(mesh, groups, count, defs[i].a, defs[i].b, defs[i].c,
				defs[i].group);
		}
	}
}

/// Arwing de 23 vertices / 31 triangulos (geometria SNES simplificada con
/// cabina elevada y alas/G-Diffusers).
void build_arwing(MeshData& mesh, u8* groups, u8& count) noexcept {
	const int v[23][3] = {
		{0, 0, 28},
		{6, 4, 10}, {-6, 4, 10}, {6, -3, 10}, {-6, -3, 10},
		{5, 3, -12}, {-5, 3, -12}, {5, -2, -12}, {-5, -2, -12},
		{0, 6, -22}, {0, -1, -22},
		{-22, 2, -4}, {-18, 8, -14}, {22, 2, -4}, {18, 8, -14},
		{-10, 5, -2}, {-10, -1, -8}, {10, 5, -2}, {10, -1, -8},
		// cabina elevada (pico delantero, trasero y laterales)
		{0, 8, 3}, {0, 8, -5}, {-4, 5, -1}, {4, 5, -1},
	};
	for (const auto& p : v) add_v(mesh, p[0], p[1], p[2], 5, 5, 5, 5, 5, 5);

	const FaceDef f[] = {
		{3, 0, 1, 2, 0, kGroupHull}, {3, 0, 2, 4, 0, kGroupHull},
		{3, 0, 4, 3, 0, kGroupHull}, {3, 0, 3, 1, 0, kGroupHull},
		{4, 1, 5, 6, 2, kGroupHull}, {3, 1, 2, 6, 0, kGroupHull},
		{4, 3, 4, 8, 7, kGroupHull},
		{4, 1, 3, 7, 5, kGroupCanopy}, {4, 2, 6, 8, 4, kGroupCanopy},
		{3, 5, 9, 6, 0, kGroupHull}, {3, 7, 10, 8, 0, kGroupCanopy},
		{4, 5, 7, 10, 9, kGroupHull},
		{4, 2, 6, 12, 11, kGroupBlue}, {3, 6, 8, 12, 0, kGroupBlue},
		{4, 1, 5, 14, 13, kGroupBlue}, {3, 5, 7, 14, 0, kGroupBlue},
		{4, 11, 15, 16, 12, kGroupBlue}, {4, 13, 17, 18, 14, kGroupBlue},
		// cabina de cristal azul
		{3, 19, 21, 22, 0, kGroupBlue}, {3, 20, 22, 21, 0, kGroupBlue},
		{3, 19, 20, 21, 0, kGroupBlue}, {3, 19, 22, 20, 0, kGroupBlue},
	};
	add_faces(mesh, groups, count, f,
		  static_cast<u16>(sizeof(f) / sizeof(f[0])), true);
}

/// Caza enemigo de 11 vertices; una sola gama roja.
void build_enemy(MeshData& mesh, u8* groups, u8& count) noexcept {
	const int v[11][3] = {
		{0, 3, 14}, {5, 2, 2}, {-5, 2, 2}, {5, -2, 2}, {-5, -2, 2},
		{3, 1, -10}, {-3, 1, -10}, {3, -1, -10}, {-3, -1, -10},
		{-12, 1, -2}, {12, 1, -2},
	};
	for (const auto& p : v) add_v(mesh, p[0], p[1], p[2], 5, 5, 5, 4, 4, 4);

	const FaceDef f[] = {
		{3, 0, 1, 2, 0, kGroupEnemy}, {3, 0, 2, 4, 0, kGroupEnemy},
		{3, 0, 4, 3, 0, kGroupEnemy}, {3, 0, 3, 1, 0, kGroupEnemy},
		{4, 1, 5, 6, 2, kGroupEnemy}, {4, 3, 4, 8, 7, kGroupEnemy},
		{4, 1, 3, 7, 5, kGroupEnemy}, {4, 2, 6, 8, 4, kGroupEnemy},
		{3, 2, 6, 9, 0, kGroupEnemy}, {3, 1, 5, 10, 0, kGroupEnemy},
	};
	add_faces(mesh, groups, count, f,
		  static_cast<u16>(sizeof(f) / sizeof(f[0])), true);
}

/// Arco de piedra: dos pilares y dintel.
void build_arch(MeshData& mesh, u8* groups, u8& count) noexcept {
	const int v[24][3] = {
		{-18, 0, -6}, {-12, 0, -6}, {-12, 0, 6}, {-18, 0, 6},
		{-18, 28, -6}, {-12, 28, -6}, {-12, 28, 6}, {-18, 28, 6},
		{12, 0, -6}, {18, 0, -6}, {18, 0, 6}, {12, 0, 6},
		{12, 28, -6}, {18, 28, -6}, {18, 28, 6}, {12, 28, 6},
		{-18, 28, -8}, {18, 28, -8}, {18, 36, -8}, {-18, 36, -8},
		{-18, 28, 8}, {18, 28, 8}, {18, 36, 8}, {-18, 36, 8},
	};
	for (const auto& p : v) add_v(mesh, p[0], p[1], p[2], 7, 5, 7, 2, 2, 2);

	const FaceDef f[] = {
		{4, 0, 1, 5, 4, kGroupStone}, {4, 1, 2, 6, 5, kGroupStone},
		{4, 2, 3, 7, 6, kGroupStone}, {4, 3, 0, 4, 7, kGroupStone},
		{4, 8, 9, 13, 12, kGroupStone}, {4, 9, 10, 14, 13, kGroupStone},
		{4, 10, 11, 15, 14, kGroupStone}, {4, 11, 8, 12, 15, kGroupStone},
		{4, 16, 17, 18, 19, kGroupStone}, {4, 20, 23, 22, 21, kGroupStone},
		{4, 16, 19, 23, 20, kGroupStone}, {4, 17, 21, 22, 18, kGroupStone},
		{4, 19, 18, 22, 23, kGroupStone}, {4, 16, 20, 21, 17, kGroupStone},
	};
	add_faces(mesh, groups, count, f, static_cast<u16>(sizeof(f) / sizeof(f[0])));
}

/// Edificio de 2 alturas.
void build_building(MeshData& mesh, u8* groups, u8& count) noexcept {
	const int v[12][3] = {
		{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10},
		{-10, 20, -10}, {10, 20, -10}, {10, 20, 10}, {-10, 20, 10},
		{-8, 32, -8}, {8, 32, -8}, {8, 32, 8}, {-8, 32, 8},
	};
	for (const auto& p : v) add_v(mesh, p[0], p[1], p[2], 4, 3, 4, 2, 2, 2);

	const FaceDef f[] = {
		{4, 0, 1, 5, 4, kGroupBuilding}, {4, 1, 2, 6, 5, kGroupBuilding},
		{4, 2, 3, 7, 6, kGroupBuilding}, {4, 3, 0, 4, 7, kGroupBuilding},
		{4, 4, 5, 9, 8, kGroupBuilding}, {4, 5, 6, 10, 9, kGroupBuilding},
		{4, 6, 7, 11, 10, kGroupBuilding}, {4, 7, 4, 8, 11, kGroupBuilding},
		{4, 8, 9, 10, 11, kGroupBuilding},
	};
	add_faces(mesh, groups, count, f, static_cast<u16>(sizeof(f) / sizeof(f[0])));
}

/// Torre de radar con mastil y plato.
void build_radar(MeshData& mesh, u8* groups, u8& count) noexcept {
	const int v[13][3] = {
		{-2, 0, -2}, {2, 0, -2}, {2, 0, 2}, {-2, 0, 2},
		{-2, 40, -2}, {2, 40, -2}, {2, 40, 2}, {-2, 40, 2},
		{-8, 38, -8}, {8, 38, -8}, {8, 38, 8}, {-8, 38, 8},
		{0, 46, 0},
	};
	for (const auto& p : v) add_v(mesh, p[0], p[1], p[2], 4, 3, 4, 2, 2, 2);

	const FaceDef f[] = {
		{4, 0, 1, 5, 4, kGroupMetal}, {4, 1, 2, 6, 5, kGroupMetal},
		{4, 2, 3, 7, 6, kGroupMetal}, {4, 3, 0, 4, 7, kGroupMetal},
		{4, 8, 9, 10, 11, kGroupStone},
		{3, 8, 9, 12, 0, kGroupStone}, {3, 9, 10, 12, 0, kGroupStone},
		{3, 10, 11, 12, 0, kGroupStone}, {3, 11, 8, 12, 0, kGroupStone},
	};
	add_faces(mesh, groups, count, f, static_cast<u16>(sizeof(f) / sizeof(f[0])));
}

/// Acantilado: caja ancha y alta para los laterales del canon.
void build_cliff(MeshData& mesh, u8* groups, u8& count) noexcept {
	const int v[8][3] = {
		{-30, 0, -60}, {30, 0, -60}, {30, 0, 60}, {-30, 0, 60},
		{-30, 100, -60}, {30, 100, -60}, {30, 100, 60}, {-30, 100, 60},
	};
	for (const auto& p : v) add_v(mesh, p[0], p[1], p[2], 2, 2, 2);

	const FaceDef f[] = {
		{4, 0, 1, 5, 4, kGroupStone}, {4, 1, 2, 6, 5, kGroupStone},
		{4, 2, 3, 7, 6, kGroupStone}, {4, 3, 0, 4, 7, kGroupStone},
		{4, 4, 5, 6, 7, kGroupStone},
	};
	add_faces(mesh, groups, count, f, static_cast<u16>(sizeof(f) / sizeof(f[0])));
}

/// Roca baja de primer plano (5 vertices, 4 caras).
void build_rock(MeshData& mesh, u8* groups, u8& count) noexcept {
	const int v[5][3] = {
		{-12, 0, -8}, {12, 0, -8}, {10, 0, 8}, {-10, 0, 8}, {0, 14, 0},
	};
	for (const auto& p : v) add_v(mesh, p[0], p[1], p[2], 2, 2, 2);

	const FaceDef f[] = {
		{3, 0, 1, 4, 0, kGroupStone}, {3, 1, 2, 4, 0, kGroupStone},
		{3, 2, 3, 4, 0, kGroupStone}, {3, 3, 0, 4, 0, kGroupStone},
	};
	add_faces(mesh, groups, count, f, static_cast<u16>(sizeof(f) / sizeof(f[0])));
}

// --- Estado ------------------------------------------------------------------
enum class Kind : u8 { Arch = 0u, Building = 1u, Radar = 2u, Cliff = 3u,
		       Rock = 4u, Count = 5u };

struct Enemy {
	bool active = false;
	s16 x0 = 0;
	s16 y0 = 0;
	s16 z = 0;
	s16 side = 1;
	s16 phase = 0;
	math3d::Vec3t<eng::coord> position = math3d::vec3(0, 40, 400);
	math3d::Mat3<> rot = math3d::Mat3<>::identity();
};

struct Scenery {
	ModelHandle model {};
	s16 x = 0;
	s16 y = kGroundY;
	s16 z = 0;
	s16 speed = 7;
	u8 kind = 0;
};

struct Bolt {
	bool active = false;
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;
	s16 dz = 0;
	u8 color = kColorPlayerBolt;
};

struct RenderItem {
	ModelHandle model {};
	math3d::Vec3t<eng::coord> position {};
	math3d::Mat3<> rotation {};
	u8 id = 0;
	bool allow_planes = false;
};

struct FaceDraw {
	u8 face = 0;
	s32 z = 0;
	u8 color = 0;
};

struct ObjHist {
	u32 cpu_cycles = 0;
	u32 planes_cycles = 0;
	u32 area = 0;
	u8 phase = 0; // 0=muestra CPU, 1=muestra planos, 2=decidido
	u8 route = 0; // 0=CPU, 1=planos
};

struct StarFoxTravel {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext& context) { // NOLINT
		if (!backend.configure_memory({384u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		comp::SceneResources res = comp::planar(kWidth, kHeight, kTotalPlanes);
		res.buffers = kBuffers;
		if (!comp::compose(m_display, backend.memory_manager(), res, comp::ocs_a500,
				   comp::display(res), comp::palette(kPalette, 0u, 16u))) {
			eng::debug::mark_failed(
				g_eng_run_status,
				kBootCompose + static_cast<u32>(m_display.config_error().code));
			return;
		}
		m_display.takeover(backend);
		m_canvas.set_rasterizer(&playfield::kBlitterRaster);

		m_view_x = 0;
		m_view_y = 0;
		m_cam_roll_q12 = 0;
		for (u8 b = 0u; b < m_display.buffer_count(); ++b) {
			const eng::PlaneBytes buffer = m_display.buffer(b);
			bind_planes(buffer);
			// El plano 3 no se renderiza: queda a 0. Solo foreground 3D.
			draw_hud_band();
			backend.wait_blitter();
		}

		if (!build_models()) {
			eng::debug::mark_failed(g_eng_run_status, kBootModels);
			return;
		}
		init_state();
		m_ready = true;
		update(backend, context);
		update(backend, context);
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) { // NOLINT
		if (!m_ready) return;
		m_display.commit();
		const eng::PlaneBytes buffer = m_display.back();
		const eng::PlaneBytes fg {
			buffer.data(), kFgPlanes * kPlaneBytes};
		++m_frame;

		update_player();
		update_enemies();
		update_scenery();
		update_view();

#if !K099_SKIP_SOLIDS
		// Solo se limpian los 3 planos de foreground; el plano 3 (fondo
		// estatico) y las filas del HUD no se tocan.
		(void)backend.blitter_clear(fg, kFgPlanes, kRowBytes,
					    kPlaneBytes, kWidth, kHudY);
		draw_solids(backend, fg);
		draw_engine_glow(fg);
#endif
#if !K099_SKIP_BOLTS
		update_bolts();
		draw_bolts(fg);
#endif

		if ((m_frame & 15u) == 0u) {
			eng::util::TextLine<48> stats {};
			u16 enemies = 0u;
			u16 bolts = 0u;
			for (u16 i = 0u; i < kEnemies; ++i) {
				if (m_enemies[i].active) ++enemies;
			}
			for (u16 i = 0u; i < kBoltSlots; ++i) {
				if (m_bolts[i].active) ++bolts;
			}
			stats << 'E' << enemies << " B" << bolts;
			m_host_hud.draw(backend.debug(), "099 STARFOX TRAVEL", kVersion,
					context.frame.frame_index, eng::debug::prof_clock(),
					stats.c_str());
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	void bind_planes(eng::PlaneBytes planes) noexcept {
		(void)m_canvas.bind_raw(planes.data(), kTotalPlanes * kPlaneBytes,
				   kWidth, kHeight, kTotalPlanes, kPlaneBytes);
	}


	/// Balanceo de camara: roll alrededor del centro y offsets de yaw/pitch.
	void apply_camera(s16& sx, s16& sy) const noexcept {
		const s32 dx = static_cast<s32>(sx) - kCx;
		const s32 dy = static_cast<s32>(sy) - kCy;
		const s32 rx = (dx * m_roll_cos - dy * m_roll_sin) >> 12;
		const s32 ry = (dx * m_roll_sin + dy * m_roll_cos) >> 12;
		sx = static_cast<s16>(kCx + rx + m_view_x);
		sy = static_cast<s16>(kCy + ry + m_view_y);
	}

	[[nodiscard]] bool project_point(s16 x, s16 y, s16 z, s16& sx,
					 s16& sy) const noexcept {
		if (z <= kNearZ) return false;
		const eng::graphics::CamVertex<eng::coord> v {
			CT::from_int(x), CT::from_int(y), CT::from_int(z)};
		s16 px = 0;
		s16 py = 0;
		if (!eng::graphics::project_camera_vertex(v, kCamera, px, py)) return false;
		apply_camera(px, py);
		sx = px;
		sy = py;
		return true;
	}

	[[nodiscard]] bool project_mesh(const RenderItem& item) noexcept {
		const eng::Ref<const scene3d::Mesh> mesh_ref = m_models.mesh(item.model);
		if (!mesh_ref.valid()) return false;
		const scene3d::Mesh& mesh = *mesh_ref;
		const u32 nv = mesh.vertex_count();
		if (nv < 3u || nv > kMaxVerts) return false;
		const math3d::Affine3<> model {item.rotation, item.position};
		math3d::mesh_transform(
			mesh.vertices, model,
			eng::Span<math3d::Vec3t<eng::coord>>(m_world, nv));
		for (u32 i = 0u; i < nv; ++i) {
			const eng::graphics::CamVertex<eng::coord> v {
				m_world[i].v[0], m_world[i].v[1], m_world[i].v[2]};
			s16 px = 0;
			s16 py = 0;
			if (!eng::graphics::project_camera_vertex(v, kCamera, px, py))
				return false;
			apply_camera(px, py);
			m_sx[i] = px;
			m_sy[i] = py;
		}
		return true;
	}

	/// Borra un span de un plano. En Amiga usa palabras completas.
	void clear_span(u8* plane, u16 row_bytes, s32 y, s32 xl, s32 xr) noexcept {
		u8* row = plane + static_cast<u32>(y) * row_bytes;
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		const u32 w0 = static_cast<u32>(xl) >> 4u;
		const u32 w1 = static_cast<u32>(xr) >> 4u;
		const u16 m0 = static_cast<u16>(0xffffu >> (static_cast<u32>(xl) & 15u));
		const u16 m1 = static_cast<u16>(0xffffu << (15u - (static_cast<u32>(xr) & 15u)));
		u16* words = reinterpret_cast<u16*>(row);
		if (w0 == w1) {
			words[w0] = static_cast<u16>(words[w0] & ~(m0 & m1));
			return;
		}
		words[w0] = static_cast<u16>(words[w0] & ~m0);
		for (u32 i = w0 + 1u; i < w1; ++i) words[i] = 0u;
		words[w1] = static_cast<u16>(words[w1] & ~m1);
#else
		const u32 b0 = static_cast<u32>(xl) >> 3u;
		const u32 b1 = static_cast<u32>(xr) >> 3u;
		const u8 m0 = static_cast<u8>(0xffu >> (static_cast<u32>(xl) & 7u));
		const u8 m1 = static_cast<u8>(0xffu << (7u - (static_cast<u32>(xr) & 7u)));
		if (b0 == b1) {
			row[b0] = static_cast<u8>(row[b0] & ~(m0 & m1));
			return;
		}
		row[b0] = static_cast<u8>(row[b0] & ~m0);
		for (u32 i = b0 + 1u; i < b1; ++i) row[i] = 0u;
		row[b1] = static_cast<u8>(row[b1] & ~m1);
#endif
	}

	[[nodiscard]] bool register_model(ModelHandle handle, const u8* groups,
					  u8 face_count, bool orient_centroid) noexcept {
		if (!handle.valid() || handle.index >= 16u || face_count > kMaxFaces)
			return false;
		const eng::Ref<const scene3d::Mesh> mesh_ref = m_models.mesh(handle);
		if (!mesh_ref.valid()) return false;
		const scene3d::Mesh& mesh = *mesh_ref;
		if (mesh.face_count() != face_count) return false;
		s32 cen_x = 0;
		s32 cen_y = 0;
		s32 cen_z = 0;
		for (u32 i = 0u; i < mesh.vertex_count(); ++i) {
			cen_x += CT::to_int(mesh.vertices[i].v[0]);
			cen_y += CT::to_int(mesh.vertices[i].v[1]);
			cen_z += CT::to_int(mesh.vertices[i].v[2]);
		}
		const s32 cen_n = static_cast<s32>(mesh.vertex_count());
		cen_x /= cen_n;
		cen_y /= cen_n;
		cen_z /= cen_n;
		m_face_count[handle.index] = face_count;
		for (u8 f = 0u; f < face_count; ++f) {
			m_group[handle.index][f] = groups[f];
			const eng::Span<const u16> ids = mesh.face_indices(f);
			if (ids.size() < 3u || ids.size() > 4u) return false;
			const auto& va = mesh.vertices[ids[0]];
			const auto& vb = mesh.vertices[ids[1]];
			const auto& vc = mesh.vertices[ids[2]];
			const s32 ax = CT::to_int(va.v[0]);
			const s32 ay = CT::to_int(va.v[1]);
			const s32 az = CT::to_int(va.v[2]);
			const s32 ux = CT::to_int(vb.v[0]) - ax;
			const s32 uy = CT::to_int(vb.v[1]) - ay;
			const s32 uz = CT::to_int(vb.v[2]) - az;
			const s32 vx = CT::to_int(vc.v[0]) - ax;
			const s32 vy = CT::to_int(vc.v[1]) - ay;
			const s32 vz = CT::to_int(vc.v[2]) - az;
			s32 nx = uy * vz - uz * vy;
			s32 ny = uz * vx - ux * vz;
			s32 nz = ux * vy - uy * vx;
			const u32 l2 = static_cast<u32>(nx * nx + ny * ny + nz * nz);
			if (l2 == 0u) return false;
			const u32 len = eng::isqrt(l2);
			s32 qx = (nx * 4096) / static_cast<s32>(len);
			s32 qy = (ny * 4096) / static_cast<s32>(len);
			s32 qz = (nz * 4096) / static_cast<s32>(len);
			if (orient_centroid) {
				s32 fcx = 0;
				s32 fcy = 0;
				s32 fcz = 0;
				for (u32 k = 0u; k < ids.size(); ++k) {
					fcx += CT::to_int(mesh.vertices[ids[k]].v[0]);
					fcy += CT::to_int(mesh.vertices[ids[k]].v[1]);
					fcz += CT::to_int(mesh.vertices[ids[k]].v[2]);
				}
				fcx /= static_cast<s32>(ids.size());
				fcy /= static_cast<s32>(ids.size());
				fcz /= static_cast<s32>(ids.size());
				if (qx * (fcx - cen_x) + qy * (fcy - cen_y) +
					    qz * (fcz - cen_z) < 0) {
					qx = -qx;
					qy = -qy;
					qz = -qz;
				}
			}
			m_normals[handle.index][f] = math3d::Vec3t<eng::real> {
				{eng::real {static_cast<s16>(qx)},
				 eng::real {static_cast<s16>(qy)},
				 eng::real {static_cast<s16>(qz)}}};
		}
		return true;
	}

	[[nodiscard]] bool make_model(MeshData& mesh, const u8* groups, u8 count,
				      ModelHandle& out, bool orient_centroid) noexcept {
		const ModelHandle h = m_models.add_mesh(mesh.view(), 1u);
		if (!h.valid() || !register_model(h, groups, count, orient_centroid))
			return false;
		out = h;
		return true;
	}

	[[nodiscard]] bool build_models() noexcept {
		MeshData mesh {};
		u8 groups[kMaxFaces] {};
		u8 n = 0u;
		build_arwing(mesh, groups, n);
		if (!make_model(mesh, groups, n, m_player_model, true)) return false;
		mesh = MeshData {};
		n = 0u;
		build_enemy(mesh, groups, n);
		if (!make_model(mesh, groups, n, m_enemy_model, true)) return false;
		for (u8 k = 0u; k < static_cast<u8>(Kind::Count); ++k) {
			mesh = MeshData {};
			n = 0u;
			switch (static_cast<Kind>(k)) {
			case Kind::Arch: build_arch(mesh, groups, n); break;
			case Kind::Building: build_building(mesh, groups, n); break;
			case Kind::Radar: build_radar(mesh, groups, n); break;
			case Kind::Cliff: build_cliff(mesh, groups, n); break;
			case Kind::Rock: build_rock(mesh, groups, n); break;
			default: break;
			}
			if (!make_model(mesh, groups, n, m_kind_model[k], false))
				return false;
		}
		return true;
	}

	[[nodiscard]] static Kind kind_for_slot(u16 slot) noexcept {
		if (slot == 0u || slot == 3u) return Kind::Arch;
		if (slot == 4u) return Kind::Rock;
		return Kind::Building;
	}

	[[nodiscard]] static s16 scenery_z(u16 slot) noexcept {
		switch (slot) {
		case 0u: return 650;
		case 1u: return 750;
		case 2u: return 850;
		case 3u: return 1000;
		default: return 550;
		}
	}

	void init_state() noexcept {
		m_rng.state = 0x57a3f01dU;
		m_player_pos = math3d::vec3(0, kPlayerY, kPlayerZ);
		m_player_rot = math3d::Mat3<>::identity();
		for (u16 i = 0u; i < kEnemies; ++i) {
			reset_enemy(m_enemies[i], i);
			m_enemies[i].active = true;
		}
		for (u16 i = 0u; i < kScenery; ++i) {
			m_scenery[i].y = kGroundY;
			m_scenery[i].speed = 7;
			m_scenery[i].z = scenery_z(i);
			m_scenery[i].kind = static_cast<u8>(kind_for_slot(i));
			m_scenery[i].model = m_kind_model[m_scenery[i].kind];
			m_scenery[i].x = scenery_x(m_scenery[i].kind, i);
		}
		for (u16 i = 0u; i < kBoltSlots; ++i) m_bolts[i] = {};
	}

	[[nodiscard]] s16 scenery_x(u8 kind, u16 slot) noexcept {
		const bool odd = (slot & 1u) != 0u;
		switch (static_cast<Kind>(kind)) {
		case Kind::Arch: return static_cast<s16>(odd ? -16 : 16);
		case Kind::Building: return static_cast<s16>(odd ? -90 : 90);
		case Kind::Radar: return static_cast<s16>(odd ? -65 : 65);
		case Kind::Cliff: return static_cast<s16>(odd ? -150 : 150);
		case Kind::Rock: return static_cast<s16>(odd ? -35 : 35);
		default: return 0;
		}
	}

	void reset_enemy(Enemy& e, u16 slot) noexcept {
		e.side = (slot & 1u) == 0u ? static_cast<s16>(-1) : static_cast<s16>(1);
		e.x0 = static_cast<s16>(e.side * (34 + static_cast<s16>(slot) * 9));
		e.y0 = static_cast<s16>(kPlayerY + (slot & 1u) * 10);
		e.z = static_cast<s16>(420 + static_cast<s16>(slot) * 80);
		e.phase = static_cast<s16>((slot * 1024u + (m_rng.next() & 0x03ffu)) & 4095u);
		e.position = math3d::vec3(e.x0, e.y0, e.z);
		e.rot = math3d::Mat3<>::identity();
		e.active = true;
	}

	void update_player() noexcept {
#ifdef K099_DEBUG_ARWING
		m_player_pos = math3d::vec3(0, 35, 150);
		math3d::load_rotate(m_player_rot, retro::turns(120),
				    retro::turns(341), retro::turns(0));
		return;
#endif
		const s32 sin = retro::kSinQ12[m_frame & 4095u];
		const s32 cos = retro::kSinQ12[(m_frame + 1024u) & 4095u];
		const s32 zig = retro::kSinQ12[(m_frame * 2u + 512u) & 4095u];
		m_player_pos = math3d::vec3(
			static_cast<s16>(((sin * 24) + (zig * 12)) >> 12),
			static_cast<s16>(kPlayerY + ((cos * 5) >> 12)), kPlayerZ);
		const s16 roll = static_cast<s16>((sin * 300) >> 12);
		const s16 pitch = static_cast<s16>((cos * 120) >> 12);
		const s16 yaw = static_cast<s16>((sin * 80) >> 12);
		math3d::load_rotate(m_player_rot,
				    retro::turns(static_cast<u16>(pitch) & 4095u),
				    retro::turns(static_cast<u16>(yaw) & 4095u),
				    retro::turns(static_cast<u16>(roll) & 4095u));
	}

	void update_enemies() noexcept {
		for (u16 i = 0u; i < kEnemies; ++i) {
			Enemy& e = m_enemies[i];
			e.z = static_cast<s16>(e.z - kEnemySpeed);
			const s16 drift = e.z < 330 ? static_cast<s16>((330 - e.z) * 3 / 4) : 0;
			const u16 ang = static_cast<u16>((m_frame * 3u +
							  static_cast<u16>(e.phase)) & 4095u);
			const s32 weave = retro::kSinQ12[ang];
			const s32 weave_cos = retro::kSinQ12[(ang + 1024u) & 4095u];
			const s16 x = static_cast<s16>(e.x0 + ((weave * 26) >> 12) + e.side * drift);
			const s16 y = static_cast<s16>(e.y0 + ((weave_cos * 14) >> 12));
			if (e.z < kEnemyResetZ || x > 280 || x < -280) {
				reset_enemy(e, i);
				continue;
			}
			e.position = math3d::vec3(x, y, e.z);
			const s16 bank = static_cast<s16>(e.side * drift * 3);
			const s16 yaw = static_cast<s16>(2048 + ((weave * 300) >> 12));
			const s16 pitch = static_cast<s16>((weave_cos * 220) >> 12);
			math3d::load_rotate(e.rot,
					    retro::turns(static_cast<u16>(pitch) & 4095u),
					    retro::turns(static_cast<u16>(yaw) & 4095u),
					    retro::turns(static_cast<u16>(bank) & 4095u));
		}
	}

	void update_scenery() noexcept {
		for (u16 i = 0u; i < kScenery; ++i) {
			Scenery& s = m_scenery[i];
			s.z = static_cast<s16>(s.z - s.speed);
			if (s.z < kSceneryResetZ) {
				s.z = scenery_z(i);
				s.y = kGroundY;
				s.kind = static_cast<u8>(kind_for_slot(i));
				s.model = m_kind_model[s.kind];
				s.x = scenery_x(s.kind, i);
				s.speed = static_cast<s16>(6 + (i & 1u));
			}
		}
	}

	void update_view() noexcept {
		const s32 yaw_a = retro::kSinQ12[(m_frame * 2u) & 4095u];
		const s32 yaw_b = retro::kSinQ12[(m_frame * 5u + 700u) & 4095u];
		m_view_x = static_cast<s16>(((yaw_a * 18) + (yaw_b * 6)) >> 12);
		const s32 pitch = retro::kSinQ12[(m_frame * 3u + 512u) & 4095u];
		m_view_y = static_cast<s16>((pitch * 12) >> 12);
		const s32 roll = retro::kSinQ12[(m_frame * 2u + 300u) & 4095u];
		const s16 cam_roll = static_cast<s16>((roll * 90) >> 12);
		m_cam_roll_q12 = cam_roll;
		m_roll_sin = retro::kSinQ12[static_cast<u16>(cam_roll) & 4095u];
		m_roll_cos = retro::kSinQ12[(static_cast<u16>(cam_roll) + 1024u) & 4095u];
	}

	void draw_hud_band() noexcept {
		eng::playfield::Surface hud {m_canvas, eng::playfield::SurfaceRect {
			0, static_cast<s16>(kHudY), kWidth, static_cast<s16>(kHudH)}};
		(void)hud.fill_rect(0, kHudY, kWidth, kHudH, 0u);
		(void)hud.draw_text_words(16, static_cast<s32>(kHudY) + 8,
					  "099 STARFOX TRAVEL", kColorHudTitle);
		(void)hud.draw_text_words(16, static_cast<s32>(kHudY) + 24,
					  "RAIL RUN  CORNERIA  NO INPUT", kColorHudSubtitle);
	}

	void fill_faces_cpu(const eng::graphics::PlanePolygon* faces, u32 n,
			    eng::PlaneBytes fg) noexcept {
		s32 vx[16] {};
		s32 vy[16] {};
		for (u32 f = 0u; f < n; ++f) {
			const eng::graphics::PlanePolygon& face = faces[f];
			if (face.count < 3u || face.count > 16u) continue;
			for (u8 k = 0u; k < face.count; ++k) {
				vx[k] = face.xs[k];
				vy[k] = face.ys[k];
			}
			eng::math3d::convex_spans(
				eng::Span<const s32>(vx, face.count),
				eng::Span<const s32>(vy, face.count),
				[&](s32 y, s32 xl, s32 xr) {
					if (y < 0 || y >= static_cast<s32>(kHudY)) return;
					if (xl < 0) xl = 0;
					if (xr >= static_cast<s32>(kWidth))
						xr = static_cast<s32>(kWidth) - 1;
					if (xl > xr) return;
					u8* const planes[3] = {
						fg.data(), fg.data() + kPlaneBytes,
						fg.data() + 2u * kPlaneBytes};
					for (u8 p = 0u; p < kFgPlanes; ++p) {
						if ((face.color & (1u << p)) != 0u) {
							eng::graphics::detail::plane_or_span(
								planes[p], kRowBytes, y, xl, xr,
								0xffffu);
						} else {
							clear_span(planes[p], kRowBytes, y,
								   xl, xr);
						}
					}
				});
		}
	}

	[[nodiscard]] u8 choose_route(const RenderItem& it, u32 area) noexcept {
#if K099_FILL_MODE == 0
		(void)it;
		(void)area;
		return kRouteCpu;
#elif K099_FILL_MODE == 1
		(void)it;
		(void)area;
		return kRoutePlanes;
#else
		if (!it.allow_planes || it.id >= kMaxItems) return kRouteCpu;
		ObjHist& h = m_hist[it.id];
		if (h.phase == 2u && h.area != 0u &&
		    (area > h.area + h.area / 4u || area + area / 4u < h.area)) {
			h.phase = 0u;
			h.cpu_cycles = 0u;
			h.planes_cycles = 0u;
		}
		h.area = area;
		if (h.phase == 0u) return kRouteCpu;
		if (h.phase == 1u) return kRoutePlanes;
		return h.route;
#endif
	}

	void record_route(u8 id, u8 route, u32 cycles) noexcept {
#if K099_FILL_MODE == 2
		if (id >= kMaxItems) return;
		ObjHist& h = m_hist[id];
		if (route == kRouteCpu) {
			h.cpu_cycles = cycles;
		} else {
			h.planes_cycles = cycles;
		}
		if (h.phase == 0u) {
			h.phase = 1u;
		} else if (h.phase == 1u) {
			h.phase = 2u;
			h.route = (h.cpu_cycles != 0u && h.planes_cycles != 0u &&
				   h.planes_cycles * 100u < h.cpu_cycles * 90u)
					  ? kRoutePlanes
					  : kRouteCpu;
		}
#else
		(void)id;
		(void)route;
		(void)cycles;
#endif
	}

	void draw_solids(eng::amiga::AmigaBackend& backend,
			 eng::PlaneBytes fg) noexcept {
		RenderItem items[kMaxItems] {};
		u16 count = 0u;
		items[count++] = RenderItem {m_player_model, m_player_pos, m_player_rot,
					    0u, false};
#if !defined(K099_DEBUG_ARWING)
		for (u16 i = 0u; i < kEnemies; ++i) {
			if (!m_enemies[i].active) continue;
			items[count++] = RenderItem {m_enemy_model, m_enemies[i].position,
						     m_enemies[i].rot,
						     static_cast<u8>(1u + i), false};
		}
#endif
		for (u16 i = 0u; i < kScenery; ++i) {
			items[count++] = RenderItem {m_scenery[i].model,
						     math3d::vec3(m_scenery[i].x, m_scenery[i].y,
								  m_scenery[i].z),
						     math3d::Mat3<>::identity(),
						     static_cast<u8>(1u + kEnemies + i),
						     m_scenery[i].kind ==
							     static_cast<u8>(Kind::Building)};
		}
		u16 order[kMaxItems] {};
		for (u16 i = 0u; i < count; ++i) order[i] = i;
		for (u16 i = 1u; i < count; ++i) {
			const u16 key = order[i];
			const s32 z = CT::to_int(items[key].position.v[2]);
			u16 j = i;
			while (j > 0u && CT::to_int(items[order[j - 1u]].position.v[2]) < z) {
				order[j] = order[j - 1u];
				--j;
			}
			order[j] = key;
		}

		for (u16 i = 0u; i < count; ++i) {
			const RenderItem& it = items[order[i]];
			const u8 mi = static_cast<u8>(it.model.index);
			if (mi >= 16u || m_face_count[mi] == 0u) continue;
			if (!project_mesh(it)) continue;
			const s32 lx = static_cast<s32>(kSunX) - CT::to_int(it.position.v[0]);
			const s32 ly = static_cast<s32>(kSunY) - CT::to_int(it.position.v[1]);
			const s32 lz = static_cast<s32>(kSunZ) - CT::to_int(it.position.v[2]);
			const u32 l2 = static_cast<u32>(lx * lx + ly * ly + lz * lz);
			if (l2 == 0u) continue;
			const u32 llen = eng::isqrt(l2);
			if (llen == 0u) continue;
			const s16 lqx = static_cast<s16>((lx * 4096) / static_cast<s32>(llen));
			const s16 lqy = static_cast<s16>((ly * 4096) / static_cast<s32>(llen));
			const s16 lqz = static_cast<s16>((lz * 4096) / static_cast<s32>(llen));

			const eng::Ref<const scene3d::Mesh> mesh_ref = m_models.mesh(it.model);
			if (!mesh_ref.valid()) continue;
			const scene3d::Mesh& mesh = *mesh_ref;
			FaceDraw faces[kMaxFaces] {};
			u8 nf = 0u;
			for (u8 f = 0u; f < m_face_count[mi]; ++f) {
				const eng::Span<const u16> ids = mesh.face_indices(f);
				if (ids.size() < 3u) continue;
				const u16 ia = ids[0];
				const u16 ib = ids[1];
				const u16 ic = ids[2];
				math3d::Vec3t<eng::real> wn {};
				const math3d::Vec3t<eng::real>& ln = m_normals[mi][f];
				math3d::transform(it.rotation, &wn, &ln, 1u);
				const s32 facing =
					wn.v[0].v * CT::to_int(m_world[ia].v[0]) +
					wn.v[1].v * CT::to_int(m_world[ia].v[1]) +
					wn.v[2].v * CT::to_int(m_world[ia].v[2]);
				if (facing >= 0) continue;
				const s32 ndl = (wn.v[0].v * lqx + wn.v[1].v * lqy +
						 wn.v[2].v * lqz) >> 12;
				const u8 level = ndl >= 2600 ? 2u : (ndl >= 1000 ? 1u : 0u);
				const u8 group = m_group[mi][f];
				const u8 color = kShade[group & 7u][level];
				const s32 z = CT::to_int(m_world[ia].v[2]) +
					      CT::to_int(m_world[ib].v[2]) +
					      CT::to_int(m_world[ic].v[2]);
				faces[nf++] = FaceDraw {f, z, color};
			}
			for (u8 a = 1u; a < nf; ++a) {
				const FaceDraw key = faces[a];
				u8 b = a;
				while (b > 0u && faces[b - 1u].z < key.z) {
					faces[b] = faces[b - 1u];
					--b;
				}
				faces[b] = key;
			}
			u8 np = 0u;
			s16 bx0 = 32767;
			s16 by0 = 32767;
			s16 bx1 = -32768;
			s16 by1 = -32768;
			for (u8 a = 0u; a < nf && np < kMaxFaces; ++a) {
				const eng::Span<const u16> ids =
					mesh.face_indices(faces[a].face);
				const u16 fn = static_cast<u16>(ids.size());
				if (fn < 3u || fn > 4u) continue;
				s16* const gx = m_draw_gx[np];
				s16* const gy = m_draw_gy[np];
				for (u16 k = 0u; k < fn; ++k) {
					gx[k] = m_sx[ids[k]];
					gy[k] = m_sy[ids[k]];
					if (gx[k] < bx0) bx0 = gx[k];
					if (gx[k] > bx1) bx1 = gx[k];
					if (gy[k] < by0) by0 = gy[k];
					if (gy[k] > by1) by1 = gy[k];
				}
				m_draw_polys[np] = eng::graphics::PlanePolygon {
					gx, gy, static_cast<u8>(fn), faces[a].color};
				++np;
			}
			if (np == 0u) continue;
			if (bx0 < 0) bx0 = 0;
			if (by0 < 0) by0 = 0;
			if (bx1 >= static_cast<s16>(kWidth))
				bx1 = static_cast<s16>(kWidth - 1u);
			if (by1 >= static_cast<s16>(kHudY))
				by1 = static_cast<s16>(kHudY - 1u);
			if (bx1 < bx0 || by1 < by0) continue;
			const u32 area = static_cast<u32>(bx1 - bx0 + 1) *
					 static_cast<u32>(by1 - by0 + 1);
			const u8 route = choose_route(it, area);
			const u32 t0 = eng::debug::prof_clock();
			if (route == kRouteCpu) {
				fill_faces_cpu(m_draw_polys, np, fg);
			} else {
				(void)backend.fill_polygons_by_plane_dirty(
					m_draw_polys, np, fg, kRowBytes, kPlaneBytes,
					kFgPlanes, kWidth, kHudY,
					static_cast<u16>(bx0), static_cast<u16>(by0),
					static_cast<u16>(bx1), static_cast<u16>(by1));
			}
			record_route(it.id, route,
				     eng::debug::prof_clock() - t0);
		}
	}
	void draw_engine_glow(eng::PlaneBytes planes) noexcept {
		const math3d::Affine3<> model {m_player_rot, m_player_pos};
		const math3d::Vec3t<eng::coord> local = math3d::vec3(0, 0, -10);
		math3d::Vec3t<eng::coord> exhaust {};
		math3d::transform(model, &exhaust, &local, 1u);
		s16 sx = 0;
		s16 sy = 0;
		if (!project_point(CT::to_int(exhaust.v[0]), CT::to_int(exhaust.v[1]),
				   CT::to_int(exhaust.v[2]), sx, sy)) return;
		if (sx < 0 || sx >= static_cast<s16>(kWidth)) return;
		u8* const p[3] = {planes.data(), planes.data() + kPlaneBytes,
				  planes.data() + 2u * kPlaneBytes};
		for (s16 dy = -2; dy <= 3; ++dy) {
			const s32 y = static_cast<s32>(sy) + dy;
			if (y < 0 || y > static_cast<s32>(kFlightBottom)) continue;
			for (u8 q = 0u; q < kFgPlanes; ++q) {
				if ((kColorEngine & (1u << q)) != 0u) {
					eng::graphics::detail::plane_or_span(p[q], kRowBytes, y,
									     sx, sx, 0xffffu);
				} else {
					clear_span(p[q], kRowBytes, y, sx, sx);
				}
			}
		}
	}

	void update_bolts() noexcept {
		if ((m_frame % 11u) == 0u) {
			for (u16 i = 0u; i < kBoltSlots; ++i) {
				Bolt& b = m_bolts[i];
				if (b.active) continue;
				b.active = true;
				b.dz = 24;
				b.color = kColorPlayerBolt;
				b.z = static_cast<s16>(kPlayerZ + 26);
				b.x = static_cast<s16>(((m_frame / 11u) & 1u) == 0u ? -14 : 14);
				b.y = 28;
				break;
			}
		}
		if ((m_frame % 53u) == 17u) {
			const Enemy* shooter = nullptr;
			for (u16 i = 0u; i < kEnemies; ++i) {
				if (!m_enemies[i].active) continue;
				if (shooter == nullptr ||
				    CT::to_int(m_enemies[i].position.v[2]) <
					    CT::to_int(shooter->position.v[2])) {
					shooter = &m_enemies[i];
				}
			}
			if (shooter != nullptr) {
				for (u16 i = 0u; i < kBoltSlots; ++i) {
					Bolt& b = m_bolts[i];
					if (b.active) continue;
					b.active = true;
					b.dz = -16;
					b.color = kColorEnemyBolt;
					b.x = CT::to_int(shooter->position.v[0]);
					b.y = CT::to_int(shooter->position.v[1]);
					b.z = static_cast<s16>(CT::to_int(shooter->position.v[2]) - 18);
					break;
				}
			}
		}
		for (u16 i = 0u; i < kBoltSlots; ++i) {
			Bolt& b = m_bolts[i];
			if (!b.active) continue;
			b.z = static_cast<s16>(b.z + b.dz);
			if (b.z > 700 || b.z < 70) b.active = false;
		}
	}

	void draw_bolts(eng::PlaneBytes planes) noexcept {
		for (u16 i = 0u; i < kBoltSlots; ++i) {
			const Bolt& b = m_bolts[i];
			if (!b.active) continue;
			s16 sx = 0;
			s16 sy = 0;
			if (!project_point(b.x, b.y, b.z, sx, sy)) continue;
			if (sx < 0 || sx >= static_cast<s16>(kWidth)) continue;
			const s16 dir = b.dz > 0 ? 1 : -1;
			u8* const p[3] = {planes.data(), planes.data() + kPlaneBytes,
					  planes.data() + 2u * kPlaneBytes};
			for (s16 d = 0; d <= 5; ++d) {
				const s32 y = static_cast<s32>(sy) + static_cast<s32>(dir) * d;
				if (y < 0 || y > static_cast<s32>(kFlightBottom)) continue;
				for (u8 q = 0u; q < kFgPlanes; ++q) {
					if ((b.color & (1u << q)) != 0u) {
						eng::graphics::detail::plane_or_span(
							p[q], kRowBytes, y, sx, sx, 0xffffu);
					} else {
						clear_span(p[q], kRowBytes, y, sx, sx);
					}
				}
			}
		}
	}

	// --- estado -------------------------------------------------------------
	bool m_ready = false;
	u16 m_frame = 0u;
	s16 m_view_x = 0;
	s16 m_view_y = 0;
	s32 m_roll_sin = 0;
	s32 m_roll_cos = 4096;
	s16 m_cam_roll_q12 = 0;
	comp::Scene m_display {};
	eng::playfield::ContiguousPlayfield m_canvas {};

	scene3d::ModelTable<16, kMaxVerts, kMaxIndices, kMaxFaces, 1> m_models {};
	ModelHandle m_player_model {};
	ModelHandle m_enemy_model {};
	ModelHandle m_kind_model[static_cast<u16>(Kind::Count)] {};
	u8 m_face_count[16] {};
	u8 m_group[16][kMaxFaces] {};
	math3d::Vec3t<eng::real> m_normals[16][kMaxFaces] {};

	math3d::Vec3t<eng::coord> m_player_pos = math3d::vec3(0, kPlayerY, kPlayerZ);
	math3d::Mat3<> m_player_rot {};

	Rng m_rng {};
	Enemy m_enemies[kEnemies] {};
	Scenery m_scenery[kScenery == 0u ? 1u : kScenery] {};
	Bolt m_bolts[kBoltSlots] {};
	eng::debug::HostHud m_host_hud {};

	s16 m_draw_gx[kMaxDrawFaces][4] {};
	s16 m_draw_gy[kMaxDrawFaces][4] {};
	eng::graphics::PlanePolygon m_draw_polys[kMaxDrawFaces] {};
	ObjHist m_hist[kMaxItems] {};

	math3d::Vec3t<eng::coord> m_world[kMaxVerts] {};
	s16 m_sx[kMaxVerts] {};
	s16 m_sy[kMaxVerts] {};
};

StarFoxTravel g_game {};

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
