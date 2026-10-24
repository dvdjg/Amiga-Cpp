// ============================================================================
// Demo 099 - Star Fox style rail travel
// ============================================================================
//
// Traveling no jugable inspirado en los niveles de espacio de Star Fox (SNES):
// una pareja de cazas se acerca por un rail fijo, el jugador dispara y los
// enemigos devuelven fuego mientras el campo de estrellas y las rocas de fondo
// dan sensacion de avance.
//
// Los modelos (caza y enemigo) se construyen en el codigo como solidos convexos
// de 4-5 vertices. El render es **silueta plana**: se proyectan los vertices, se
// calcula la envolvente convexa 2D y se rellena con spans de CPU directos
// (`plane_or_span`) sobre 2 planos. Es la ruta barata para objetos pequenos: el
// camino del engine por mascara + cookie-cut cuesta ~90k ciclos por objeto en
// esta escena, mientras que el span fill de una silueta pequena cuesta una
// fraccion. Con 3 colores (planos 0/1) la escena cabe en el presupuesto de
// 25 fps. Los asteroides y anillos dinamicos quedan como opcion de compilacion
// (`kAsteroids`/`kGates`); en el perfil por defecto son fondo estatico para no
// romper el presupuesto.
//
// Fondo: bandas, planeta, luna y rocas pintadas UNA vez por buffer en los planos
// 2/3; estrellas 3D y disparos se pintan por frame. La zona de vuelo (320x160)
// se limpia entera en los 2 planos dinamicos, de modo que no hay residuos entre
// frames ni entre los 3 buffers.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release

#include <eng/api/3d.hpp>
#include <eng/api/api.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/retro/lib2d.hpp>
#include <eng/retro/mesh_primitives.hpp>
#include <eng/retro/wireframe.hpp>

namespace {

namespace comp = eng::graphics::composition;
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
constexpr u8 kPlanes = 4u;
constexpr u8 kBuffers = 3u;

// Zona de vuelo: no tapa las bandas decorativas ni el HUD. Es el unico rect que
// se limpia y donde se recortan las primitivas dinamicas.
constexpr u16 kCorridorY = 16u;
constexpr u16 kCorridorH = 160u; // filas 16..175
constexpr u16 kCorridorBottom = kCorridorY + kCorridorH - 1u;
constexpr u16 kBandY = 208u;
constexpr u16 kBandH = kHeight - kBandY;

// Planos dinamicos: 0 y 1. El plano 2/3 queda para el fondo/HUD estatico.
constexpr u8 kDynamicPlanes = 2u;

// --- Camara ------------------------------------------------------------------
// `eng::coord` es el escalar q0: focal y semianchos de 320 px caben en s16.
// Espacio de camara: +x derecha, +y abajo, +z delante, camara en el origen.
constexpr s16 kFocal = 280;
constexpr s16 kCx = 160;
constexpr s16 kCy = 104;
constexpr s16 kNearZ = 48;
constexpr s16 kHalfW = 160;
constexpr s16 kHalfH = 104;

// --- Paleta ------------------------------------------------------------------
constexpr eng::Palette32 kPalette {{
	0x000, // 0 negro
	0x0df, // 1 cian: caza del jugador
	0xf24, // 2 rojo: enemigos
	0xfd0, // 3 amarillo: disparos y fuego del motor
	0x114, // 4 magenta oscuro: banda superior
	0x226, // 5 azul oscuro: bandas
	0x48f, // 6 azul: luna
	0x8cf, // 7 azul claro: luz de la luna
	0x031, // 8 verde oscuro: suelo
	0x363, // 9 verde: linea de horizonte
	0x05f, // 10 azul brillante
	0x0a3, // 11 verde azulado
	0x444, // 12 gris
	0xaaa, // 13 gris claro: rocas de primer plano
	0xfd0, // 14 amarillo claro: subtitulo HUD
	0xfff, // 15 blanco: titulo HUD
}};

constexpr u8 kColorEnemy = 2u;
constexpr u8 kColorWarm = 3u;
constexpr u8 kColorStarFar = 1u;
constexpr u8 kColorStarNear = 3u;
constexpr u8 kHudTitle = 15u;
constexpr u8 kHudSubtitle = 14u;

constexpr eng::debug::Version kVersion {1u, 0u};

// --- Limites de contenido ----------------------------------------------------
constexpr u16 kEnemies = 2u;
constexpr u16 kAsteroids = 0u;
constexpr u16 kGates = 0u;
constexpr u16 kStars = 24u;
constexpr u16 kBoltSlots = 8u;
constexpr u16 kMaxItems = 1u + kEnemies + kAsteroids; // jugador + resto

constexpr s16 kPlayerZ = 180;
constexpr s16 kEnemyResetZ = 200;
constexpr s16 kEnemySpeed = 5;
constexpr s16 kAsteroidResetZ = 210;
constexpr s16 kGateResetZ = 300;

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

constexpr retro::Rect kCorridorClip = retro::rect(0, static_cast<s16>(kCorridorY), kWidth,
						  static_cast<s16>(kCorridorY + kCorridorH));

// Ablaciones de rendimiento para la validacion (no afectan a la demo normal).
// `K099_ABL` se elige con `EXTRA_DEFINES=-DK099_ABL=<modo>`:
//   0 normal | 1 solo bucle | 2 solo solidos | 3 solo clear | 4 solo anillo
//   5 solo HUD | 6 todo menos solidos | 7 todo menos clear | 8 todo menos anillo
//   9 todo menos HUD
#ifndef K099_ABL
#define K099_ABL 0
#endif
#ifndef K099_SKIP_FILL
#define K099_SKIP_FILL 0
#endif

#define K099_SKIP_SOLIDS 1
#define K099_SKIP_GATES 1
#define K099_SKIP_STARS 1
#define K099_SKIP_BOLTS 1
#define K099_SKIP_CLEAR 1
#define K099_SKIP_HUD 1

#if K099_ABL == 0
#undef K099_SKIP_SOLIDS
#undef K099_SKIP_GATES
#undef K099_SKIP_STARS
#undef K099_SKIP_BOLTS
#undef K099_SKIP_CLEAR
#undef K099_SKIP_HUD
#define K099_SKIP_SOLIDS 0
#define K099_SKIP_GATES 0
#define K099_SKIP_STARS 0
#define K099_SKIP_BOLTS 0
#define K099_SKIP_CLEAR 0
#define K099_SKIP_HUD 0
#elif K099_ABL == 2
#undef K099_SKIP_SOLIDS
#define K099_SKIP_SOLIDS 0
#elif K099_ABL == 3
#undef K099_SKIP_CLEAR
#define K099_SKIP_CLEAR 0
#elif K099_ABL == 4
#undef K099_SKIP_GATES
#define K099_SKIP_GATES 0
#elif K099_ABL == 5
#undef K099_SKIP_HUD
#define K099_SKIP_HUD 0
#elif K099_ABL == 6
#undef K099_SKIP_SOLIDS
#undef K099_SKIP_GATES
#undef K099_SKIP_STARS
#undef K099_SKIP_BOLTS
#undef K099_SKIP_CLEAR
#undef K099_SKIP_HUD
#define K099_SKIP_SOLIDS 1
#define K099_SKIP_GATES 0
#define K099_SKIP_STARS 0
#define K099_SKIP_BOLTS 0
#define K099_SKIP_CLEAR 0
#define K099_SKIP_HUD 0
#elif K099_ABL == 7
#undef K099_SKIP_SOLIDS
#undef K099_SKIP_GATES
#undef K099_SKIP_STARS
#undef K099_SKIP_BOLTS
#undef K099_SKIP_CLEAR
#undef K099_SKIP_HUD
#define K099_SKIP_SOLIDS 0
#define K099_SKIP_GATES 0
#define K099_SKIP_STARS 0
#define K099_SKIP_BOLTS 0
#define K099_SKIP_CLEAR 1
#define K099_SKIP_HUD 0
#elif K099_ABL == 8
#undef K099_SKIP_SOLIDS
#undef K099_SKIP_GATES
#undef K099_SKIP_STARS
#undef K099_SKIP_BOLTS
#undef K099_SKIP_CLEAR
#undef K099_SKIP_HUD
#define K099_SKIP_SOLIDS 0
#define K099_SKIP_GATES 1
#define K099_SKIP_STARS 0
#define K099_SKIP_BOLTS 0
#define K099_SKIP_CLEAR 0
#define K099_SKIP_HUD 0
#elif K099_ABL == 9
#undef K099_SKIP_SOLIDS
#undef K099_SKIP_GATES
#undef K099_SKIP_STARS
#undef K099_SKIP_BOLTS
#undef K099_SKIP_CLEAR
#undef K099_SKIP_HUD
#define K099_SKIP_SOLIDS 0
#define K099_SKIP_GATES 0
#define K099_SKIP_STARS 0
#define K099_SKIP_BOLTS 0
#define K099_SKIP_CLEAR 0
#define K099_SKIP_HUD 1
#endif

/// Generador xorshift pequeno: solo al reciclar entidades/estrellas.
struct Rng {
	u32 state = 0x1234abcdU;

