// ============================================================================
// Test HOST-481: renderer 3D de dominio — clip de cámara y emisión de caras.
// ============================================================================
//
// Cubre el paquete 1 de `3D_GAME_ARCHITECTURE.md` en su parte pura:
//   - `clip_camera_polygon`: interior intacto, near, planos laterales, esquinas,
//     completamente fuera/detrás, y saturación de coordenadas.
//   - Invariantes del contrato: tras el clip todo vértice cumple `z >= near` y
//     `|x|·focal <= z·half_w`, `|y|·focal <= z·half_h`; la proyección queda dentro del
//     viewport (±1 px); nunca se emite una cara degenerada o trasera.
//   - `render_mesh`: emisión por `Surface::fill_polygon` (el camino que en Amiga usa el
//     Blitter por `PolygonFillSink`), culling por área, winding CCW, doble cara y
//     `solid_depth_key`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/481_render3d_clip

#include <eng/core/math/fixed.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/memory/mem_bank.hpp>

#include <cstdio>
#include <vector>

using namespace eng;
using namespace eng::playfield;
using namespace eng::graphics;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

using CS = float;
using Cam3 = Camera3D<CS>;
using CV = CamVertex<CS>;
constexpr Cam3 kCam {4096.0f, 160, 100, 64.0f, 160.0f, 100.0f};
using CC = eng::math::scalar_traits<eng::coord>;
using MF = eng::math::Fixed<eng::s16, 0>;
using CTF = eng::math::scalar_traits<MF>;
constexpr Camera3D<MF> kCamF {CTF::from_int(4096), 160, 100, CTF::from_int(64),
			      CTF::from_int(160), CTF::from_int(100)};

/// Playfield de prueba con relleno observable: captura los polígonos que `Surface`
/// delega (el mismo borde que usa el Blitter en Amiga).
struct RecordingPlayfield : Playfield {
	std::vector<u8> mem;
	struct Poly {
		std::vector<s16> xs;
		std::vector<s16> ys;
		u8 n = 0;
		u8 color = 0;
	};
	std::vector<Poly> polys;

	void init(u16 w, u16 h, u8 p) {
		m_width = w;
		m_height = h;
		m_planes = p;
		m_bytes_per_row = static_cast<u16>((w / 8u) & ~1u);
		m_total_bytes = static_cast<u32>(m_bytes_per_row) * m_planes * h;
		mem.assign(m_total_bytes, 0);
		eng::MemBank<eng::MemoryKind::Chip> bank {};
		bank.configure(mem.data(), static_cast<eng::u32>(mem.size()), 2u);
		m_frontbuffer = bank.reserve<eng::PlaneTag>(m_total_bytes, 2u).address();
		m_initialized = true;
	}
	u8 planes_count() const { return m_planes; }
	u32 planeline_for(s32 wy) const override { return static_cast<u32>(wy) * m_planes; }
	u32 byte_for(s32 wx) const override { return static_cast<u32>(wx) / 8u & ~1u; }
	bool in_bounds(s32 wx, s32 wy) const override {
		return wx >= 0 && wy >= 0 && static_cast<u32>(wx) < m_width &&
		       static_cast<u32>(wy) < m_height;
	}
	PlayfieldHardwareView hardware_view() const override {
		PlayfieldHardwareView v {};
		v.planes = m_planes;
		v.bitmap_bytes_per_row = m_bytes_per_row;
		return v;
	}
	bool add_world_bitmap(graphics::FramePlan&, Span<const u16>, s32, s32, u16, u16, u16, u32,
			      u8, u8, bool, RasterOp) override {
		return false;
	}
	bool add_world_bitmap_masked(graphics::FramePlan&, Span<const u16>, Span<const u16>, s32,
				     s32, u16, u16, u16, u32, u8, u8) override {
		return false;
	}
	bool fill_polygon(Span<const s16> xs, Span<const s16> ys, u8 color) override {
		Poly p {};
		p.n = static_cast<u8>(xs.size());
		p.color = color;
		for (u8 i = 0u; i < p.n; ++i) {
			p.xs.push_back(xs[i]);
			p.ys.push_back(ys[i]);
		}
		polys.push_back(p);
		return true;
	}
};

