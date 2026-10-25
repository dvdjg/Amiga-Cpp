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
	s16 x[8] {};
	s16 y[8] {};
	s16 z[8] {};
	s16 fz = 0;
	u8 color = kRed;
};

/// Pieza de la malla única del personaje. `bone` identifica el hueso que la
/// transforma; todas las piezas comparten un solo presupuesto de vértices y un
/// solo dibujo de personaje.
struct RigPart {
	s16 x0 = 0, y0 = 0, z0 = 0;
	s16 x1 = 0, y1 = 0, z1 = 0;
	u8 color = kRed;
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
		for (u8 i = 0u; i < 8u; ++i) {
			cx += p.x[i];
			cy += p.y[i];
			cz += p.z[i];
		}
		s16 sx = 0, sy = 0;
		return project3(static_cast<s16>(cx / 8), static_cast<s16>(cy / 8),
				static_cast<s16>(cz / 8), sx, sy, fz);
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
	}

	void draw_part(const Part3& p) noexcept {
		Pt pts[8] {};
		for (u8 i = 0u; i < 8u; ++i) {
			s16 fz = 0;
			if (!project3(p.x[i], p.y[i], p.z[i], pts[i].x, pts[i].y, fz))
				return;
		}
		Pt hull_pts[16] {};
		const u8 hn = hull(pts, 8u, hull_pts);
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

	void draw_world() noexcept {
		Part3 items[32] {};
		u8 n = 0u;
		for (u8 i = 0u; i < 2u; ++i) {
			Part3 p {};
			part_from_box(cover_box(i), p);
			s16 fz = 0;
			if (part_visible(p, fz) && n < 32u) {
				p.fz = fz;
				items[n++] = p;
			}
		}
		for (u8 e = 0u; e < kEnemies; ++e) {
			if (!m_enemies[e].alive) continue;
			Part3 parts[7] {};
			enemy_mesh(m_enemies[e], parts);
			for (u8 p = 0u; p < 7u && n < 32u; ++p) {
				s16 fz = 0;
				if (part_visible(parts[p], fz)) {
					parts[p].fz = fz;
					items[n++] = parts[p];
				}
			}
		}
		for (u8 i = 1u; i < n; ++i) {
			const Part3 key = items[i];
			u8 j = i;
			while (j > 0u && items[j - 1u].fz < key.fz) {
				items[j] = items[j - 1u];
				--j;
			}
			items[j] = key;
		}
		for (u8 i = 0u; i < n; ++i) draw_part(items[i]);
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

	/// Malla única del personaje: todas las piezas se transforman con el mismo
	/// esqueleto y se dibujan como un solo cuerpo. Los brazos y piernas giran en
	/// el plano de la cámara; la fase `Enemy::anim` avanza con cada tick de tiempo.
	void enemy_mesh(const Enemy& e, Part3* out) const noexcept {
		static constexpr RigPart kRig[7] = {
			{-8, 0, -5, 8, 28, 5, kRed, 0},       // torso
			{-5, 0, -5, 5, 10, 5, kRedLight, 1},  // cabeza
			{-4, 3, -6, 4, 7, -5, kRedDark, 1},   // visor
			{-5, -24, -4, 0, 0, 4, kGrayLight, 2},// brazo izquierdo
			{0, -24, -4, 5, 0, 4, kGrayLight, 3}, // brazo derecho
			{-4, -26, -4, 0, 0, 4, kGrayDark, 4}, // pierna izquierda
			{0, -26, -4, 4, 0, 4, kGrayDark, 5},  // pierna derecha
		};
		static constexpr s16 kSwingC[8] = {
			256, 247, 222, 247, 256, 247, 222, 247};
		static constexpr s16 kSwingS[8] = {
			0, 66, 128, 66, 0, -66, -128, -66};
		const u8 phase = static_cast<u8>(e.anim & 7u);
		const s16 c = kSwingC[phase];
		const s16 s = kSwingS[phase];
		const s16 bob = (phase == 1u || phase == 2u || phase == 5u ||
				 phase == 6u)
					? 1
					: 0;
		const s16 swing = s < 0 ? static_cast<s16>(-s) : s;
		const s16 arm_c = e.aim != 0u ? 181 : c;
		const s16 arm_s = e.aim != 0u ? 181 : swing;
		BoneFrame f[6] {};
		f[0] = BoneFrame {e.x, static_cast<s16>(26 + bob), e.z, 256, 0};
		f[1] = child_frame(f[0], 0, 28, 0, 256, 0);
		f[2] = child_frame(f[0], -8, 24, 0, arm_c,
				   static_cast<s16>(-arm_s));
		f[3] = child_frame(f[0], 8, 24, 0, arm_c, arm_s);
		f[4] = child_frame(f[0], -4, 0, 0, c,
				   static_cast<s16>(-swing));
		f[5] = child_frame(f[0], 4, 0, 0, c, swing);
		for (u8 i = 0u; i < 7u; ++i) {
			const RigPart& r = kRig[i];
			const BoneFrame& b = f[r.bone];
			const s16 px[8] = {r.x0, r.x1, r.x1, r.x0,
					   r.x0, r.x1, r.x1, r.x0};
			const s16 py[8] = {r.y0, r.y0, r.y1, r.y1,
					   r.y0, r.y0, r.y1, r.y1};
			const s16 pz[8] = {r.z0, r.z0, r.z0, r.z0,
					   r.z1, r.z1, r.z1, r.z1};
			for (u8 v = 0u; v < 8u; ++v) {
				out[i].x[v] = static_cast<s16>(
					b.x + ((px[v] * b.c - py[v] * b.s) >> 8));
				out[i].y[v] = static_cast<s16>(
					b.y + ((px[v] * b.s + py[v] * b.c) >> 8));
				out[i].z[v] = static_cast<s16>(b.z + pz[v]);
			}
			out[i].color = r.color;
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
		draw_world();
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
