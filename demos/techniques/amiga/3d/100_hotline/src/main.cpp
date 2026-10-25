// ============================================================================
// 100 - HOTLINE (Superhot-like 2.5D playground)
// ============================================================================
// Tiempo detenido hasta que el jugador se mueve. Enemigos hechos con cajas 3D,
// sala con sombreado de grises y rojos, 3 planos y fondo pre-renderizado. La
// proyección es la de un corredor con cámara en el nodo actual.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/100_hotline --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/100_hotline --config A500_release

#include <eng/api/api.hpp>
#include <eng/core/data/polygon.hpp>
#include <eng/core/math/isqrt.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/polygon_fill.hpp>
#include <eng/platform/amiga/input_poll.hpp>

namespace {

namespace comp = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kRowBytes = kWidth / 8u;
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u8 kPlanes = 3u;
constexpr u8 kBuffers = 3u;
constexpr u16 kHudY = 208u;
constexpr u16 kHudH = kHeight - kHudY;
constexpr u16 kViewRows = kHudY;
constexpr u32 kViewPlaneBytes = static_cast<u32>(kRowBytes) * kViewRows;
constexpr u32 kViewBytes = static_cast<u32>(kPlanes) * kViewPlaneBytes;
constexpr u8 kViews = 4u;
constexpr u32 kViewsBytes = static_cast<u32>(kViews) * kViewBytes;

constexpr s16 kFocal = 220;
constexpr s16 kEyeY = 48;
constexpr s16 kCx = 160;
constexpr s16 kCy = 104;

constexpr eng::Palette32 kPalette {{
	0x000, // 0 negro
	0xfff, // 1 blanco
	0x999, // 2 gris claro
	0x555, // 3 gris oscuro
	0x711, // 4 rojo oscuro
	0xc22, // 5 rojo medio
	0xf66, // 6 rojo claro
	0x400, // 7 rojo profundo
}};

constexpr u8 kBlack = 0u;
constexpr u8 kWhite = 1u;
constexpr u8 kGrayLight = 2u;
constexpr u8 kGrayDark = 3u;
constexpr u8 kRedDark = 4u;
constexpr u8 kRed = 5u;
constexpr u8 kRedLight = 6u;
constexpr u8 kRedDeep = 7u;

enum : u32 { kBootMemory = 0x00010001u, kBootCompose = 0x00010002u };
constexpr eng::debug::Version kVersion {0u, 1u};
constexpr u16 kRoomMin = 32u;
constexpr u16 kRoomMax = 224u;
constexpr u16 kStep = 24u;
constexpr u16 kEnemyStep = 6u;
constexpr u16 kBulletStep = 8u;
constexpr u16 kBulletSlots = 8u;
constexpr u16 kPlayerRadius = 8u;

struct Enemy {
	bool alive = true;
	s16 x = 0;
	s16 z = 0;
	u8 cooldown = 0;
	u8 anim = 0;
	u8 aim = 0;
};

struct Bullet {
	bool active = false;
	s16 x = 0;
	s16 z = 0;
};

struct Pt {
	s16 x = 0;
	s16 y = 0;
};

struct Box3 {
	s16 x0 = 0, y0 = 0, z0 = 0;
	s16 x1 = 0, y1 = 0, z1 = 0;
	u8 color = kRed;
};

/// Pieza ya posada por el rig: ocho vértices world (no un AABB), de modo que un
/// brazo o una pierna girados conservan su forma y no se convierten en una caja
/// envolvente axis-aligned.
struct Part3 {
	s16 x[16] {};
	s16 y[16] {};
	s16 z[16] {};
	s16 fz = 0;
	u8 color = kRed;
	u8 n = 0;
};

/// Vértice de la piel continua. `bone` es el hueso rígido que lo transforma;
/// el color lo aporta el grupo de dibujo para no repetirlo 55 veces.
struct MeshVertex {
	s16 x = 0, y = 0, z = 0;
	u8 bone = 0;
};

/// Transformación world de un hueso. `c`/`s` son cos/sin Q8 del giro local;
/// los huesos usados aquí solo giran en el plano XY (eje Z), suficiente para
/// articular brazos, piernas y cabeza sin matrices de 32 bits por vértice.
struct BoneFrame {
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;
	s16 c = 256;
	s16 s = 0;
};

/// Malla única de 55 vértices. Es la misma para los tres personajes; cada
/// vértice pertenece a un solo hueso (skinning rígido). Y arriba, X derecha,
/// Z delante; el render invierte Z para que la cara mire a la cámara.
static constexpr MeshVertex kBody[55] = {
	// Cabeza (neck)
	{0, 58, 4, 1}, {5, 56, 2, 1}, {-5, 56, 2, 1}, {5, 56, -3, 1},
	{-5, 56, -3, 1}, {0, 50, 3, 1}, {4, 50, 0, 1}, {-4, 50, 0, 1},
	{0, 52, -4, 1},
	// Visor (neck)
	{3, 55, 5, 1}, {-3, 55, 5, 1}, {3, 52, 5, 1}, {-3, 52, 5, 1},
	// Torso (root)
	{7, 46, 4, 0}, {-7, 46, 4, 0}, {7, 46, -3, 0}, {-7, 46, -3, 0},
	{8, 32, 4, 0}, {-8, 32, 4, 0}, {8, 32, -3, 0}, {-8, 32, -3, 0},
	{6, 20, 3, 0}, {-6, 20, 3, 0}, {6, 20, -2, 0}, {-6, 20, -2, 0},
	// Brazo L (arm_L)
	{-10, 45, 1, 2}, {-18, 40, 1, 2}, {-18, 36, 1, 2}, {-24, 30, 1, 2},
	{-24, 28, 1, 2}, {-10, 43, -1, 2}, {-18, 38, -1, 2}, {-24, 29, -1, 2},
	// Brazo R (arm_R)
	{10, 45, 1, 3}, {18, 40, 1, 3}, {18, 36, 1, 3}, {24, 30, 1, 3},
	{24, 28, 1, 3}, {10, 43, -1, 3}, {18, 38, -1, 3}, {24, 29, -1, 3},
	// Pierna L (leg_L)
	{-5, 18, 2, 4}, {-6, 10, 2, 4}, {-6, 10, -1, 4}, {-5, 0, 3, 4},
	{-5, 0, -2, 4}, {-3, 18, -1, 4}, {-4, 10, 0, 4},
	// Pierna R (leg_R)
	{5, 18, 2, 5}, {6, 10, 2, 5}, {6, 10, -1, 5}, {5, 0, 3, 5},
	{5, 0, -2, 5}, {3, 18, -1, 5}, {4, 10, 0, 5},
};

/// Caras triangulares de la piel. `base` conserva la intención del modelo:
/// 1..3 son rojos (cabeza/torso/visor) y 4..5 grises (brazos/piernas).
struct BodyFace {
	u8 a = 0;
	u8 b = 0;
	u8 c = 0;
	u8 base = 2;
};

static constexpr BodyFace kBodyFaces[] = {
	// Cabeza
	{0, 1, 2, 1}, {0, 2, 5, 1}, {1, 3, 0, 1}, {2, 0, 4, 1},
	{1, 6, 3, 1}, {2, 4, 7, 1}, {3, 8, 4, 1}, {5, 6, 1, 1},
	{5, 2, 7, 1}, {6, 8, 3, 1}, {7, 4, 8, 1}, {5, 7, 8, 1},
	{5, 8, 6, 1},
	// Visor
	{9, 10, 12, 3}, {9, 12, 11, 3},
	// Torso
	{13, 14, 18, 2}, {13, 18, 17, 2}, {15, 19, 20, 2}, {15, 20, 16, 2},
	{13, 17, 19, 2}, {13, 19, 15, 2}, {14, 16, 20, 2}, {14, 20, 18, 2},
	{17, 18, 22, 2}, {17, 22, 21, 2}, {19, 23, 24, 2}, {19, 24, 20, 2},
	{17, 21, 23, 2}, {17, 23, 19, 2}, {18, 20, 24, 2}, {18, 24, 22, 2},
	// Unión cuello-torso
	{6, 13, 7, 2}, {7, 13, 14, 2}, {6, 15, 13, 2}, {7, 14, 16, 2},
	// Brazo L
	{25, 26, 31, 4}, {25, 31, 30, 4}, {26, 27, 31, 4}, {27, 28, 32, 4},
	{27, 32, 31, 4}, {28, 29, 32, 4}, {14, 25, 30, 4}, {14, 30, 16, 4},
	// Brazo R
	{33, 38, 39, 4}, {33, 39, 34, 4}, {34, 39, 35, 4}, {35, 39, 40, 4},
	{35, 40, 36, 4}, {36, 40, 37, 4}, {13, 15, 38, 4}, {13, 38, 33, 4},
	// Pierna L
	{41, 42, 47, 5}, {41, 47, 46, 5}, {42, 44, 47, 5}, {42, 43, 45, 5},
	{42, 45, 44, 5}, {43, 47, 45, 5}, {22, 41, 46, 5}, {22, 46, 24, 5},
	// Pierna R
	{48, 53, 54, 5}, {48, 54, 49, 5}, {49, 54, 51, 5}, {49, 50, 52, 5},
	{49, 52, 51, 5}, {50, 54, 52, 5}, {21, 23, 53, 5}, {21, 53, 48, 5},
};
constexpr u16 kBodyFaceCount = static_cast<u16>(sizeof(kBodyFaces) / sizeof(kBodyFaces[0]));

/// Peso secundario por vértice en los hombros/caderas. El primero es el hueso
/// primario de `MeshVertex`; este reparte el vértice para suavizar la unión.
struct SkinWeight {
	u8 secondary = 0xFF;
	u8 weight = 0; // 0..256
};

[[nodiscard]] static constexpr SkinWeight skin_weight(u8 i) noexcept {
	switch (i) {
	case 5: case 6: case 7: case 8:
		return {0, 80}; // base de cuello hacia root
	case 13: case 15:
		return {3, 80}; // hombro derecho hacia arm_R
	case 14: case 16:
		return {2, 80}; // hombro izquierdo hacia arm_L
	case 25: case 30:
		return {0, 96}; // arranque brazo L hacia root
	case 33: case 38:
		return {0, 96}; // arranque brazo R hacia root
	case 21: case 23:
		return {5, 80}; // cintura derecha hacia leg_R
	case 22: case 24:
		return {4, 80}; // cintura izquierda hacia leg_L
	case 41: case 46:
		return {0, 96}; // cadera L hacia root
	case 48: case 53:
		return {0, 96}; // cadera R hacia root
	default:
		return {};
	}
}

struct FaceDraw {
	Pt p[3] {};
	s16 fz = 0;
	u8 color = kRed;
};
// Origen rest de cada hueso en espacio del personaje (pies en y=0).
static constexpr s16 kBoneRestX[6] = {0, 0, -12, 12, -5, 5};
static constexpr s16 kBoneRestY[6] = {16, 30, 28, 28, 16, 16};
struct Game {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext& context) { // NOLINT
		if (!backend.configure_memory({384u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		comp::SceneResources res = comp::planar(kWidth, kHeight, kPlanes);
		res.buffers = kBuffers;
		if (!comp::compose(m_display, backend.memory_manager(), res, comp::ocs_a500,
				   comp::display(res), comp::palette(kPalette, 0u, 8u))) {
			eng::debug::mark_failed(
				g_eng_run_status,
				kBootCompose + static_cast<u32>(m_display.config_error().code));
			return;
		}
		m_display.takeover(backend);
		m_canvas.set_rasterizer(&playfield::kBlitterRaster);
		m_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kRowBytes) * kHudY + 16u, 16);
		if (!m_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		m_masks_block = backend.memory_manager().chip().reserve<eng::MaskTag>(
			5u * static_cast<u32>(kRowBytes) * kHudY + 16u, 16);
		if (!m_masks_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		m_masks = m_masks_block.data();
		m_fill.backend = &backend;
		m_fill.mask = m_mask.view;
		m_canvas.set_polygon_fill_sink(m_fill.sink());
		m_bg_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(
			kViewsBytes);
		if (!m_bg_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		m_bg = m_bg_block.data();
		build_face_colors();

		for (u8 v = 0u; v < kViews; ++v) draw_background(v);

		reset_level();
		for (u8 b = 0u; b < m_display.buffer_count(); ++b) {
			const eng::PlaneBytes buffer = m_display.buffer(b);
			copy_background(backend, buffer);
			draw_scene(backend, buffer);
		}
		m_ready = true;
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) { // NOLINT
		if (!m_ready) return;
		eng::amiga::poll_input(m_in);
		const eng::input::PadState& pad =
			m_in.pad1.any() ? m_in.pad1 : m_in.pad0;
		bool action = false;

		const bool fire = pad.fire && !m_prev.fire;
		const bool up = pad.up && !m_prev.up;
		const bool down = pad.down && !m_prev.down;
		const bool left = pad.left && !m_prev.left;
		const bool right = pad.right && !m_prev.right;
		const bool any_input = pad.any();
		m_prev = pad;
		if (any_input) {
			m_idle = 0u;
			m_attract = false;
		} else if (m_idle < 0xffffu) {
			++m_idle;
		}
		if (!any_input && m_idle > 300u) m_attract = true;

		if (m_dead || m_won) {
			if (fire) {
				reset_level();
				action = true;
			}
		} else if (fire) {
			shoot();
			action = true;
		} else if (left || right) {
			m_heading = static_cast<u8>((m_heading + (right ? 1u : 3u)) & 3u);
			action = true;
		} else if (up || down) {
			move_player(up ? 1 : -1);
			tick_world();
			action = true;
		}

		if (m_attract) {
			if (m_dead || m_won) {
				if (++m_attract_wait >= 40u) {
					reset_level();
					m_attract_wait = 0u;
				}
				action = true;
			} else if (++m_attract_step >= 8u) {
				m_attract_step = 0u;
				if ((m_attract_phase & 1u) == 0u) {
					shoot();
				} else {
					move_player(-1);
					tick_world();
				}
				++m_attract_phase;
				action = true;
			}
		}

		if (action) {
			m_display.commit();
			const eng::PlaneBytes buffer = m_display.back();
			copy_background(backend, buffer);
			draw_scene(backend, buffer);
			++m_frame;
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	void bind_planes(eng::PlaneBytes planes) noexcept {
		(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(planes.size()),
					kWidth, kHeight, kPlanes, kPlaneBytes);
	}

	void bind_view(u8 view) noexcept {
		(void)m_canvas.bind_raw(m_bg + static_cast<u32>(view) * kViewBytes,
					kViewBytes, kWidth, kViewRows, kPlanes,
					kViewPlaneBytes);
	}

	void draw_background(u8 view) noexcept {
		bind_view(view);
		eng::playfield::Surface s {
			m_canvas, eng::playfield::SurfaceRect {0, 0, kWidth, kHudY}};
		(void)s.fill_rect(0, 0, kWidth, kHudY, kGrayDark);
		(void)s.fill_rect(0, 0, kWidth, 34, kGrayDark);
		(void)s.fill_rect(0, 34, 28, kHudY - 34, kGrayDark);
		(void)s.fill_rect(kWidth - 28, 34, 28, kHudY - 34, kGrayDark);
		(void)s.fill_rect(28, 34, kWidth - 56, 116, kWhite);
		(void)s.fill_rect(28, 34, kWidth - 56, 4, kGrayLight);
		(void)s.fill_rect(28, 146, kWidth - 56, 4, kGrayDark);
		(void)s.fill_rect(28, 150, kWidth - 56, kHudY - 150, kGrayLight);
		(void)s.fill_rect(28, 168, kWidth - 56, 2, kGrayDark);
		(void)s.fill_rect(28, 190, kWidth - 56, 2, kGrayDark);
		(void)s.fill_rect(44, 8, 54, 10, kWhite);
		(void)s.fill_rect(222, 8, 54, 10, kWhite);
		const s16 door_x = static_cast<s16>(136 + static_cast<s16>(view) * 8 - 12);
		(void)s.fill_rect(door_x, 58, 42, 92, kBlack);
		(void)s.fill_rect(door_x - 4, 54, 50, 4, kRedDark);
		(void)s.fill_rect(door_x - 4, 54, 4, 98, kRedDark);
		(void)s.fill_rect(door_x + 42, 54, 4, 98, kRedDark);
		(void)s.fill_rect(door_x + 10, 44, 22, 6, kRedLight);
		(void)s.fill_rect(28, 150, kWidth - 56, 2, kRedDeep);
		(void)s.fill_rect(0, 150, 28, 2, kRedDark);
		(void)s.fill_rect(kWidth - 28, 150, 28, 2, kRedDark);
		// Mobiliario: dos grises y remate claro para dar volumen.
		(void)s.fill_rect(40, 166, 28, 24, kGrayDark);
		(void)s.fill_rect(252, 166, 28, 24, kGrayLight);
		(void)s.fill_rect(40, 166, 28, 3, kGrayLight);
		(void)s.fill_rect(252, 166, 28, 3, kWhite);
		(void)s.fill_rect(40, 190, 28, 2, kBlack);
		(void)s.fill_rect(252, 190, 28, 2, kBlack);
	}

	void copy_background(eng::amiga::AmigaBackend& backend,
			     eng::PlaneBytes buffer) noexcept {
		using ChipAddr = eng::Address<eng::MemoryKind::Chip>;
		const u8* src = m_bg + static_cast<u32>(m_heading) * kViewBytes;
		for (u8 p = 0u; p < kPlanes; ++p) {
			const auto s = eng::graphics::BlitPtr {
				ChipAddr::from_storage(src + static_cast<u32>(p) *
							     kViewPlaneBytes)};
			const auto d = eng::graphics::BlitPtr {
				ChipAddr::from_storage(buffer.data() +
						       static_cast<u32>(p) * kPlaneBytes)};
			(void)backend.blitter_copy_words_strided(
				s, d, kWidth / 16u, kViewRows, 0, 0, false);
		}
		backend.wait_blitter();
	}

	[[nodiscard]] bool project(s16 x, s16 z, s16& sx, s16& base,
				   s16& height, s16& fz_out) const noexcept {
		const s16 dx = static_cast<s16>(x - m_px);
		const s16 dz = static_cast<s16>(z - m_pz);
		s16 fz = 0;
		s16 rx = 0;
		switch (m_heading) {
		case 0u: fz = dz; rx = dx; break;
		case 1u: fz = dx; rx = static_cast<s16>(-dz); break;
		case 2u: fz = static_cast<s16>(-dz); rx = static_cast<s16>(-dx); break;
		default: fz = static_cast<s16>(-dx); rx = dz; break;
		}
		if (fz < 18) return false;
		const s32 x32 = static_cast<s32>(kCx) +
				(static_cast<s32>(rx) * kFocal) / fz;
		const s32 y32 = static_cast<s32>(kCy) +
				(static_cast<s32>(kEyeY) * kFocal) / fz;
		const s32 h32 = (static_cast<s32>(kFocal) * 58) / fz;
		if (x32 < -80 || x32 > kWidth + 80 || y32 < 0) return false;
		sx = static_cast<s16>(x32);
		base = static_cast<s16>(y32);
		height = static_cast<s16>(h32 < 8 ? 8 : h32);
		fz_out = fz;
		return true;
	}

		[[nodiscard]] bool project3(s16 x, s16 y, s16 z, s16& sx, s16& sy,
				   s16& fz_out) const noexcept {
		const s16 dx = static_cast<s16>(x - m_px);
		const s16 dz = static_cast<s16>(z - m_pz);
		s16 fz = 0;
		s16 rx = 0;
		switch (m_heading) {
		case 0u: fz = dz; rx = dx; break;
		case 1u: fz = dx; rx = static_cast<s16>(-dz); break;
		case 2u: fz = static_cast<s16>(-dz); rx = static_cast<s16>(-dx); break;
		default: fz = static_cast<s16>(-dx); rx = dz; break;
		}
		if (fz < 18) return false;
		const s32 x32 = static_cast<s32>(kCx) +
				(static_cast<s32>(rx) * kFocal) / fz;
		const s32 y32 = static_cast<s32>(kCy) +
				(static_cast<s32>(kEyeY - y) * kFocal) / fz;
		sx = static_cast<s16>(x32);
		sy = static_cast<s16>(y32);
		fz_out = fz;
		return true;
	}

	[[nodiscard]] static s32 cross(const Pt& a, const Pt& b,
				       const Pt& c) noexcept {
		return static_cast<s32>(b.x - a.x) * (c.y - a.y) -
		       static_cast<s32>(b.y - a.y) * (c.x - a.x);
	}

	[[nodiscard]] static u8 hull(Pt* p, u8 n, Pt* out) noexcept {
		if (n < 3u) return 0u;
		for (u8 i = 1u; i < n; ++i) {
			const Pt key = p[i];
			u8 j = i;
			while (j > 0u && (p[j - 1u].x > key.x ||
					  (p[j - 1u].x == key.x &&
					   p[j - 1u].y > key.y))) {
				p[j] = p[j - 1u];
				--j;
			}
			p[j] = key;
		}
		u8 k = 0u;
		for (u8 i = 0u; i < n; ++i) {
			while (k >= 2u && cross(out[k - 2u], out[k - 1u], p[i]) <= 0)
				--k;
			out[k++] = p[i];
		}
		u8 lower = static_cast<u8>(k + 1u);
		for (u8 i = static_cast<u8>(n - 1u); i > 0u; --i) {
			while (k >= lower && cross(out[k - 2u], out[k - 1u], p[i - 1u]) <= 0)
				--k;
			out[k++] = p[i - 1u];
		}
		return static_cast<u8>(k - 1u);
	}

	[[nodiscard]] bool part_visible(const Part3& p, s16& fz) const noexcept {
		s32 cx = 0;
		s32 cy = 0;
		s32 cz = 0;
		if (p.n == 0u) return false;
		for (u8 i = 0u; i < p.n; ++i) {
			cx += p.x[i];
			cy += p.y[i];
			cz += p.z[i];
		}
		s16 sx = 0, sy = 0;
		return project3(static_cast<s16>(cx / p.n), static_cast<s16>(cy / p.n),
				static_cast<s16>(cz / p.n), sx, sy, fz);
	}

	static void part_from_box(const Box3& b, Part3& p) noexcept {
		const s16 xs[8] = {b.x0, b.x1, b.x1, b.x0, b.x0, b.x1, b.x1, b.x0};
		const s16 ys[8] = {b.y0, b.y0, b.y1, b.y1, b.y0, b.y0, b.y1, b.y1};
		const s16 zs[8] = {b.z0, b.z0, b.z0, b.z0, b.z1, b.z1, b.z1, b.z1};
		for (u8 i = 0u; i < 8u; ++i) {
			p.x[i] = xs[i];
			p.y[i] = ys[i];
			p.z[i] = zs[i];
		}
		p.color = b.color;
		p.n = 8u;
	}

	void draw_part(const Part3& p) noexcept {
		Pt pts[16] {};
		for (u8 i = 0u; i < p.n; ++i) {
			s16 fz = 0;
			if (!project3(p.x[i], p.y[i], p.z[i], pts[i].x, pts[i].y, fz))
				return;
		}
		Pt hull_pts[16] {};
		const u8 hn = hull(pts, p.n, hull_pts);
		if (hn < 3u) return;
		s16 hx[16] {};
		s16 hy[16] {};
		for (u8 i = 0u; i < hn; ++i) {
			hx[i] = hull_pts[i].x;
			hy[i] = hull_pts[i].y;
		}
		eng::playfield::Surface s {
			m_canvas, eng::playfield::SurfaceRect {0, 0, kWidth, kHudY}};
		(void)s.fill_polygon(hx, hy, hn, p.color);
	}

	[[nodiscard]] static Box3 cover_box(u8 i) noexcept {
		if (i == 0u) return Box3 {76, 0, 160, 104, 34, 184, kGrayDark};
		return Box3 {156, 0, 160, 184, 34, 184, kGrayLight};
	}

	[[nodiscard]] static BoneFrame child_frame(const BoneFrame& parent, s16 px,
						   s16 py, s16 pz, s16 c,
						   s16 s) noexcept {
		return BoneFrame {
			static_cast<s16>(parent.x + ((px * parent.c - py * parent.s) >> 8)),
			static_cast<s16>(parent.y + ((px * parent.s + py * parent.c) >> 8)),
			static_cast<s16>(parent.z + pz), c, s};
	}

	/// Color por cara calculado UNA vez desde la normal de rest y una luz fija.
	/// No hay iluminación por frame: los tres rojos (oscuro/medio/claro) y los
	/// dos grises quedan fijados con coherencia espacial.
	void build_face_colors() noexcept {
		for (u16 i = 0u; i < kBodyFaceCount; ++i) {
			const BodyFace& f = kBodyFaces[i];
			const MeshVertex& a = kBody[f.a];
			const MeshVertex& b = kBody[f.b];
			const MeshVertex& c = kBody[f.c];
			const s32 ux = b.x - a.x;
			const s32 uy = b.y - a.y;
			const s32 uz = b.z - a.z;
			const s32 vx = c.x - a.x;
			const s32 vy = c.y - a.y;
			const s32 vz = c.z - a.z;
			s32 nx = uy * vz - uz * vy;
			s32 ny = uz * vx - ux * vz;
			s32 nz = ux * vy - uy * vx;
			const s32 fcx = (a.x + b.x + c.x) / 3;
			const s32 fcy = (a.y + b.y + c.y) / 3;
			const s32 fcz = (a.z + b.z + c.z) / 3;
			const s32 bcx = (kBoneRestX[a.bone] + kBoneRestX[b.bone] +
					 kBoneRestX[c.bone]) / 3;
			const s32 bcy = (kBoneRestY[a.bone] + kBoneRestY[b.bone] +
					 kBoneRestY[c.bone]) / 3;
			const bool flip = nx * (fcx - bcx) + ny * (fcy - bcy) +
						  nz * fcz <
					  0;
			m_face_swap[i] = flip;
			if (flip) {
				nx = -nx;
				ny = -ny;
				nz = -nz;
			}
			const s32 ax = nx < 0 ? -nx : nx;
			const s32 ay = ny < 0 ? -ny : ny;
			const s32 az = nz < 0 ? -nz : nz;
			const s32 mag = ax > ay ? (ax > az ? ax : az)
						: (ay > az ? ay : az);
			const s32 light = mag == 0
				? 0
				: ((2 * ny - nx + nz) * 256) / mag;
			u8 color = kRed;
			if (f.base >= 4u) {
				color = light > 0 ? kGrayLight : kGrayDark;
			} else if (f.base == 1u) {
				// Cabeza: el tono más claro, arriba y hacia la luz.
				color = kRedLight;
			} else if (f.base == 3u) {
				// Visor: el rojo más oscuro, en la zona de sombra.
				color = kRedDark;
			} else {
				// Torso: rojo medio; los grises conservan su
				// gradación estática por normal.
				color = kRed;
			}
			m_face_color[i] = color;
		}
	}

	void pose_vertex(const BoneFrame* f, u8 bone, const MeshVertex& v,
			 s16& x, s16& y, s16& z) const noexcept {
		const BoneFrame& b = f[bone];
		const s16 lx = static_cast<s16>(v.x - kBoneRestX[bone]);
		const s16 ly = static_cast<s16>(v.y - kBoneRestY[bone]);
		x = static_cast<s16>(b.x + ((lx * b.c - ly * b.s) >> 8));
		y = static_cast<s16>(b.y + ((lx * b.s + ly * b.c) >> 8));
		// El modelo usa +Z como delante; la cámara mira desde -Z.
		z = static_cast<s16>(b.z - v.z);
	}

	void pose_enemy_vertices(const Enemy& e, s16* xs, s16* ys, s16* zs) const noexcept {
		static constexpr s16 kSwingC[8] = {
			256, 247, 222, 247, 256, 247, 222, 247};
		static constexpr s16 kSwingS[8] = {
			0, 66, 128, 66, 0, -66, -128, -66};
		const u8 phase = static_cast<u8>(e.anim & 7u);
		const s16 c = kSwingC[phase];
		const s16 s = kSwingS[phase];
		const s16 swing = s < 0 ? static_cast<s16>(-s) : s;
		const bool aim = e.aim != 0u;
		const s16 arm_c = aim ? 181 : c;
		const s16 arm_l_s = aim ? static_cast<s16>(-181) : s;
		const s16 arm_r_s = aim ? 181 : s;
		const s16 bob = (phase == 1u || phase == 2u || phase == 5u ||
				 phase == 6u)
					? 1
					: 0;
		BoneFrame f[6] {};
		f[0] = BoneFrame {e.x, static_cast<s16>(16 + bob), e.z, 256, 0};
		f[1] = child_frame(f[0], 0, 14, 0, 256, 0);
		f[2] = child_frame(f[0], -12, 12, 0, arm_c, arm_l_s);
		f[3] = child_frame(f[0], 12, 12, 0, arm_c, arm_r_s);
		f[4] = child_frame(f[0], -5, 0, 0, c,
				   static_cast<s16>(-swing));
		f[5] = child_frame(f[0], 5, 0, 0, c, swing);
		for (u8 i = 0u; i < 55u; ++i) {
			const MeshVertex& v = kBody[i];
			s16 x = 0, y = 0, z = 0;
			pose_vertex(f, v.bone, v, x, y, z);
			const SkinWeight sw = skin_weight(i);
			if (sw.secondary != 0xFFu && sw.weight != 0u) {
				s16 sx = 0, sy = 0, sz = 0;
				pose_vertex(f, sw.secondary, v, sx, sy, sz);
				const s32 w = sw.weight;
				x = static_cast<s16>(((x * (256 - w)) + sx * w) >> 8);
				y = static_cast<s16>(((y * (256 - w)) + sy * w) >> 8);
				z = static_cast<s16>(((z * (256 - w)) + sz * w) >> 8);
			}
			xs[i] = x;
			ys[i] = y;
			zs[i] = z;
		}
	}

	/// Rasteriza la malla exacta en un buffer de color por píxel (pintor por
	/// cara, de lejos a cerca), y luego materializa cada color con una sola
	/// máscara + un cookie-cut por plano.
	void draw_characters(eng::amiga::AmigaBackend& backend) noexcept {
		u8* skin = m_skin;
		for (u32 i = 0u; i < static_cast<u32>(kWidth) * kHudY; ++i)
			skin[i] = 0u;
		FaceDraw faces[kBodyFaceCount * kEnemies] {};
		u16 n = 0u;
		for (u8 e = 0u; e < kEnemies; ++e) {
			if (!m_enemies[e].alive) continue;
			s16 xs[55] {};
			s16 ys[55] {};
			s16 zs[55] {};
			pose_enemy_vertices(m_enemies[e], xs, ys, zs);
			for (u16 fi = 0u; fi < kBodyFaceCount; ++fi) {
				const BodyFace& f = kBodyFaces[fi];
				const u8 ia = f.a;
				const u8 ib = m_face_swap[fi] ? f.c : f.b;
				const u8 ic = m_face_swap[fi] ? f.b : f.c;
				s16 fz[3] {};
				bool ok = true;
				FaceDraw fd {};
				for (u8 k = 0u; k < 3u; ++k) {
					const u8 vi = k == 0u ? ia : (k == 1u ? ib : ic);
					s16 fzv = 0;
					if (!project3(xs[vi], ys[vi], zs[vi], fd.p[k].x,
						      fd.p[k].y, fzv)) {
						ok = false;
						break;
					}
					fz[k] = fzv;
				}
				if (!ok) continue;
				const s32 ux = static_cast<s32>(xs[ib]) - xs[ia];
				const s32 uy = static_cast<s32>(ys[ib]) - ys[ia];
				const s32 uz = static_cast<s32>(zs[ib]) - zs[ia];
				const s32 vx = static_cast<s32>(xs[ic]) - xs[ia];
				const s32 vy = static_cast<s32>(ys[ic]) - ys[ia];
				const s32 vz = static_cast<s32>(zs[ic]) - zs[ia];
				const s32 nx = uy * vz - uz * vy;
				const s32 ny = uz * vx - ux * vz;
				const s32 nz = ux * vy - uy * vx;
				const s32 fcx = (static_cast<s32>(xs[ia]) + xs[ib] +
						 xs[ic]) /
						3;
				const s32 fcy = (static_cast<s32>(ys[ia]) + ys[ib] +
						 ys[ic]) /
						3;
				const s32 fcz = (static_cast<s32>(zs[ia]) + zs[ib] +
						 zs[ic]) /
						3;
				if (nx * (static_cast<s32>(m_px) - fcx) +
					    ny * (kEyeY - fcy) +
					    nz * (static_cast<s32>(m_pz) - fcz) <=
				    0)
					continue;
				fd.fz = static_cast<s16>(
					(static_cast<s32>(fz[0]) + fz[1] + fz[2]) / 3);
				fd.color = m_face_color[fi];
				faces[n++] = fd;
			}
		}
		for (u16 i = 1u; i < n; ++i) {
			const FaceDraw key = faces[i];
			u16 j = i;
			while (j > 0u && faces[j - 1u].fz < key.fz) {
				faces[j] = faces[j - 1u];
				--j;
			}
			faces[j] = key;
		}
		for (u16 i = 0u; i < n; ++i) {
			const FaceDraw& fd = faces[i];
			const s32 px[3] = {fd.p[0].x, fd.p[1].x, fd.p[2].x};
			const s32 py[3] = {fd.p[0].y, fd.p[1].y, fd.p[2].y};
			eng::math3d::convex_spans(
				eng::Span<const s32>(px, 3u),
				eng::Span<const s32>(py, 3u),
				[&](s32 y, s32 xl, s32 xr) {
					if (y < 0 || y >= static_cast<s32>(kHudY)) return;
					if (xl < 0) xl = 0;
					if (xr >= static_cast<s32>(kWidth))
						xr = static_cast<s32>(kWidth) - 1;
					if (xl > xr) return;
					u8* row = skin + static_cast<u32>(y) * kWidth;
					for (s32 x = xl; x <= xr; ++x)
						row[x] = fd.color;
				});
		}
		static constexpr u8 kColors[5] = {kRedLight, kRed, kRedDark,
						  kGrayLight, kGrayDark};
		const u32 mask_bytes = static_cast<u32>(kRowBytes) * kHudY;
		for (u8 ci = 0u; ci < 5u; ++ci) {
			for (u32 i = 0u; i < mask_bytes; ++i)
				m_masks[static_cast<u32>(ci) * mask_bytes + i] = 0u;
		}
		s16 bx0[5] = {32767, 32767, 32767, 32767, 32767};
		s16 by0[5] = {32767, 32767, 32767, 32767, 32767};
		s16 bx1[5] = {-1, -1, -1, -1, -1};
		s16 by1[5] = {-1, -1, -1, -1, -1};
		for (u16 y = 0u; y < kHudY; ++y) {
			const u8* row = skin + static_cast<u32>(y) * kWidth;
			for (u16 x = 0u; x < kWidth; ++x) {
				const u8 col = row[x];
				if (col == 0u) continue;
				u8 ci = 0u;
				for (; ci < 5u; ++ci) {
					if (kColors[ci] == col) break;
				}
				if (ci >= 5u) continue;
				if (x < bx0[ci]) bx0[ci] = x;
				if (x > bx1[ci]) bx1[ci] = x;
				if (y < by0[ci]) by0[ci] = y;
				if (y > by1[ci]) by1[ci] = y;
				m_masks[static_cast<u32>(ci) * mask_bytes + static_cast<u32>(y) * kRowBytes +
					     (x >> 3)] |= static_cast<u8>(
					0x80u >> (x & 7u));
			}
		}
		for (u8 ci = 0u; ci < 5u; ++ci) {
			if (bx1[ci] < bx0[ci]) continue;
			(void)m_fill.backend->blit_fill_from_mask(
				eng::MaskBytes(m_masks + static_cast<u32>(ci) * mask_bytes, mask_bytes),
				m_draw_planes, kPlanes, kRowBytes, kPlaneBytes, bx0[ci], by0[ci],
				static_cast<u16>(bx1[ci] - bx0[ci] + 1),
				static_cast<u16>(by1[ci] - by0[ci] + 1), kColors[ci]);
		}
	}

	void draw_covers() noexcept {
		for (u8 i = 0u; i < 2u; ++i) {
			Part3 p {};
			part_from_box(cover_box(i), p);
			s16 fz = 0;
			if (part_visible(p, fz)) {
				p.fz = fz;
				draw_part(p);
			}
		}
	}
	[[nodiscard]] bool los_blocked(s16 ex, s16 ez) const noexcept {
		for (u8 step = 1u; step < 16u; ++step) {
			const s16 x = static_cast<s16>(
				m_px + (static_cast<s32>(ex - m_px) * step) / 16);
			const s16 z = static_cast<s16>(
				m_pz + (static_cast<s32>(ez - m_pz) * step) / 16);
			for (u8 c = 0u; c < 2u; ++c) {
				const Box3 b = cover_box(c);
				if (x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1)
					return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool enemy_on_screen(const Enemy& e, s16& sx, s16& base,
					   s16& height) const noexcept {
		s16 fz = 0;
		return e.alive && project(e.x, e.z, sx, base, height, fz);
	}

	void draw_scene(eng::amiga::AmigaBackend& backend,
			eng::PlaneBytes buffer) noexcept {
		bind_planes(buffer);
		m_draw_planes = buffer;
		draw_characters(backend);
		draw_covers();
		for (u16 i = 0u; i < kBulletSlots; ++i) draw_bullet(backend, m_bullets[i]);
		draw_gun_and_crosshair();
		draw_hud();
		backend.wait_blitter();
	}

	void draw_bullet(eng::amiga::AmigaBackend& backend,
			 const Bullet& b) noexcept {
		if (!b.active) return;
		s16 sx = 0, base = 0, h = 0, fz = 0;
		if (!project(b.x, b.z, sx, base, h, fz)) return;
		const s16 size = static_cast<s16>(clamp_i32(4, 12, 220 / fz));
		fill_rect(backend, static_cast<s16>(sx - size / 2),
			  static_cast<s16>(base - h / 2 - size / 2), size, size);
	}

	void fill_rect(eng::amiga::AmigaBackend& backend, s16 x, s16 y, s16 w,
		       s16 h) noexcept {
		if (w <= 0 || h <= 0) return;
		(void)backend.blitter_fill_rect(
			m_draw_planes.data(), kPlanes, kPlaneBytes, kRowBytes,
			kRowBytes, kWidth, kHudY, x, y, static_cast<u16>(w),
			static_cast<u16>(h), kRedLight, false);
	}

	[[nodiscard]] static s16 clamp_i32(s32 lo, s32 hi, s32 v) noexcept {
		return static_cast<s16>(v < lo ? lo : (v > hi ? hi : v));
	}

	void draw_gun_and_crosshair() noexcept {
		eng::playfield::Surface s {
			m_canvas, eng::playfield::SurfaceRect {0, 0, kWidth, kHudY}};
		const s16 gun_x[6] = {132, 188, 184, 168, 160, 140};
		const s16 gun_y[6] = {208, 208, 182, 174, 174, 182};
		(void)s.fill_polygon(gun_x, gun_y, kBlack);
		const s16 slide_x[4] = {148, 174, 170, 152};
		const s16 slide_y[4] = {178, 178, 160, 160};
		(void)s.fill_polygon(slide_x, slide_y, kGrayLight);
		const s16 sight_x[3] = {156, 166, 161};
		const s16 sight_y[3] = {166, 166, 156};
		(void)s.fill_polygon(sight_x, sight_y, kWhite);
		const s16 c1x[3] = {156, 160, 160};
		const s16 c1y[3] = {108, 104, 108};
		const s16 c2x[3] = {164, 160, 160};
		const s16 c2y[3] = {108, 104, 108};
		const s16 c3x[3] = {156, 160, 160};
		const s16 c3y[3] = {100, 104, 104};
		const s16 c4x[3] = {164, 160, 160};
		const s16 c4y[3] = {100, 104, 104};
		(void)s.fill_polygon(c1x, c1y, kBlack);
		(void)s.fill_polygon(c2x, c2y, kBlack);
		(void)s.fill_polygon(c3x, c3y, kWhite);
		(void)s.fill_polygon(c4x, c4y, kWhite);
	}

	void draw_hud() noexcept {
		eng::playfield::Surface hud {
			m_canvas, eng::playfield::SurfaceRect {
				0, static_cast<s16>(kHudY), kWidth,
				static_cast<s16>(kHudH)}};
		(void)hud.fill_rect(0, kHudY, kWidth, kHudH, kBlack);
		(void)hud.fill_rect(0, kHudY, 4, kHudH, kRed);
		(void)hud.fill_rect(4, kHudY, kWidth - 4, 1, kWhite);
		const s16 logo_x[4] = {10, 22, 30, 18};
		const s16 logo_y[4] = {214, 214, 224, 224};
		(void)hud.fill_polygon(logo_x, logo_y, kRedLight);
		for (u16 i = 0u; i < kEnemies; ++i) {
			const s16 cx = static_cast<s16>(40 + i * 14);
			const s16 dx[4] = {cx, static_cast<s16>(cx + 5),
					   cx, static_cast<s16>(cx - 5)};
			const s16 dy[4] = {214, 220, 226, 220};
			(void)hud.fill_polygon(dx, dy,
					       m_enemies[i].alive ? kRedLight : kGrayDark);
		}
		const s16 state_x[4] = {282, 308, 302, 288};
		const s16 state_y[4] = {214, 214, 226, 226};
		(void)hud.fill_polygon(state_x, state_y,
				       m_dead ? kGrayDark : (m_won ? kWhite : kRed));
		const s16 step_x[4] = {282, 308, 302, 288};
		const s16 step_y[4] = {228, 228, 232, 232};
		(void)hud.fill_polygon(step_x, step_y, kGrayDark);
	}
	void move_player(s16 dir) noexcept {
		switch (m_heading) {
		case 0u: m_pz = static_cast<s16>(m_pz + dir * kStep); break;
		case 1u: m_px = static_cast<s16>(m_px + dir * kStep); break;
		case 2u: m_pz = static_cast<s16>(m_pz - dir * kStep); break;
		default: m_px = static_cast<s16>(m_px - dir * kStep); break;
		}
		m_px = static_cast<s16>(m_px < kRoomMin ? kRoomMin :
					(m_px > kRoomMax ? kRoomMax : m_px));
		m_pz = static_cast<s16>(m_pz < kRoomMin ? kRoomMin :
					(m_pz > kRoomMax ? kRoomMax : m_pz));
	}

	void tick_world() noexcept {
		for (u16 i = 0u; i < kEnemies; ++i) {
			Enemy& e = m_enemies[i];
			if (!e.alive) continue;
			const s16 dx = static_cast<s16>(m_px - e.x);
			const s16 dz = static_cast<s16>(m_pz - e.z);
			if (dx > kEnemyStep) e.x = static_cast<s16>(e.x + kEnemyStep);
			else if (dx < -kEnemyStep) e.x = static_cast<s16>(e.x - kEnemyStep);
			else e.x = m_px;
			if (dz > kEnemyStep) e.z = static_cast<s16>(e.z + kEnemyStep);
			else if (dz < -kEnemyStep) e.z = static_cast<s16>(e.z - kEnemyStep);
			else e.z = m_pz;

			e.anim = static_cast<u8>((e.anim + 1u) & 7u);
			s16 sx = 0, base = 0, h = 0;
			const bool can_see = enemy_on_screen(e, sx, base, h) &&
					     sx > 0 && sx < static_cast<s16>(kWidth) &&
					     !los_blocked(e.x, e.z);
			e.aim = can_see ? 1u : 0u;
			if (e.cooldown != 0u) --e.cooldown;
			if (can_see && e.cooldown == 0u) {
				spawn_bullet(e.x, e.z);
				e.cooldown = 2u;
			}
		}
		for (u16 i = 0u; i < kBulletSlots; ++i) {
			Bullet& b = m_bullets[i];
			if (!b.active) continue;
			const s16 dx = static_cast<s16>(m_px - b.x);
			const s16 dz = static_cast<s16>(m_pz - b.z);
			if (dx > kBulletStep) b.x = static_cast<s16>(b.x + kBulletStep);
			else if (dx < -kBulletStep) b.x = static_cast<s16>(b.x - kBulletStep);
			else b.x = m_px;
			if (dz > kBulletStep) b.z = static_cast<s16>(b.z + kBulletStep);
			else if (dz < -kBulletStep) b.z = static_cast<s16>(b.z - kBulletStep);
			else b.z = m_pz;
			const s16 ddx = static_cast<s16>(m_px - b.x);
			const s16 ddz = static_cast<s16>(m_pz - b.z);
			if (static_cast<u32>(ddx * ddx + ddz * ddz) <=
			    static_cast<u32>(kPlayerRadius * kPlayerRadius)) {
				m_dead = true;
				b.active = false;
			}
		}
	}

	void shoot() noexcept {
		s16 best = 0x7fff;
		u16 hit = kEnemies;
		for (u16 i = 0u; i < kEnemies; ++i) {
			s16 sx = 0, base = 0, h = 0, fz = 0;
			if (!project(m_enemies[i].x, m_enemies[i].z, sx, base, h, fz))
				continue;
			const s16 tol = static_cast<s16>(h / 2 + 4);
			const s16 dx = static_cast<s16>(sx - kCx);
			if (dx < -tol || dx > tol) continue;
			if (los_blocked(m_enemies[i].x, m_enemies[i].z)) continue;
			if (fz < best) {
				best = fz;
				hit = i;
			}
		}
		if (hit < kEnemies) {
			m_enemies[hit].alive = false;
			m_won = true;
			for (u16 i = 0u; i < kEnemies; ++i) {
				if (m_enemies[i].alive) m_won = false;
			}
		}
	}

	void spawn_bullet(s16 x, s16 z) noexcept {
		for (u16 i = 0u; i < kBulletSlots; ++i) {
			if (m_bullets[i].active) continue;
			m_bullets[i] = Bullet {true, x, z};
			return;
		}
	}

	void reset_level() noexcept {
		m_px = 128;
		m_pz = 96;
		m_heading = 0u;
		m_dead = false;
		m_won = false;
		for (u16 i = 0u; i < kBulletSlots; ++i) m_bullets[i] = {};
		m_enemies[0] = Enemy {true, 128, 224, 1u, 0u, 0u};
		m_enemies[1] = Enemy {true, 80, 220, 2u, 2u, 0u};
		m_enemies[2] = Enemy {true, 176, 220, 3u, 5u, 0u};
	}

	static constexpr u16 kEnemies = 3u;

	bool m_ready = false;
	u16 m_frame = 0u;
	s16 m_px = 128;
	s16 m_pz = 96;
	u8 m_heading = 0u;
	bool m_dead = false;
	bool m_won = false;
	comp::Scene m_display {};
	eng::playfield::ContiguousPlayfield m_canvas {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_mask {};
	eng::amiga::PolygonFillService m_fill {};
	eng::PlaneBytes m_draw_planes {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bg_block {};
	u8* m_bg = nullptr;
	u8 m_skin[static_cast<u32>(kWidth) * kHudY] {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_masks_block {};
	u8* m_masks = nullptr;
	u8 m_face_color[kBodyFaceCount] {};
	bool m_face_swap[kBodyFaceCount] {};
	Enemy m_enemies[kEnemies] {};
	Bullet m_bullets[kBulletSlots] {};
	eng::input::InputAggregator m_in {};
	eng::input::PadState m_prev {};
	u16 m_idle = 0u;
	u8 m_attract_step = 0u;
	u8 m_attract_phase = 0u;
	u8 m_attract_wait = 0u;
	bool m_attract = false;
};

Game g_game {};

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