/// Invariante del contrato en el otro sentido: |x|·focal <= z·half (sin ambigüedad).
bool plane_ok(const CV& v) {
	if (v.z < kCam.near_z) return false;
	const CS ax = v.x >= 0.0f ? v.x : -v.x;
	const CS ay = v.y >= 0.0f ? v.y : -v.y;
	return ax * kCam.focal <= v.z * kCam.half_w && ay * kCam.focal <= v.z * kCam.half_h;
}

} // namespace

int main() {
	// 1) Polígono interior: el clip no lo toca (mismo nº y coordenadas).
	{
		const CV in[4] = {{-20, -20, 1000}, {20, -20, 1000}, {20, 20, 1000}, {-20, 20, 1000}};
		CV out[16] {};
		const u8 n = clip_camera_polygon(in, 4u, kCam, out, 16u);
		check(n == 4u, "interior: 4 vértices");
		bool same = true;
		for (u8 i = 0u; i < n; ++i) {
			same = same && out[i].x == in[i].x && out[i].y == in[i].y && out[i].z == in[i].z;
		}
		check(same, "interior: sin cambios");
	}

	// 2) Cruce del near: el vértice de atrás se sustituye por dos cortes a z = near.
	{
		const CV in[3] = {{-10, -10, 1000}, {10, -10, 1000}, {0, 10, -500}};
		CV out[16] {};
		const u8 n = clip_camera_polygon(in, 3u, kCam, out, 16u);
		check(n >= 3u, "near: queda polígono recortado");
		bool ok = true;
		bool saw_near = false;
		for (u8 i = 0u; i < n; ++i) {
			ok = ok && plane_ok(out[i]);
			if (out[i].z <= kCam.near_z + 64.0f) saw_near = true;
		}
		check(ok, "near: invariantes");
		check(saw_near, "near: vértices nuevos en el plano");
	}

	// 3) Cruce del plano lateral izquierdo: todo el resultado queda dentro.
	{
		const CV in[3] = {{-10000, -10, 1000}, {10, -10, 1000}, {10, 10, 1000}};
		CV out[16] {};
		const u8 n = clip_camera_polygon(in, 3u, kCam, out, 16u);
		check(n >= 3u, "lateral: queda polígono");
		bool ok = true;
		for (u8 i = 0u; i < n; ++i) ok = ok && plane_ok(out[i]);
		check(ok, "lateral: invariantes");
	}

	// 4) Cruce de esquina (izquierda + arriba): sale dentro de ambos planos.
	{
		const CV in[3] = {{-9000, -9000, 800}, {10, -10, 800}, {10, 10, 800}};
		CV out[16] {};
		const u8 n = clip_camera_polygon(in, 3u, kCam, out, 16u);
		check(n >= 3u, "esquina: queda polígono");
		bool ok = true;
		for (u8 i = 0u; i < n; ++i) ok = ok && plane_ok(out[i]);
		check(ok, "esquina: invariantes");
	}

	// 5) Todo detrás / todo fuera: 0 vértices.
	{
		const CV behind[3] = {{0, 0, -100}, {10, 0, -200}, {0, 10, -50}};
		CV out[16] {};
		check(clip_camera_polygon(behind, 3u, kCam, out, 16u) == 0u, "detrás: vacío");
		const CV outside[3] = {{20000, 0, 500}, {20010, 0, 500}, {20000, 10, 500}};
		check(clip_camera_polygon(outside, 3u, kCam, out, 16u) == 0u, "fuera: vacío");
		check(clip_camera_polygon(behind, 0u, kCam, out, 16u) == 0u, "n=0: vacío");
		check(clip_camera_polygon(behind, 3u, kCam, out, 4u) == 0u, "cap insuficiente: vacío");
	}

	// 6) Invariantes con polígonos deterministas (LCG) dentro/fuera/cruzando.
	{
		u32 seed = 0x12345678u;
		auto next = [&seed]() {
			seed = seed * 1664525u + 1013904223u;
			return seed;
		};
		bool ok = true;
		for (int t = 0; t < 200 && ok; ++t) {
			CV in[6] {};
			for (u8 i = 0u; i < 6u; ++i) {
				in[i].x = static_cast<CS>(static_cast<s32>(next() % 40001u) - 20000);
				in[i].y = static_cast<CS>(static_cast<s32>(next() % 40001u) - 20000);
				in[i].z = static_cast<CS>(static_cast<s32>(next() % 60001u) - 15000);
			}
			CV out[16] {};
			const u8 n = clip_camera_polygon(in, 6u, kCam, out, 16u);
			for (u8 i = 0u; i < n; ++i) ok = ok && plane_ok(out[i]);
			// Proyección acotada (si sobrevive) y sin división inválida.
			for (u8 i = 0u; i < n; ++i) {
				s16 sx = 0;
				s16 sy = 0;
				ok = ok && project_camera_vertex(out[i], kCam, sx, sy);
				ok = ok && sx >= kCam.cx - kCam.half_w - 1 && sx <= kCam.cx + kCam.half_w + 1;
				ok = ok && sy >= kCam.cy - kCam.half_h - 1 && sy <= kCam.cy + kCam.half_h + 1;
			}
		}
		check(ok, "fuzz: invariantes y proyección acotada");
	}

	// 7) `render_mesh`: una cara interior se emite; trasera se culla; doble cara la pinta;
	//    cruzando near emite con >= 3 vertices dentro del viewport. Escalar de la maquina
	//    (`Fixed<s16,0>`): ejercita el camino crudo del corte (precision sub-unidad).
	{
		using Aff = math::Affine<3, MF, MF>;
		eng::util::Array<math3d::Vec3t<MF>, 3> verts {};
		verts[0] = math3d::vec3<MF>(-20, -20, 800);
		verts[1] = math3d::vec3<MF>(20, -20, 800);
		verts[2] = math3d::vec3<MF>(0, 20, 800);
		math3d::Face faces[1] = {{0u, 1u, 2u}};
		math3d::MeshViewT<MF> mesh {
			Span<const math3d::Vec3t<MF>>(verts.data(), 3u),
			Span<const math3d::Face>(faces, 1u)};

		RecordingPlayfield pf;
		pf.init(320, 200, 4);
		Surface surf {pf, SurfaceRect {0, 0, 320, 200}};
		math3d::Vec3t<MF> world[3] {};
		CamVertex<MF> scratch[16] {};
		s16 xs[16] {};
		s16 ys[16] {};
		const Aff ident = Aff::identity();
		const u32 drawn = render_mesh(mesh, ident, kCamF, surf, [](u32) { return u8 {5u}; },
					      world, scratch, xs, ys);
		check(drawn == 1u && pf.polys.size() == 1u, "render: cara interior emitida");
		bool inside = true;
		for (const auto& p : pf.polys) {
			for (u8 i = 0u; i < p.n; ++i) {
				inside = inside && p.xs[i] >= 0 && p.xs[i] < 320 && p.ys[i] >= 0 &&
					 p.ys[i] < 200;
			}
		}
		check(inside, "render: coordenadas de pantalla dentro del viewport");

		// Winding invertido = trasera -> culled por defecto; con doble cara se pinta.
		math3d::Face rev[1] = {{0u, 2u, 1u}};
		math3d::MeshViewT<MF> mesh_rev {
			Span<const math3d::Vec3t<MF>>(verts.data(), 3u),
			Span<const math3d::Face>(rev, 1u)};
		RecordingPlayfield pf2;
		pf2.init(320, 200, 4);
		Surface surf2 {pf2, SurfaceRect {0, 0, 320, 200}};
		check(render_mesh(mesh_rev, ident, kCamF, surf2, [](u32) { return u8 {1u}; }, world, scratch,
				  xs, ys) == 0u,
		      "render: trasera culled");
		check(render_mesh(mesh_rev, ident, kCamF, surf2, [](u32) { return u8 {1u}; }, world, scratch,
				  xs, ys, true) == 1u,
		      "render: doble cara pinta");

		// Cruzando near: queda una cara con al menos 3 vertices, todos dentro.
		verts[2] = math3d::vec3<MF>(0, 20, -400);
		RecordingPlayfield pf3;
		pf3.init(320, 200, 4);
		Surface surf3 {pf3, SurfaceRect {0, 0, 320, 200}};
		const u32 drew = render_mesh(mesh, ident, kCamF, surf3, [](u32) { return u8 {2u}; }, world,
					     scratch, xs, ys, true);
		check(drew == 1u && !pf3.polys.empty(), "render: cara que cruza near se emite recortada");
		check(!pf3.polys.empty() && pf3.polys[0].n >= 3u, "render: recorte con n>=3");
	}

	// 8) Clave de profundidad (painter entre sólidos): suma de z monótona.
	{
		eng::util::Array<math3d::Vec3t<eng::coord>, 4> near_v {};
		eng::util::Array<math3d::Vec3t<eng::coord>, 4> far_v {};
		for (u8 i = 0u; i < 4u; ++i) {
			near_v[i] = math3d::vec3<eng::coord>(0, 0, 100);
			far_v[i] = math3d::vec3<eng::coord>(0, 0, 900);
		}
		const s32 kn = solid_depth_key(Span<const math3d::Vec3t<eng::coord>>(near_v.data(), 4u));
		const s32 kf = solid_depth_key(Span<const math3d::Vec3t<eng::coord>>(far_v.data(), 4u));
		check(kf > kn, "profundidad: el lejano tiene clave mayor");
	}

	// 9) Pentagono (n-gon) interior: se emite SIN triangular (5 vertices).
	{
		using Aff = math::Affine<3, MF, MF>;
		eng::util::Array<math3d::Vec3t<MF>, 5> verts {};
		// Pentagono regular aproximado a radio 20 en el plano z = 1000.
		static constexpr s16 kPX[5] = {0, 19, 12, -12, -19};
		static constexpr s16 kPY[5] = {-20, -6, 16, 16, -6};
		for (u8 i = 0u; i < 5u; ++i) {
			verts[i] = math3d::vec3<MF>(kPX[i], kPY[i], 1000);
		}
		const u16 idx[5] = {0u, 1u, 2u, 3u, 4u};
		const math3d::FaceSpan span {0u, 5u};
		math3d::PolyMeshViewT<MF> mesh {
			Span<const math3d::Vec3t<MF>>(verts.data(), 5u),
			Span<const u16>(idx, 5u), Span<const math3d::FaceSpan>(&span, 1u)};
		RecordingPlayfield pf;
		pf.init(320, 200, 4);
		Surface surf {pf, SurfaceRect {0, 0, 320, 200}};
		math3d::Vec3t<MF> world[5] {};
		CamVertex<MF> scratch[16] {};
		s16 xs[16] {};
		s16 ys[16] {};
		const u32 drawn = render_solid(mesh, Aff::identity(), kCamF, surf,
					       [](u32) { return u8 {7u}; }, world, scratch, xs, ys);
		check(drawn == 1u && pf.polys.size() == 1u && pf.polys[0].n == 5u,
		      "pentagono: emitido con 5 vertices (sin triangular)");
	}

	if (g_fail != 0) {
		std::printf("HOST-481 FALLO: %d comprobaciones\n", g_fail);
		return 1;
	}
	std::printf("HOST-481 OK\n");
	return 0;
}