	[[nodiscard]] u16 next() noexcept {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		return static_cast<u16>(state);
	}

	/// Entero en `[lo, lo+mask]` con mascara (rangos potencia de dos).
	[[nodiscard]] s16 span(u16 lo, u16 mask) noexcept {
		return static_cast<s16>(lo + (next() & mask));
	}
};

/// Proyecta un punto de camara con la API del renderer.
[[nodiscard]] bool project_point(s16 x, s16 y, s16 z, s16& sx, s16& sy) noexcept {
	if (z <= kNearZ) {
		return false;
	}
	const eng::graphics::CamVertex<eng::coord> v {
		CT::from_int(x), CT::from_int(y), CT::from_int(z)};
	return eng::graphics::project_camera_vertex(v, kCamera, sx, sy);
}

/// Modelos embebidos. Todos son convexos; la silueta es la envolvente de sus
/// vertices proyectados, sin necesidad de caras ni de back-face culling.
template <u16 MaxV, u16 MaxI, u16 MaxF>
void build_dart(retro::MeshData<eng::coord, MaxV, MaxI, MaxF>& mesh, s16 hx, s16 hy,
		s16 nose, s16 tail) noexcept {
	(void)mesh.add_vertex(math3d::vec3(-hx, 0, tail));
	(void)mesh.add_vertex(math3d::vec3(hx, 0, tail));
	(void)mesh.add_vertex(math3d::vec3(0, 0, nose));
	(void)mesh.add_vertex(math3d::vec3(0, -hy, 0));
	(void)mesh.add_vertex(math3d::vec3(0, hy, 0));

	const u16 faces[6][3] = {
		{0u, 1u, 3u}, {0u, 3u, 2u}, {0u, 4u, 1u},
		{0u, 2u, 4u}, {1u, 2u, 3u}, {1u, 4u, 2u},
	};
	for (u8 i = 0u; i < 6u; ++i) {
		(void)mesh.add_face(faces[i], 3u);
	}
}

template <u16 MaxV, u16 MaxI, u16 MaxF>
void build_tetra(retro::MeshData<eng::coord, MaxV, MaxI, MaxF>& mesh, s16 ax, s16 ay, s16 az,
		 s16 bx, s16 by, s16 bz, s16 cx, s16 cy, s16 cz, s16 dx, s16 dy, s16 dz) noexcept {
	(void)mesh.add_vertex(math3d::vec3(ax, ay, az));
	(void)mesh.add_vertex(math3d::vec3(bx, by, bz));
	(void)mesh.add_vertex(math3d::vec3(cx, cy, cz));
	(void)mesh.add_vertex(math3d::vec3(dx, dy, dz));
	const u16 faces[4][3] = {{0u, 1u, 2u}, {0u, 1u, 3u}, {0u, 2u, 3u}, {1u, 2u, 3u}};
	for (u8 i = 0u; i < 4u; ++i) {
		(void)mesh.add_face(faces[i], 3u);
	}
}

struct Star {
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;
	s16 speed = 0;
};

struct Bolt {
	bool active = false;
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;
	s16 dz = 0;
	u8 color = kColorWarm;
};

struct Enemy {
	bool active = false;
	s16 x0 = 0;
	s16 y0 = 0;
	s16 z = 0;
	s16 side = 1;
	s16 phase = 0;
	math3d::Vec3t<eng::coord> position = math3d::vec3(0, 0, 400);
	math3d::Mat3<> rot = math3d::Mat3<>::identity();
};

struct Asteroid {
	s16 x0 = 0;
	s16 y0 = 0;
	s16 z = 0;
	s16 side = 1;
	s16 speed = 0;
	s16 spin = 0;
	math3d::Vec3t<eng::coord> position = math3d::vec3(0, 40, 500);
	math3d::Mat3<> rot = math3d::Mat3<>::identity();
};

struct Gate {
	s16 z = 0;
	s16 speed = 0;
	s16 phase = 0;
};

/// Un solido a pintar: malla resuelta, transform world y color/planos.
struct RenderItem {
	scene3d::ModelHandle model {};
	math3d::Vec3t<eng::coord> position {};
	math3d::Mat3<> rotation {};
	u8 plane_mask = 1u; ///< 1 = plano 0; 2 = plano 1; 3 = ambos
};



struct StarFoxTravel {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext& context) { // NOLINT
		if (!backend.configure_memory({256u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}

		comp::SceneResources res = comp::planar(kWidth, kHeight, kPlanes);
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

		// Los tres buffers nacen negros y reciben UNA vez el fondo estatico.
		for (u8 b = 0u; b < m_display.buffer_count(); ++b) {
			const eng::PlaneBytes planes = m_display.buffer(b);
			(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(planes.size()),
						kWidth, kHeight, kPlanes, kPlaneBytes);
			(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes,
						    kWidth, kHeight);
			backend.wait_blitter();
			draw_static_background(backend);
			backend.wait_blitter();
		}

		if (!build_models()) {
			eng::debug::mark_failed(g_eng_run_status, kBootModels);
			return;
		}
		init_state();
		m_ready = true;
		// Completar dos frames antes de READY: el primer flip publica una pagina
		// ya dibujada mientras el raster muestra la anterior.
		update(backend, context);
		update(backend, context);
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) { // NOLINT
		if (!m_ready) {
			return;
		}
		m_display.commit();
		const eng::PlaneBytes planes = m_display.back();
		bind_dynamic_planes(planes, 3u); // clear/estrellas/disparos: 2 planos

#if !K099_SKIP_CLEAR
		clear_corridor(backend, planes);
#endif

		playfield::Surface world = world_surface();
		++m_frame;
		update_player();
		update_enemies();
		update_asteroids();
		update_gates();

#if !K099_SKIP_STARS
		draw_stars(world);
#endif
#if !K099_SKIP_GATES
		draw_gates(backend, planes);
#endif
#if !K099_SKIP_SOLIDS
		draw_solids(planes);
		draw_engine_glow(world);
		draw_cockpit_patch(planes);
#endif
#if !K099_SKIP_BOLTS
		bind_dynamic_planes(planes, 3u);
		update_bolts();
		for (u16 i = 0u; i < kBoltSlots; ++i) {
			if (m_bolts[i].active) {
				draw_bolt(planes, m_bolts[i]);
			}
		}
#endif

#if !K099_SKIP_HUD
		// El HUD de host cuesta ~25k ciclos/frame en esta escena; se refresca
		// cada 8 frames (la ventana de fps sigue siendo correcta). El resto de
		// frames conserva el ultimo texto del overlay.
		if ((m_frame & 7u) == 0u) {
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
#endif
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	eng::playfield::Surface world_surface() noexcept {
		return eng::playfield::Surface {
			m_canvas, eng::playfield::SurfaceRect {0, static_cast<s16>(kCorridorY),
							      kWidth, kCorridorH}};
	}

	/// Binds 1 o 2 planos del buffer trasero. `mask`: 1 = plano 0, 2 = plano 1,
	/// 3 = ambos. El `Surface` que apunta al canvas ve el nuevo binding al
	/// instante, asi que el llamador no reconstruye el contexto de dibujo.
	void bind_dynamic_planes(eng::PlaneBytes planes, u8 mask) noexcept {
		if (mask == 1u) {
			const eng::PlaneBytes p0 = planes.subspan(0u, kPlaneBytes);
			(void)m_canvas.bind_raw(p0.data(), static_cast<u32>(p0.size()), kWidth,
						kHeight, 1u, kPlaneBytes);
		} else if (mask == 2u) {
			const eng::PlaneBytes p1 = planes.subspan(kPlaneBytes, kPlaneBytes);
			(void)m_canvas.bind_raw(p1.data(), static_cast<u32>(p1.size()), kWidth,
						kHeight, 1u, kPlaneBytes);
		} else {
			(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(planes.size()),
						kWidth, kHeight, kDynamicPlanes, kPlaneBytes);
		}
	}

	/// Limpia la zona de vuelo en los 2 planos dinamicos. El fondo dentro del
	/// corredor es negro; no hay que restaurar nada mas.
	void clear_corridor(eng::amiga::AmigaBackend& backend, eng::PlaneBytes planes) noexcept {
		constexpr u16 kWords = kWidth / 16u;
		for (u8 p = 0u; p < kDynamicPlanes; ++p) {
			const eng::PlaneBytes plane =
				planes.subspan(static_cast<u32>(p) * kPlaneBytes, kPlaneBytes);
			(void)backend.blitter_clear_rect(plane, kRowBytes, 0,
							 static_cast<s16>(kCorridorY), kWords, kCorridorH,
							 false);
		}
		(void)backend.wait_blitter();
	}

	bool build_models() noexcept {
		retro::MeshData<eng::coord, 8, 24, 8> player {};
		build_dart(player, 28, 7, 28, -9);
		const auto player_model = m_models.add_mesh(player.view(), 1u);
		if (!player_model.valid()) return false;
		m_player_model = player_model;

		retro::MeshData<eng::coord, 8, 24, 8> enemy_a {};
		build_dart(enemy_a, 22, 6, 22, -7);
		const auto enemy_a_model = m_models.add_mesh(enemy_a.view(), 1u);
		if (!enemy_a_model.valid()) return false;
		m_enemy_models[0] = enemy_a_model;
		if constexpr (kEnemies > 1u) m_enemy_models[1] = enemy_a_model;

		retro::MeshData<eng::coord, 8, 24, 8> enemy_b {};
		build_dart(enemy_b, 20, 5, 20, -6);
		const auto enemy_b_model = m_models.add_mesh(enemy_b.view(), 1u);
		if (!enemy_b_model.valid()) return false;
		if constexpr (kEnemies > 2u) m_enemy_models[2] = enemy_a_model;
		if constexpr (kEnemies > 3u) m_enemy_models[3] = enemy_b_model;

		if constexpr (kAsteroids > 0u) {
			retro::MeshData<eng::coord, 8, 24, 8> rock_a {};
			build_tetra(rock_a, -18, 8, 6, 16, -10, 10, 6, 14, -12, -8, -12, -14);
			const auto rock_a_model = m_models.add_mesh(rock_a.view(), 1u);
			if (!rock_a_model.valid()) return false;
			m_rock_models[0] = rock_a_model;
			if constexpr (kAsteroids > 1u) {
				retro::MeshData<eng::coord, 8, 24, 8> rock_b {};
				build_tetra(rock_b, -15, 10, 8, 14, -8, 6, 5, 12, -10, -7,
					    -10, -12);
				const auto rock_b_model =
					m_models.add_mesh(rock_b.view(), 1u);
				if (!rock_b_model.valid()) return false;
				m_rock_models[1] = rock_b_model;
			}
		}
		return true;
	}

	void init_state() noexcept {
		m_rng.state = 0x57a3f01dU;
		for (u16 i = 0u; i < kStars; ++i) {
			reset_star(m_stars[i]);
		}
		for (u16 i = 0u; i < kEnemies; ++i) {
			reset_enemy(m_enemies[i], i);
			m_enemies[i].active = true;
		}
		for (u16 i = 0u; i < kAsteroids; ++i) {
			reset_asteroid(m_asteroids[i], i);
		}
		for (u16 i = 0u; i < kGates; ++i) {
			m_gates[i].z = static_cast<s16>(460 + i * 260);
			m_gates[i].speed = 2;
			m_gates[i].phase = static_cast<s16>(i * 2048);
		}
		for (u16 i = 0u; i < kBoltSlots; ++i) {
			m_bolts[i] = {};
		}
	}

	void reset_star(Star& s) noexcept {
		s.x = static_cast<s16>((m_rng.span(0u, 0x00ffu) - 128) * 3);
		s.y = static_cast<s16>((m_rng.span(0u, 0x007fu) - 64) * 3);
		s.z = static_cast<s16>(520 + (m_rng.next() & 0x00ffu));
		s.speed = m_rng.span(7u, 0x0007u);
	}

	void reset_enemy(Enemy& e, u16 slot) noexcept {
		e.side = (slot == 0u || slot == 2u) ? static_cast<s16>(-1) : static_cast<s16>(1);
		e.x0 = static_cast<s16>(e.side * (34 + static_cast<s16>(slot) * 9));
		e.y0 = static_cast<s16>(-6 + static_cast<s16>(slot & 1u) * 12);
		e.z = static_cast<s16>(390 + static_cast<s16>(slot) * 70);
		e.phase = static_cast<s16>((slot * 1024u + (m_rng.next() & 0x03ffu)) & 4095u);
		e.position = math3d::vec3(e.x0, e.y0, e.z);
		e.rot = math3d::Mat3<>::identity();
		e.active = true;
	}

	void reset_asteroid(Asteroid& a, u16 slot) noexcept {
		a.side = (slot & 1u) == 0u ? static_cast<s16>(-1) : static_cast<s16>(1);
		a.x0 = static_cast<s16>(a.side * (118 + static_cast<s16>(slot) * 13 +
						 (m_rng.next() & 0x000fu)));
		a.y0 = static_cast<s16>(28 + static_cast<s16>(slot) * 11);
		a.z = static_cast<s16>(430 + static_cast<s16>(slot) * 90);
		a.speed = static_cast<s16>(4 + static_cast<s16>(slot & 1u) * 2);
		a.spin = static_cast<s16>(m_rng.next() & 0x0fffu);
		a.position = math3d::vec3(a.x0, a.y0, a.z);
		a.rot = math3d::Mat3<>::identity();
	}

	void update_player() noexcept {
		const s32 sin = retro::kSinQ12[m_frame & 4095u];
		const s32 cos = retro::kSinQ12[(m_frame + 1024u) & 4095u];
		m_player_pos = math3d::vec3(
			static_cast<s16>((sin * 22) >> 12),
			static_cast<s16>(24 + ((cos * 5) >> 12)), kPlayerZ);

		const s16 roll = static_cast<s16>((sin * 120) >> 12);
		const s16 pitch = static_cast<s16>((cos * 46) >> 12);
		const s16 yaw = static_cast<s16>((sin * 30) >> 12);
		math3d::load_rotate(m_player_rot, retro::turns(static_cast<u16>(pitch) & 4095u),
				    retro::turns(static_cast<u16>(yaw) & 4095u),
				    retro::turns(static_cast<u16>(roll) & 4095u));
	}

	void update_enemies() noexcept {
		for (u16 i = 0u; i < kEnemies; ++i) {
			Enemy& e = m_enemies[i];
			e.z = static_cast<s16>(e.z - kEnemySpeed);
			const s16 drift = e.z < 320 ? static_cast<s16>((320 - e.z) * 3 / 4) : 0;
			const s32 weave = retro::kSinQ12[(m_frame * 3u + static_cast<u16>(e.phase)) &
							 4095u];
			const s16 x = static_cast<s16>(e.x0 + ((weave * 18) >> 12) + e.side * drift);
			const s16 y = static_cast<s16>(e.y0 + ((weave * 5) >> 12));
			if (e.z < kEnemyResetZ || x > 260 || x < -260) {
				reset_enemy(e, i);
				e.position = math3d::vec3(e.x0, e.y0, e.z);
				continue;
			}
			e.position = math3d::vec3(x, y, e.z);
			const s16 bank = static_cast<s16>(e.side * drift * 2);
			math3d::load_rotate(e.rot, retro::turns(static_cast<u16>(e.phase) & 4095u),
					    retro::turns(2048u),
					    retro::turns(static_cast<u16>(bank) & 4095u));
		}
	}

	void update_asteroids() noexcept {
		for (u16 i = 0u; i < kAsteroids; ++i) {
			Asteroid& a = m_asteroids[i];
			a.z = static_cast<s16>(a.z - a.speed);
			const s16 drift = a.z < 360 ? static_cast<s16>((360 - a.z) / 3) : 0;
			const s16 x = static_cast<s16>(a.x0 + a.side * drift);
			if (a.z < kAsteroidResetZ || x > 280 || x < -280) {
				reset_asteroid(a, i);
				a.position = math3d::vec3(a.x0, a.y0, a.z);
				continue;
			}
			a.position = math3d::vec3(x, a.y0, a.z);
			const u16 spin = static_cast<u16>(m_frame * 9u + a.spin);
			math3d::load_rotate(a.rot, retro::turns(spin),
					    retro::turns(static_cast<u16>(spin * 3u)),
					    retro::turns(static_cast<u16>(spin * 5u)));
		}
	}

	void update_gates() noexcept {
		for (u16 i = 0u; i < kGates; ++i) {
			Gate& g = m_gates[i];
			g.z = static_cast<s16>(g.z - g.speed);
			if (g.z < kGateResetZ) {
				g.z = 760;
				g.phase = static_cast<s16>(m_rng.next() & 0x03ffu);
			}
		}
	}

	void draw_stars(playfield::Surface& world) noexcept {
		for (u16 i = 0u; i < kStars; ++i) {
			Star& s = m_stars[i];
			s.z = static_cast<s16>(s.z - s.speed);
			if (s.z < kNearZ + 8) {
				reset_star(s);
			}
			s16 sx = 0;
			s16 sy = 0;
			if (!project_point(s.x, s.y, s.z, sx, sy)) {
				continue;
			}
			if (sx < 0 || sy < static_cast<s16>(kCorridorY) || sx >= static_cast<s16>(kWidth) ||
			    sy > static_cast<s16>(kCorridorBottom)) {
				continue;
			}
			(void)world.set_pixel(sx, sy, s.z < 230 ? kColorStarNear : kColorStarFar);
		}
	}

	void draw_gates(eng::amiga::AmigaBackend& backend, eng::PlaneBytes planes) noexcept {
		eng::graphics::WireScreenEdge edges[8u * (kGates == 0u ? 1u : kGates)] {};
		u16 count = 0u;
		// Octogono: 8 vertices y 8 aristas. Un anillo pequeno de alambre es
		// decorado lejano y cuesta mucho menos que un solido relleno.
		constexpr u16 kOctAngle[8] = {0u, 512u, 1024u, 1536u, 2048u, 2560u, 3072u, 3584u};
		for (u16 gi = 0u; gi < kGates; ++gi) {
			const Gate& g = m_gates[gi];
			const s32 sway = retro::kSinQ12[(m_frame * 2u + static_cast<u16>(g.phase)) &
							4095u];
			const s16 gx = static_cast<s16>((sway * 20) >> 12);
			s16 xs[8] {};
			s16 ys[8] {};
			bool ok = true;
			for (u8 i = 0u; i < 8u; ++i) {
				const s32 c = retro::kSinQ12[(kOctAngle[i] + 1024u) & 4095u];
				const s32 s = retro::kSinQ12[kOctAngle[i] & 4095u];
				const s16 px = static_cast<s16>(gx + ((c * 40) >> 12));
				const s16 py = static_cast<s16>((s * 40) >> 12);
				if (!project_point(px, py, g.z, xs[i], ys[i])) {
					ok = false;
					break;
				}
			}
			if (!ok) continue;
			for (u8 i = 0u; i < 8u; ++i) {
				const u8 j = static_cast<u8>((i + 1u == 8u) ? 0u : (i + 1u));
				if (count < 8u * kGates) {
					edges[count++] = eng::graphics::WireScreenEdge {
						xs[i], ys[i], xs[j], ys[j], kColorWarm};
				}
			}
		}
		if (count == 0u) return;
		(void)retro::wire_lines(
			backend, planes, kRowBytes, kPlaneBytes, kDynamicPlanes,
			eng::Span<const eng::graphics::WireScreenEdge>(edges, count), false,
			&kCorridorClip);
	}

	/// Proyecta los vertices del modelo y calcula la envolvente convexa 2D. Los
	/// puntos salen en coordenadas de pantalla absolutas. Devuelve `false` si el
	/// modelo no es proyectable (near/capacidad).
	[[nodiscard]] bool project_hull(scene3d::ModelHandle handle,
					const math3d::Vec3t<eng::coord>& position,
					const math3d::Mat3<>& rotation, s16* hx, s16* hy,
					u16& out_n) noexcept {
		const eng::Ref<const scene3d::Mesh> mesh_ref = m_models.mesh(handle);
		if (!mesh_ref.valid()) return false;
		const scene3d::Mesh& mesh = *mesh_ref;
		const u32 nv = mesh.vertex_count();
		if (nv < 3u || nv > 8u) return false;

		const math3d::Affine3<> model {rotation, position};
		math3d::mesh_transform(
			mesh.vertices, model,
			eng::Span<math3d::Vec3t<eng::coord>>(m_world_scratch, nv));
		for (u32 i = 0u; i < nv; ++i) {
			const eng::graphics::CamVertex<eng::coord> v {
				m_world_scratch[i].v[0], m_world_scratch[i].v[1],
				m_world_scratch[i].v[2]};
			if (!eng::graphics::project_camera_vertex(v, kCamera, m_sx[i], m_sy[i])) {
				return false;
			}
		}

		// Envolvente convexa (monotone chain); maximo 8 puntos proyectados.
		u16 order[8] {};
		for (u32 i = 0u; i < nv; ++i) order[i] = static_cast<u16>(i);
		for (u32 i = 1u; i < nv; ++i) {
			const u16 key = order[i];
			u32 j = i;
			while (j > 0u) {
				const u16 prev = order[j - 1u];
				const bool after = m_sx[prev] > m_sx[key] ||
						   (m_sx[prev] == m_sx[key] && m_sy[prev] > m_sy[key]);
				if (!after) break;
				order[j] = prev;
				--j;
			}
			order[j] = key;
		}
		const auto cross = [&](u16 o, u16 a, u16 b) -> s32 {
			const s32 ax = static_cast<s32>(m_sx[a]) - m_sx[o];
			const s32 ay = static_cast<s32>(m_sy[a]) - m_sy[o];
			const s32 bx = static_cast<s32>(m_sx[b]) - m_sx[o];
			const s32 by = static_cast<s32>(m_sy[b]) - m_sy[o];
			return ax * by - ay * bx;
		};
		u16 hull[16] {};
		u16 k = 0u;
		for (u32 i = 0u; i < nv; ++i) {
			while (k >= 2u && cross(hull[k - 2u], hull[k - 1u], order[i]) <= 0) --k;
			hull[k++] = order[i];
		}
		const u16 lower = k + 1u;
		for (u32 i = nv - 1u; i > 0u; --i) {
			const u16 p = order[i - 1u];
			while (k >= lower && cross(hull[k - 2u], hull[k - 1u], p) <= 0) --k;
			hull[k++] = p;
		}
		if (k > 1u) --k;
		if (k < 3u) return false;
		for (u16 i = 0u; i < k; ++i) {
			hx[i] = m_sx[hull[i]];
			hy[i] = m_sy[hull[i]];
		}
		out_n = k;
		return true;
	}

	/// Rasteriza la silueta por scanlines con un paso 16.16 por arista (una
	/// division por arista, ningun `while` por fila) y escribe cada span con
	/// `plane_or_span`. Para objetos de 4-5 vertices es mas barato que el DDA
	/// generico de `convex_spans`, que acumula muchos pasos en aristas poco
	/// profundas (medido en 099: las siluetas dominaban el frame).
	void fill_hull_spans(const s16* hx, const s16* hy, u16 n, u8* plane0,
			     u8* plane1) noexcept {
		s32 ymin = 32767;
		s32 ymax = -32768;
		for (u16 i = 0u; i < n; ++i) {
			if (hy[i] < ymin) ymin = hy[i];
			if (hy[i] > ymax) ymax = hy[i];
		}
		if (ymax < static_cast<s32>(kCorridorY) ||
		    ymin > static_cast<s32>(kCorridorBottom)) {
			return;
		}
		s32 y0 = ymin < 0 ? 0 : ymin;
		s32 y1 = ymax > 255 ? 255 : ymax;
		for (s32 y = y0; y <= y1; ++y) {
			m_span_l[y] = 32767;
			m_span_r[y] = -32768;
		}
		for (u16 i = 0u; i < n; ++i) {
			const u16 j = static_cast<u16>((i + 1u == n) ? 0u : (i + 1u));
			s32 ya = hy[i];
			s32 yb = hy[j];
			if (ya == yb) continue;
			s32 xa = hx[i];
			s32 xb = hx[j];
			if (ya > yb) {
				const s32 t = ya;
				ya = yb;
				yb = t;
				const s32 tx = xa;
				xa = xb;
				xb = tx;
			}
			const s32 dy = yb - ya;
			const s32 step = ((xb - xa) << 16) / dy; // una division por arista
			s32 x_fx = xa << 16;
			for (s32 y = ya; y < yb; ++y) {
				if (y >= y0 && y <= y1) {
					const s32 x = x_fx >> 16;
					if (x < m_span_l[y]) m_span_l[y] = static_cast<s16>(x);
					if (x > m_span_r[y]) m_span_r[y] = static_cast<s16>(x);
				}
				x_fx += step;
			}
		}
		const s32 first = ymin < static_cast<s32>(kCorridorY)
					  ? static_cast<s32>(kCorridorY)
					  : ymin;
		const s32 last = (ymax - 1) > static_cast<s32>(kCorridorBottom)
					 ? static_cast<s32>(kCorridorBottom)
					 : (ymax - 1);
		for (s32 y = first; y <= last; ++y) {
			s32 xl = m_span_l[y];
			s32 xr = m_span_r[y];
			if (xl > xr) continue;
			if (xl < 0) xl = 0;
			if (xr >= static_cast<s32>(kWidth)) xr = static_cast<s32>(kWidth) - 1;
			if (xl > xr) continue;
			if (plane0 != nullptr) {
				eng::graphics::detail::plane_or_span(plane0, kRowBytes, y, xl,
								     xr, 0xffffu);
			}
			if (plane1 != nullptr) {
				eng::graphics::detail::plane_or_span(plane1, kRowBytes, y, xl,
								     xr, 0xffffu);
			}
		}
	}

	/// Prepara los solidos, los ordena de lejos a cerca (painter) y pinta cada
	/// silueta en su plano: 0 = jugador (cian), 1 = enemigos (rojo), 0+1 =
	/// asteroides/llama (amarillo). El relleno por OR permite solapar grupos sin
	/// destruir el fondo ya limpiado.
	void draw_solids(eng::PlaneBytes planes) noexcept {
		RenderItem items[kMaxItems] {};
		u16 count = 0u;

		items[count++] = RenderItem {m_player_model, m_player_pos, m_player_rot, 1u};
		for (u16 i = 0u; i < kEnemies; ++i) {
			if (!m_enemies[i].active) continue;
			items[count++] = RenderItem {m_enemy_models[i], m_enemies[i].position,
						     m_enemies[i].rot, 2u};
		}
		for (u16 i = 0u; i < kAsteroids; ++i) {
			items[count++] = RenderItem {m_rock_models[i], m_asteroids[i].position,
						     m_asteroids[i].rot, 3u};
		}

		u16 order[kMaxItems] {};
		for (u16 i = 0u; i < count; ++i) order[i] = i;
		for (u16 i = 1u; i < count; ++i) {
			const u16 key = order[i];
			const s32 z = CT::to_int(items[key].position.v[2]);
			u16 j = i;
			while (j > 0u) {
				const s32 prev = CT::to_int(items[order[j - 1u]].position.v[2]);
				if (prev >= z) break;
				order[j] = order[j - 1u];
				--j;
			}
			order[j] = key;
		}

		u8* const base0 = planes.data();
		u8* const base1 = planes.data() + kPlaneBytes;
		for (u16 i = 0u; i < count; ++i) {
			const RenderItem& it = items[order[i]];
			s16 hx[16] {};
			s16 hy[16] {};
			u16 n = 0u;
			if (!project_hull(it.model, it.position, it.rotation, hx, hy, n)) {
				continue;
			}
#if !K099_SKIP_FILL
			u8* p0 = (it.plane_mask & 1u) != 0u ? base0 : nullptr;
			u8* p1 = (it.plane_mask & 2u) != 0u ? base1 : nullptr;
			fill_hull_spans(hx, hy, n, p0, p1);
#else
			(void)hx;
			(void)hy;
			(void)n;
			(void)base0;
			(void)base1;
#endif
		}
	}

	/// Llamita del motor: un segmento corto en el plano calido (amarillo) en la
	/// posicion local del escape. No es un solido 3D; ahorra una silueta entera.
	void draw_engine_glow(playfield::Surface& world) noexcept {
		const math3d::Affine3<> player_model {m_player_rot, m_player_pos};
		const math3d::Vec3t<eng::coord> exhaust_local = math3d::vec3(0, 0, -10);
		math3d::Vec3t<eng::coord> exhaust {};
		math3d::transform(player_model, &exhaust, &exhaust_local, 1u);
		s16 sx = 0;
		s16 sy = 0;
		if (!project_point(CT::to_int(exhaust.v[0]), CT::to_int(exhaust.v[1]),
				   CT::to_int(exhaust.v[2]), sx, sy)) {
			return;
		}
		(void)world.draw_line(sx, static_cast<s16>(sy - 2), sx,
				      static_cast<s16>(sy + 3), kColorWarm);
	}

	/// Cabina 2D sobre el caza: un pequeno rombo calido de 5 filas en la
	/// proyeccion del punto local (0,-5,-2). Escribe directo en los dos planos
	/// dinamicos (`plane_or_span`); usar `Surface::fill_span` por fila costaba
	/// ~25k ciclos en esta escena, mas que la propia silueta.
	void draw_cockpit_patch(eng::PlaneBytes planes) noexcept {
		const math3d::Affine3<> player_model {m_player_rot, m_player_pos};
		const math3d::Vec3t<eng::coord> cockpit_local = math3d::vec3(0, -5, -2);
		math3d::Vec3t<eng::coord> cockpit_pos {};
		math3d::transform(player_model, &cockpit_pos, &cockpit_local, 1u);
		s16 sx = 0;
		s16 sy = 0;
		if (!project_point(CT::to_int(cockpit_pos.v[0]), CT::to_int(cockpit_pos.v[1]),
				   CT::to_int(cockpit_pos.v[2]), sx, sy)) {
			return;
		}
		u8* const base0 = planes.data();
		u8* const base1 = planes.data() + kPlaneBytes;
		constexpr s16 kHalf[5] = {1, 2, 3, 2, 1};
		for (s16 r = 0; r < 5; ++r) {
			const s32 y = static_cast<s32>(sy) - 2 + r;
			if (y < static_cast<s32>(kCorridorY) ||
			    y > static_cast<s32>(kCorridorBottom)) {
				continue;
			}
			s32 xl = static_cast<s32>(sx) - kHalf[r];
			s32 xr = static_cast<s32>(sx) + kHalf[r];
			if (xl < 0) xl = 0;
			if (xr >= static_cast<s32>(kWidth)) xr = static_cast<s32>(kWidth) - 1;
			if (xl > xr) continue;
			eng::graphics::detail::plane_or_span(base0, kRowBytes, y, xl, xr, 0xffffu);
			eng::graphics::detail::plane_or_span(base1, kRowBytes, y, xl, xr, 0xffffu);
		}
	}

	void update_bolts() noexcept {
		if ((m_frame % 11u) == 0u) {
			for (u16 i = 0u; i < kBoltSlots; ++i) {
				Bolt& b = m_bolts[i];
				if (b.active) continue;
				b.active = true;
				b.dz = 24;
				b.color = kColorWarm;
				b.z = static_cast<s16>(kPlayerZ + 26);
				b.x = static_cast<s16>(((m_frame / 11u) & 1u) == 0u ? -14 : 14);
				b.y = 10; // por encima del caza en pantalla
				break;
			}
		}
		// Fuego enemigo: una bala roja cada ~1 s desde el enemigo mas cercano.
		// Viene hacia la camara (dz negativo) y anade profundidad al traveling.
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
					b.color = kColorEnemy;
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
			if (b.z > 640 || b.z < 90) b.active = false;
		}
	}

	void draw_bolt(eng::PlaneBytes planes, const Bolt& b) noexcept {
		s16 sx = 0;
		s16 sy = 0;
		if (!project_point(b.x, b.y, b.z, sx, sy)) {
			return;
		}
		// Rastro vertical de 6 px escrito directo en planos: la cola 3D a lo
		// largo de z proyectaba 1-2 px y las balas se veian como puntos; una
		// llamada a `Surface::draw_line` por bala costaba mas que todo el resto.
		const s16 dir = b.dz > 0 ? 1 : -1;
		u8* const p0 = (b.color & 1u) != 0u ? planes.data() : nullptr;
		u8* const p1 = (b.color & 2u) != 0u ? planes.data() + kPlaneBytes : nullptr;
		for (s16 i = 0; i <= 5; ++i) {
			const s32 y = static_cast<s32>(sy) + static_cast<s32>(dir) * i;
			if (y < static_cast<s32>(kCorridorY) ||
			    y > static_cast<s32>(kCorridorBottom)) {
				continue;
			}
			if (sx < 0 || sx >= static_cast<s16>(kWidth)) {
				continue;
			}
			if (p0 != nullptr) {
				eng::graphics::detail::plane_or_span(p0, kRowBytes, y, sx, sx,
								     0xffffu);
			}
			if (p1 != nullptr) {
				eng::graphics::detail::plane_or_span(p1, kRowBytes, y, sx, sx,
								     0xffffu);
			}
		}
	}

	/// Fondo estatico (planos 2/3): bandas, luna y rotulos del HUD. Se pinta una
	/// vez por buffer y no se toca en runtime.
	void draw_static_background(eng::amiga::AmigaBackend& backend) noexcept {
		eng::playfield::Surface top {
			m_canvas, eng::playfield::SurfaceRect {0, 0, kWidth, kCorridorY}};
		(void)top.fill_rect(0, 0, kWidth, 8u, 4u);
		(void)top.fill_rect(0, 8, kWidth, 8u, 5u);
		backend.wait_blitter();

		eng::playfield::Surface bottom {
			m_canvas, eng::playfield::SurfaceRect {0, static_cast<s16>(kCorridorY + kCorridorH),
							      kWidth, 32u}};
		(void)bottom.fill_rect(0, static_cast<s16>(kCorridorY + kCorridorH), kWidth, 16u,
				      5u);
		(void)bottom.fill_rect(0, static_cast<s16>(kCorridorY + kCorridorH) + 16, kWidth,
				      16u, 4u);
		backend.wait_blitter();

		s16 moon_x[12] {};
		s16 moon_y[12] {};
		s16 light_x[12] {};
		s16 light_y[12] {};
		constexpr u16 kRingStep[12] = {0u, 341u, 683u, 1024u, 1365u, 1707u,
					      2048u, 2389u, 2731u, 3072u, 3413u, 3755u};
		for (u8 i = 0u; i < 12u; ++i) {
			const s32 c = retro::kSinQ12[(kRingStep[i] + 1024u) & 4095u];
			const s32 s = retro::kSinQ12[kRingStep[i] & 4095u];
			moon_x[i] = static_cast<s16>(266 + ((c * 30) >> 12));
			moon_y[i] = static_cast<s16>(212 + ((s * 30) >> 12));
			light_x[i] = static_cast<s16>(260 + ((c * 21) >> 12));
			light_y[i] = static_cast<s16>(206 + ((s * 21) >> 12));
		}
		(void)bottom.fill_polygon(moon_x, moon_y, 12u, 6u);
		backend.wait_blitter();
		(void)bottom.fill_polygon(light_x, light_y, 12u, 7u);
		backend.wait_blitter();

		(void)bottom.fill_span(0, static_cast<s32>(kWidth) - 1,
				       static_cast<s32>(kCorridorY + kCorridorH), 9u);

		// Rocas estaticas de primer plano en la banda inferior: decorado con
		// coste cero por frame (se pintan una vez por buffer, planos 2/3).
		constexpr s16 kRockX[3] = {38, 108, 196};
		constexpr s16 kRockY[3] = {204, 212, 209};
		constexpr s16 kRockR[3] = {24, 17, 21};
		constexpr u16 kOct[8] = {0u, 512u, 1024u, 1536u, 2048u, 2560u, 3072u, 3584u};
		for (u8 r = 0u; r < 3u; ++r) {
			s16 xs[8] {};
			s16 ys[8] {};
			s16 lx[8] {};
			s16 ly[8] {};
			for (u8 i = 0u; i < 8u; ++i) {
				const s32 c = retro::kSinQ12[(kOct[i] + 1024u) & 4095u];
				const s32 s = retro::kSinQ12[kOct[i] & 4095u];
				xs[i] = static_cast<s16>(kRockX[r] + ((c * kRockR[r]) >> 12));
				ys[i] = static_cast<s16>(kRockY[r] + ((s * kRockR[r]) >> 12));
				lx[i] = static_cast<s16>(kRockX[r] - 3 +
							 ((c * (kRockR[r] - 6)) >> 12));
				ly[i] = static_cast<s16>(kRockY[r] - 3 +
							 ((s * (kRockR[r] - 6)) >> 12));
			}
			(void)bottom.fill_polygon(xs, ys, 8u, 12u);
			(void)bottom.fill_polygon(lx, ly, 8u, 13u);
		}

		// Planeta lejano asomando en la banda superior (tambien plano 2/3).
		{
			s16 xs[12] {};
			s16 ys[12] {};
			constexpr u16 kRing[12] = {0u, 341u, 683u, 1024u, 1365u, 1707u,
						   2048u, 2389u, 2731u, 3072u, 3413u, 3755u};
			for (u8 i = 0u; i < 12u; ++i) {
				const s32 c = retro::kSinQ12[(kRing[i] + 1024u) & 4095u];
				const s32 s = retro::kSinQ12[kRing[i] & 4095u];
				xs[i] = static_cast<s16>(50 + ((c * 32) >> 12));
				ys[i] = static_cast<s16>(-16 + ((s * 32) >> 12));
			}
			(void)top.fill_polygon(xs, ys, 12u, 6u);
		}

		eng::playfield::Surface hud {
			m_canvas,
			eng::playfield::SurfaceRect {0, static_cast<s16>(kBandY), kWidth, kBandH}};
		(void)hud.draw_text_words(16, static_cast<s32>(kBandY) + 8, "099 STARFOX TRAVEL",
					  kHudTitle);
		(void)hud.draw_text_words(16, static_cast<s32>(kBandY) + 24,
					  "RAIL RUN  SECTOR 7   NO INPUT", kHudSubtitle);
	}

	// --- estado -------------------------------------------------------------
	bool m_ready = false;
	u16 m_frame = 0u;
	comp::Scene m_display {};
	eng::playfield::ContiguousPlayfield m_canvas {};

	scene3d::ModelTable<8, 8, 24, 8, 1> m_models {};
	scene3d::ModelHandle m_player_model {};
	scene3d::ModelHandle m_enemy_models[kEnemies == 0u ? 1u : kEnemies] {};
	scene3d::ModelHandle m_rock_models[kAsteroids == 0u ? 1u : kAsteroids] {};

	math3d::Vec3t<eng::coord> m_player_pos = math3d::vec3(0, 24, kPlayerZ);
	math3d::Mat3<> m_player_rot {};

	Rng m_rng {};
	Star m_stars[kStars] {};
	Enemy m_enemies[kEnemies] {};
	Asteroid m_asteroids[kAsteroids == 0u ? 1u : kAsteroids] {};
	Gate m_gates[kGates == 0u ? 1u : kGates] {};
	Bolt m_bolts[kBoltSlots] {};
	eng::debug::HostHud m_host_hud {};

	math3d::Vec3t<eng::coord> m_world_scratch[8] {};
	s16 m_sx[8] {};
	s16 m_sy[8] {};
	s16 m_span_l[256] {};
	s16 m_span_r[256] {};
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
