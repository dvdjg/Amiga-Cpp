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
#include <eng/graphics/polygon_planes.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/memory/mem_bank.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/retro/mesh_primitives.hpp>

#include <cmath>
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
	bool fill_cpu(const s16* xs, const s16* ys, u8 n, u8 color) {
		return Playfield::fill_polygon(Span<const s16>(xs, n), Span<const s16>(ys, n), color);
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

/// CPU reference for the polygons emitted by Surface/renderer, in the same bitplane format
/// consumed by the Amiga backend. This lets geometry fixtures check coverage, not only vertices.
std::vector<u8> rasterize_recorded(const RecordingPlayfield& pf, u16 width, u16 height, u8 planes) {
	const u16 row_bytes = static_cast<u16>((width + 7u) / 8u);
	const u32 plane_bytes = static_cast<u32>(row_bytes) * height;
	std::vector<PlanePolygon> faces;
	faces.reserve(pf.polys.size());
	for (const auto& p : pf.polys) {
		faces.push_back(PlanePolygon {p.xs.data(), p.ys.data(), p.n, p.color});
	}
	std::vector<u8> pixels(static_cast<usize>(plane_bytes) * planes, 0u);
	(void)fill_polygons_by_plane_cpu(Span<const PlanePolygon>(faces.data(), faces.size()),
					 PlaneBytes {pixels.data(), static_cast<u32>(pixels.size())},
					 row_bytes, plane_bytes, planes, width, height);
	return pixels;
}

u32 count_set_pixels(const std::vector<u8>& pixels) {
	u32 count = 0u;
	for (u8 value : pixels) {
		for (u8 bit = 0u; bit < 8u; ++bit) count += (value >> bit) & 1u;
	}
	return count;
}

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

	// 5a) El fast path de proyección solo es válido cuando todos los vértices respetan near.
	//     Reproduce la cara cercana de la sala a yaw moderado: z=32 y x=±160 dan vértices
	//     por detrás de near tras rotar, aunque el ángulo esté dentro del antiguo límite yaw.
	{
		const math3d::Vec3t<MF> in_front[4] = {
			math3d::vec3<MF>(-20, -20, 100), math3d::vec3<MF>(20, -20, 100),
			math3d::vec3<MF>(20, 20, 64), math3d::vec3<MF>(-20, 20, 100)};
		const math3d::Vec3t<MF> crosses_near[4] = {
			math3d::vec3<MF>(-160, 96, -8), math3d::vec3<MF>(160, 96, 70),
			math3d::vec3<MF>(160, 96, 70), math3d::vec3<MF>(-160, 96, -8)};
		check(camera_vertices_at_or_beyond_near(kCamF, in_front, 4u),
		      "fast path: vértices sobre/delante de near son proyectables");
		check(!camera_vertices_at_or_beyond_near(kCamF, crosses_near, 4u),
		      "yaw moderado: cara cercana que cruza near exige clip de cámara");
		check(!camera_vertices_at_or_beyond_near(kCamF, crosses_near, 0u),
		      "near: conjunto vacío no habilita proyección directa");
	}

	// 5ab) Winding del render: fill_box publica caras CCW desde fuera.
	//      Con la camara en -Z, la cara cercana -Z (indice 1) es la unica visible en
	//      identidad; la lejana +Z (indice 0) no debe emitirse. Regresion de la inversion
	//      de signo del area de pantalla (y hacia abajo) que dejaba huecos en los cubos.
	{
		retro::MeshData<MF, 8, 24, 6> cube;
		check(retro::fill_box(cube, math3d::vec3<MF>(20, 20, 20)), "cubo fill_box");
		RecordingPlayfield pf;
		pf.init(320, 200, 4);
		Surface surf {pf, SurfaceRect {0, 0, 320, 200}};
		math3d::Vec3t<MF> world[8] {};
		CamVertex<MF> scratch[16] {};
		s16 xs[16] {};
		s16 ys[16] {};
		using Aff = math::Affine<3, MF, MF>;
		Aff model = Aff::identity();
		model.t = math3d::vec3<MF>(0, 0, 1000);
		const u32 drawn = render_solid(
			cube.view(), model, kCamF, surf,
			[](u32 face) { return static_cast<u8>(face + 1u); }, world, scratch, xs, ys);
		check(drawn == 1u && pf.polys.size() == 1u, "cubo: una cara visible en identidad");
		check(!pf.polys.empty() && pf.polys[0].color == 2u,
		      "cubo: se emite la cara -Z cercana (no la +Z lejana)");
	}
	// 5ac) La misma cara q12 de la demo 091 (fase 120): el poligono proyectado
	//      debe ser convexo. Si no lo es, `convex_spans` deja huecos en el relleno.
	{
		using RS = math::Fixed<s16, 12>;
		using Aff = math::Affine<3, RS, MF>;
		retro::MeshData<MF, 8, 24, 6> cube;
		check(retro::fill_box(cube, math3d::vec3<MF>(28, 28, 28)), "cubo q12");
		math3d::Mat3<RS> rot {};
		math3d::load_rotate(rot, retro::turns(120u), retro::turns(240u), retro::turns(360u));
		Aff model {};
		model.m = rot;
		model.t = math3d::vec3<MF>(0, 0, 320);
		const u16 ids[4] = {1u, 0u, 3u, 2u};
		const math3d::FaceSpan span {0u, 4u};
		const math3d::PolyMeshViewT<MF> face {
			cube.view().vertices, Span<const u16>(ids, 4u),
			Span<const math3d::FaceSpan>(&span, 1u)};
		RecordingPlayfield pf;
		pf.init(320, 200, 4);
		Surface surf {pf, SurfaceRect {0, 0, 320, 200}};
		math3d::Vec3t<MF> world[8] {};
		CamVertex<MF> scratch[16] {};
		s16 xs[16] {};
		s16 ys[16] {};
		const Camera3D<MF> cam340 {CTF::from_int(340), 160, 104, CTF::from_int(64),
					  CTF::from_int(160), CTF::from_int(104)};
		const u32 drawn = render_solid_styled(
			face, model, cam340, surf,
			[](u32) { return FaceStyle {2u, 0u, 0u}; }, world, scratch, xs, ys, true);
		check(drawn == 1u && pf.polys.size() == 1u, "cara q12: emitida");
		bool convex = pf.polys.size() == 1u && pf.polys[0].n == 4u;
		if (convex) {
			const auto& p = pf.polys[0];
			s32 sign = 0;
			for (u8 i = 0u; i < p.n; ++i) {
				const u8 j = static_cast<u8>((i + 1u) % p.n);
				const u8 k = static_cast<u8>((i + 2u) % p.n);
				const s32 cr = eng::math::mul_wide(p.xs[j] - p.xs[i], p.ys[k] - p.ys[j]) -
					       eng::math::mul_wide(p.ys[j] - p.ys[i], p.xs[k] - p.xs[j]);
				if (cr != 0) {
					if (sign == 0) sign = cr > 0 ? 1 : -1;
					else if ((cr > 0 ? 1 : -1) != sign) convex = false;
				}
			}
		}
		check(convex, "cara q12: poligono convexo");
		const std::vector<u8> pixels = rasterize_recorded(pf, 320u, 200u, 4u);
		const u32 covered = count_set_pixels(pixels);
		s32 area2 = 0;
		if (pf.polys.size() == 1u) {
			const auto& p = pf.polys[0];
			for (u8 i = 0u; i < p.n; ++i) {
				const u8 j = static_cast<u8>((i + 1u) % p.n);
				area2 += eng::math::mul_wide(p.xs[i], p.ys[j]) -
					 eng::math::mul_wide(p.xs[j], p.ys[i]);
			}
			if (area2 < 0) area2 = -area2;
		}
		const u32 expected = static_cast<u32>(area2 / 2);
		check(expected > 0u && covered * 4u >= expected * 3u,
		      "cara q12: cobertura CPU sin huecos");
		RecordingPlayfield cpu_pf;
		cpu_pf.init(320, 200, 4);
		if (pf.polys.size() == 1u) {
			(void)cpu_pf.fill_cpu(pf.polys[0].xs.data(), pf.polys[0].ys.data(),
					      pf.polys[0].n, 2u);
		}
		const u32 base_covered = count_set_pixels(cpu_pf.mem);
		ContiguousPlayfield contig;
		std::vector<u8> contig_mem(40u * 200u * 4u, 0u);
		check(contig.bind_raw(contig_mem.data(), static_cast<u32>(contig_mem.size()),
				      320u, 200u, 4u),
		      "contiguo bind");
		if (pf.polys.size() == 1u) {
			(void)contig.fill_polygon(Span<const s16>(pf.polys[0].xs.data(),
								  pf.polys[0].n),
						  Span<const s16>(pf.polys[0].ys.data(),
								  pf.polys[0].n),
						  2u);
		}
		const u32 contig_covered = count_set_pixels(contig_mem);
		check(expected > 0u && contig_covered * 4u >= expected * 3u,
		      "cara q12: ContiguousPlayfield sin huecos");
		check(expected > 0u && base_covered * 4u >= expected * 3u,
		      "cara q12: Playfield::fill_polygon sin huecos");
	}
	// 5ad) Silueta convexa (P8): un fill por objeto, hull de los 8 vertices.
	{
		retro::MeshData<MF, 8, 24, 6> cube;
		check(retro::fill_box(cube, math3d::vec3<MF>(20, 20, 20)), "cubo silueta");
		using Aff = math::Affine<3, MF, MF>;
		Aff model = Aff::identity();
		model.t = math3d::vec3<MF>(0, 0, 1000);
		RecordingPlayfield pf;
		pf.init(320, 200, 4);
		Surface surf {pf, SurfaceRect {0, 0, 320, 200}};
		math3d::Vec3t<MF> world[8] {};
		CamVertex<MF> scratch[16] {};
		s16 xs[16] {};
		s16 ys[16] {};
		u16 hull[16] {};
		const u32 drawn = render_convex_silhouette(
			cube.view(), model, kCamF, surf, 3u, world, scratch, xs, ys, hull, 16u);
		check(drawn == 1u && pf.polys.size() == 1u, "silueta: un poligono");
		check(pf.polys.size() == 1u && pf.polys[0].n == 4u,
		      "silueta identidad: hull de 4 vertices");
		const std::vector<u8> pixels = rasterize_recorded(pf, 320u, 200u, 4u);
		check(count_set_pixels(pixels) != 0u, "silueta: cobertura CPU");
		model.t = math3d::vec3<MF>(0, 0, 20);
		check(render_convex_silhouette(cube.view(), model, kCamF, surf, 3u, world,
					       scratch, xs, ys, hull, 16u) == 0u,
		      "silueta: near devuelve 0 para fallback");
	}	// 5aa) Regresión aislada de winding/culling para el mismo cubo usado por la demo 080.
	//      Compara el producto s32 barato (válido en 68000) contra face_visible del engine en
	//      yaw simétrico y giro compuesto: ambos deben seleccionar las mismas caras exteriores.
	{
		static constexpr s16 kCubeHalf = 28;
		const math3d::Vec3t<CS> local[8] = {
			math3d::vec3<CS>(-kCubeHalf, -kCubeHalf, -kCubeHalf),
			math3d::vec3<CS>(kCubeHalf, -kCubeHalf, -kCubeHalf),
			math3d::vec3<CS>(kCubeHalf, kCubeHalf, -kCubeHalf),
			math3d::vec3<CS>(-kCubeHalf, kCubeHalf, -kCubeHalf),
			math3d::vec3<CS>(-kCubeHalf, -kCubeHalf, kCubeHalf),
			math3d::vec3<CS>(kCubeHalf, -kCubeHalf, kCubeHalf),
			math3d::vec3<CS>(kCubeHalf, kCubeHalf, kCubeHalf),
			math3d::vec3<CS>(-kCubeHalf, kCubeHalf, kCubeHalf),
		};
		static constexpr u8 kFace[6][4] = {
			{0u, 3u, 2u, 1u}, {4u, 5u, 6u, 7u}, {1u, 2u, 6u, 5u},
			{0u, 4u, 7u, 3u}, {0u, 1u, 5u, 4u}, {3u, 7u, 6u, 2u},
		};
		struct Pose { s16 yaw; s16 pitch; s16 roll; };
		static constexpr Pose poses[4] = {{0, 0, 0}, {165, 0, 0}, {-165, 0, 0}, {91, -73, 37}};
		const math3d::Vec3t<CS> origin = math3d::vec3<CS>(0, 0, 0);
		bool ok = true;
		for (const Pose& pose : poses) {
			math3d::Affine3<CS> view = math3d::Affine3<CS>::identity();
			const CS turns_to_radians = 6.28318530718f / 4096.0f;
			const auto radians = [turns_to_radians](s16 turn) {
				return static_cast<CS>(turn * turns_to_radians);
			};
			const CS ax = radians(pose.pitch), ay = radians(pose.yaw), az = radians(pose.roll);
			const math3d::SinCos3<CS> sc {std::sin(ax), std::cos(ax), std::sin(ay),
						       std::cos(ay), std::sin(az), std::cos(az)};
			math3d::load_rotate(view.m, sc);
			math3d::Vec3t<CS> world[8] {};
			math3d::Vec3t<CS> cam[8] {};
			for (u8 v = 0u; v < 8u; ++v) {
				world[v] = local[v];
				world[v].v[2] += 512.0f;
			}
			math3d::mesh_transform(Span<const math3d::Vec3t<CS>>(world, 8u), view,
					       Span<math3d::Vec3t<CS>>(cam, 8u));
			u8 fast_count = 0u;
			u8 oracle_count = 0u;
			for (u8 f = 0u; f < 6u; ++f) {
				const auto& a = cam[kFace[f][0]];
				const auto& b = cam[kFace[f][1]];
				const auto& c = cam[kFace[f][2]];
				const CS abx = b.v[0] - a.v[0], aby = b.v[1] - a.v[1], abz = b.v[2] - a.v[2];
				const CS acx = c.v[0] - a.v[0], acy = c.v[1] - a.v[1], acz = c.v[2] - a.v[2];
				const CS nx = aby * acz - abz * acy;
				const CS ny = abz * acx - abx * acz;
				const CS nz = abx * acy - aby * acx;
				const CS facing = nx * -a.v[0] + ny * -a.v[1] + nz * -a.v[2];
				const bool fast = facing > 0;
				const bool oracle = math3d::face_signed_area(a, b, c, origin) > 0;
				if (pose.yaw == 0 && pose.pitch == 0 && pose.roll == 0) {
					if (f == 0u) ok = ok && fast;      // cara z− orientada a la cámara
					if (f == 1u) ok = ok && !fast;     // cara z+ orientada hacia el fondo
				}
				if (fast != oracle) {
					std::printf("[cube-normal] pose %d/%d/%d face %u fast=%d oracle=%d dot=%g\n",
						    pose.yaw, pose.pitch, pose.roll, f, fast ? 1 : 0,
						    oracle ? 1 : 0, static_cast<double>(facing));
				}
				ok = ok && fast == oracle;
				fast_count = static_cast<u8>(fast_count + (fast ? 1u : 0u));
				oracle_count = static_cast<u8>(oracle_count + (oracle ? 1u : 0u));
			}
			if (fast_count != oracle_count || fast_count > 3u || fast_count == 0u) {
				std::printf("[cube-count] pose %d/%d/%d fast=%u oracle=%u\n", pose.yaw,
					    pose.pitch, pose.roll, fast_count, oracle_count);
			}
			ok = ok && fast_count == oracle_count && fast_count <= 3u && fast_count != 0u;
		}
		check(ok, "cubo convexo: culling por normal s32 coincide con el oracle en yaw ± y giro compuesto");
	}
	// Camino entero usado por 080: valida literalmente el producto s16×s16→s32 del target,
	// con la cara z− hacia la cámara y la opuesta z+ descartada.
	{
		static constexpr u8 kFace[6][4] = {
			{0u, 3u, 2u, 1u}, {4u, 5u, 6u, 7u}, {1u, 2u, 6u, 5u},
			{0u, 4u, 7u, 3u}, {0u, 1u, 5u, 4u}, {3u, 7u, 6u, 2u},
		};
		const math3d::Vec3t<MF> cube[8] = {
			math3d::vec3<MF>(-28, -28, 484), math3d::vec3<MF>(28, -28, 484),
			math3d::vec3<MF>(28, 28, 484), math3d::vec3<MF>(-28, 28, 484),
			math3d::vec3<MF>(-28, -28, 540), math3d::vec3<MF>(28, -28, 540),
			math3d::vec3<MF>(28, 28, 540), math3d::vec3<MF>(-28, 28, 540),
		};
		const math3d::Vec3t<MF> origin = math3d::vec3<MF>(0, 0, 0);
		bool ok = true;
		u8 visible = 0u;
		for (u8 f = 0u; f < 6u; ++f) {
			const auto& a = cube[kFace[f][0]];
			const auto& b = cube[kFace[f][1]];
			const auto& c = cube[kFace[f][2]];
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
			const bool fast = facing > 0;
			const bool oracle = math3d::face_signed_area(a, b, c, origin) > 0;
			ok = ok && fast == oracle;
			visible = static_cast<u8>(visible + (fast ? 1u : 0u));
		}
		check(ok && visible == 1u, "cubo q0: producto normal s32 mantiene frente z− y descarta z+");
	}
	// La sonda fija del demo 080 expone solo una pose a la vez; barre la tabla de fase para
	// demostrar que las seis caras del winding llegan a verse y que el producto Fixed Q0 del
	// target mantiene el mismo signo que el oráculo en orientaciones compuestas.
	{
		static constexpr s16 kCubeHalf = 28;
		using RS = math::Fixed<s16, 12>;
		static constexpr u8 kFace[6][4] = {
			{0u, 3u, 2u, 1u}, {4u, 5u, 6u, 7u}, {1u, 2u, 6u, 5u},
			{0u, 4u, 7u, 3u}, {0u, 1u, 5u, 4u}, {3u, 7u, 6u, 2u},
		};
		const math3d::Vec3t<MF> local[8] = {
			math3d::vec3<MF>(-kCubeHalf, -kCubeHalf, -kCubeHalf),
			math3d::vec3<MF>(kCubeHalf, -kCubeHalf, -kCubeHalf),
			math3d::vec3<MF>(kCubeHalf, kCubeHalf, -kCubeHalf),
			math3d::vec3<MF>(-kCubeHalf, kCubeHalf, -kCubeHalf),
			math3d::vec3<MF>(-kCubeHalf, -kCubeHalf, kCubeHalf),
			math3d::vec3<MF>(kCubeHalf, -kCubeHalf, kCubeHalf),
			math3d::vec3<MF>(kCubeHalf, kCubeHalf, kCubeHalf),
			math3d::vec3<MF>(-kCubeHalf, kCubeHalf, kCubeHalf),
		};
		const math3d::Vec3t<MF> origin = math3d::vec3<MF>(0, 0, 0);
		bool ok = true;
		u8 face_seen = 0u;
		for (u16 phase = 0u; phase < retro::kAngleSteps; phase += 32u) {
			math3d::Affine3<RS, MF> model = math3d::Affine3<RS, MF>::identity();
			math3d::load_rotate(
				model.m,
				retro::turns(static_cast<u16>((phase * 2u) & 4095u)),
				retro::turns(static_cast<u16>((phase * 3u) & 4095u)),
				retro::turns(static_cast<u16>((phase * 5u) & 4095u)));
			math3d::Vec3t<MF> world[8] {};
			math3d::Vec3t<MF> cam[8] {};
			math3d::mesh_transform(Span<const math3d::Vec3t<MF>>(local, 8u), model,
					       Span<math3d::Vec3t<MF>>(world, 8u));
			for (u8 v = 0u; v < 8u; ++v) {
				world[v].v[2] = world[v].v[2] + CTF::from_int(480);
			}
			math3d::Affine3<RS, MF> view = math3d::Affine3<RS, MF>::identity();
			math3d::mesh_transform(Span<const math3d::Vec3t<MF>>(world, 8u), view,
					       Span<math3d::Vec3t<MF>>(cam, 8u));
			u8 visible = 0u;
			for (u8 f = 0u; f < 6u; ++f) {
				const auto& a = cam[kFace[f][0]];
				const auto& b = cam[kFace[f][1]];
				const auto& c = cam[kFace[f][2]];
				const s16 abx = static_cast<s16>(b.v[0].v - a.v[0].v);
				const s16 aby = static_cast<s16>(b.v[1].v - a.v[1].v);
				const s16 abz = static_cast<s16>(b.v[2].v - a.v[2].v);
				const s16 acx = static_cast<s16>(c.v[0].v - a.v[0].v);
				const s16 acy = static_cast<s16>(c.v[1].v - a.v[1].v);
				const s16 acz = static_cast<s16>(c.v[2].v - a.v[2].v);
				const s32 nx = math::mul_wide(aby, acz) - math::mul_wide(abz, acy);
				const s32 ny = math::mul_wide(abz, acx) - math::mul_wide(abx, acz);
				const s32 nz = math::mul_wide(abx, acy) - math::mul_wide(aby, acx);
				const s32 facing = math::mul_wide(static_cast<s16>(nx),
								  static_cast<s16>(-a.v[0].v)) +
					   math::mul_wide(static_cast<s16>(ny),
								  static_cast<s16>(-a.v[1].v)) +
					   math::mul_wide(static_cast<s16>(nz),
								  static_cast<s16>(-a.v[2].v));
				const bool fast = facing > 0;
				const bool oracle = math3d::face_signed_area(a, b, c, origin) > 0;
				ok = ok && fast == oracle;
				if (fast) {
					face_seen = static_cast<u8>(face_seen | (1u << f));
					++visible;
				}
			}
			ok = ok && visible > 0u && visible <= 3u;
		}
		check(ok && face_seen == 0x3fu,
		      "cubo: barrido Q0 de 128 fases expone las seis normales y coincide con el oracle");
	}

	// 5b) La capacidad declarada limita las escrituras incluso si el polígono cabe en
	//     el scratch interno. Un octágono interior necesita 8 salidas; cap=6 debe fallar
	//     sin tocar el primer elemento que queda fuera de la vista.
	{
		const CV octagon[8] = {{0.0f, -20.0f, 1000.0f}, {14.0f, -14.0f, 1000.0f},
				       {20.0f, 0.0f, 1000.0f},  {14.0f, 14.0f, 1000.0f},
				       {0.0f, 20.0f, 1000.0f},  {-14.0f, 14.0f, 1000.0f},
				       {-20.0f, 0.0f, 1000.0f}, {-14.0f, -14.0f, 1000.0f}};
		CV out[16] {};
		out[6] = CV {1234.0f, 2345.0f, 3456.0f};
		const u8 n = clip_camera_polygon(octagon, 8u, kCam, out, 6u);
		check(n == 0u, "capacidad insuficiente: no publica un polígono parcial");
		check(out[6].x == 1234.0f && out[6].y == 2345.0f && out[6].z == 3456.0f,
		      "capacidad insuficiente: no escribe fuera de la vista declarada");
	}

	// 5c) Un shared edge puede llegar con cualquiera de sus dos sentidos desde caras
	//     adyacentes. El corte fijo debe producir el mismo punto tanto dentro→fuera como
	//     fuera→dentro (la llamada real recorre el winding original de cada cara).
	{
		const auto planes = detail3d::camera_planes(kCamF);
		const CamVertex<MF> inside {CTF::from_int(0), CTF::from_int(50), CTF::from_int(900)};
		const CamVertex<MF> outside {CTF::from_int(-600), CTF::from_int(-50), CTF::from_int(900)};
		const auto& plane = planes.planes[1];
		const auto d_inside = plane.dist(inside);
		const auto d_outside = plane.dist(outside);
		const CamVertex<MF> forward = detail3d::cut_edge<MF>(inside, outside, d_inside, d_outside);
		const CamVertex<MF> reverse = detail3d::cut_edge<MF>(outside, inside, d_outside, d_inside);
		check(forward.x == reverse.x && forward.y == reverse.y && forward.z == reverse.z,
		      "corte fijo: una arista compartida no depende del sentido del winding");
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

	// 6b) Generalizacion del culling a CUALQUIER convexo: tetraedro irregular con
	//     rotaciones aleatorias. La visibilidad por normal 3D (camara en el origen)
	//     debe coincidir con el signo del area proyectada para toda cara visible no
	//     degenerada. Si el modelo fuese solo para el cubo, esto fallaria.
	{
		const math3d::Vec3t<CS> tetra[4] = {{1.0f, 1.0f, 1.0f},
						 {1.0f, -1.0f, -1.0f},
						 {-1.0f, 1.0f, -1.0f},
						 {-1.0f, -1.0f, 1.0f}};
		const u8 faces4[4][3] = {{0u, 1u, 2u}, {0u, 2u, 3u}, {0u, 3u, 1u}, {1u, 3u, 2u}};
		const math3d::Vec3t<CS> origin {0.0f, 0.0f, 0.0f};
		u32 seed = 0x9e3779b9u;
		auto rnd = [&seed]() {
			seed = seed * 1664525u + 1013904223u;
			return static_cast<int>(seed >> 16u) - 32768;
		};
		bool ok = true;
		for (int it = 0; it < 64 && ok; ++it) {
			const float ax = static_cast<float>(rnd()) * (6.2831853f / 65536.0f);
			const float ay = static_cast<float>(rnd()) * (6.2831853f / 65536.0f);
			const CS ca = std::cos(ax), sa = std::sin(ax);
			const CS cb = std::cos(ay), sb = std::sin(ay);
			math3d::Vec3t<CS> w[4];
			for (u8 k = 0u; k < 4u; ++k) {
				const CS x = tetra[k].v[0];
				const CS y = tetra[k].v[1];
				const CS z = tetra[k].v[2];
				// Rz no hace falta: dos ejes bastan para orientaciones genericas.
				const CS xr = cb * x + sb * z;
				const CS zr = -sb * x + cb * z;
				w[k] = math3d::Vec3t<CS> {{xr, ca * y - sa * zr, sa * y + ca * zr}};
				w[k].v[2] += 900.0f; // camara: z delante
			}
			for (u8 f = 0u; f < 4u && ok; ++f) {
				const math3d::Vec3t<CS>& a = w[faces4[f][0]];
				const math3d::Vec3t<CS>& b = w[faces4[f][1]];
				const math3d::Vec3t<CS>& c = w[faces4[f][2]];
				const bool nvis = math3d::face_visible(a, b, c, origin);
				const CV cva {a.v[0], a.v[1], a.v[2]};
				const CV cvb {b.v[0], b.v[1], b.v[2]};
				const CV cvc {c.v[0], c.v[1], c.v[2]};
				s16 xs[3] {}, ys[3] {};
				if (!project_camera_vertex(cva, kCam, xs[0], ys[0]) ||
				    !project_camera_vertex(cvb, kCam, xs[1], ys[1]) ||
				    !project_camera_vertex(cvc, kCam, xs[2], ys[2])) {
					continue;
				}
				const s32 area = screen_area2(xs, ys, 3u);
				if (area > -8 && area < 8) continue; // canto de perfil: ambiguo
				ok = ok && (nvis == (area < 0));
			}
		}
		check(ok, "culling por normal 3D coincide con area proyectada (tetraedro irregular)");
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
		math3d::Face faces[1] = {{0u, 2u, 1u}};
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
		math3d::Face rev[1] = {{0u, 1u, 2u}};
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
		const u16 idx[5] = {0u, 4u, 3u, 2u, 1u};
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

	// 10) Cubo estático: el renderer recorta sus caras al cruzar cada lateral y las cuatro
	//     esquinas; completamente fuera por la derecha no emite caras. La superficie es la
	//     última barrera 2D, así cada vértice entregado al raster queda dentro del viewport.
	{
		using Aff = math::Affine<3, MF, MF>;
		static constexpr s16 kX[8] = {-12, 12, 12, -12, -12, 12, 12, -12};
		static constexpr s16 kY[8] = {-12, -12, 12, 12, -12, -12, 12, 12};
		static constexpr s16 kZ[8] = {-40, -40, -40, -40, 40, 40, 40, 40};
		static constexpr u16 kIndices[24] = {
			0u, 3u, 2u, 1u, 4u, 5u, 6u, 7u, 1u, 2u, 6u, 5u,
			0u, 4u, 7u, 3u, 0u, 1u, 5u, 4u, 3u, 7u, 6u, 2u};
		static constexpr math3d::FaceSpan kFaces[6] = {
			{0u, 4u}, {4u, 4u}, {8u, 4u}, {12u, 4u}, {16u, 4u}, {20u, 4u}};
		static constexpr s16 kCenterZ = 900;
		struct Pose {
			s16 x;
			s16 y;
			bool visible;
		};
		static constexpr Pose kPoses[13] = {
			{0, 0, true}, {35, 0, true}, {-35, 0, true}, {0, -22, true}, {0, 22, true},
			{35, -22, true}, {-35, -22, true}, {35, 22, true}, {-35, 22, true},
			{60, 0, false}, {-60, 0, false}, {0, -40, false}, {0, 40, false}};
		bool ok = true;
		for (const Pose& pose : kPoses) {
			math3d::Vec3t<MF> verts[8] {};
			for (u8 i = 0u; i < 8u; ++i) {
				verts[i] = math3d::vec3<MF>(kX[i] + pose.x, kY[i] + pose.y, kCenterZ + kZ[i]);
			}
			const math3d::PolyMeshViewT<MF> mesh {
				Span<const math3d::Vec3t<MF>>(verts, 8u), Span<const u16>(kIndices, 24u),
				Span<const math3d::FaceSpan>(kFaces, 6u)};
			RecordingPlayfield pf;
			pf.init(320u, 200u, 4u);
			Surface surf {pf, SurfaceRect {0, 0, 320, 200}};
			math3d::Vec3t<MF> world[8] {};
			CamVertex<MF> scratch[16] {};
			s16 xs[16] {}, ys[16] {};
			const u32 drawn = render_solid(mesh, Aff::identity(), kCamF, surf,
						       [](u32 face) { return static_cast<u8>(face + 1u); },
						       world, scratch, xs, ys);
			ok = ok && (pose.visible ? drawn != 0u : drawn == 0u);
			for (const auto& poly : pf.polys) {
				ok = ok && poly.n >= 3u;
				for (u8 i = 0u; i < poly.n; ++i) {
					ok = ok && poly.xs[i] >= 0 && poly.xs[i] < 320 && poly.ys[i] >= 0 &&
					     poly.ys[i] < 200;
				}
			}
		}
		check(ok, "cubo estático: cuatro laterales, cuatro esquinas y totalmente fuera");
	}

	// 11) El mismo pentágono n-gon se escala y se traslada hasta cruzar cada borde.
	//     Cada pose debe sobrevivir al clip, tocar el borde esperado y producir cobertura
	//     no vacía en la referencia CPU por plano.
	{
		static constexpr s16 kPX[5] = {0, 19, 12, -12, -19};
		static constexpr s16 kPY[5] = {-20, -6, 16, 16, -6};
		struct Pose {
			s16 tx;
			s16 ty;
			s16 scale;
			u8 edge;
		};
		static constexpr Pose kPoses[8] = {
			{-170, 0, 1, 1u}, {-170, 0, 3, 1u}, {170, 0, 1, 2u}, {170, 0, 3, 2u},
			{0, -110, 1, 4u}, {0, -110, 3, 4u}, {0, 110, 1, 8u}, {0, 110, 3, 8u}};
		bool ok = true;
		for (const Pose& pose : kPoses) {
			CamVertex<MF> input[5] {};
			for (u8 i = 0u; i < 5u; ++i) {
				input[i] = {CTF::from_int(kPX[i] * pose.scale + pose.tx),
					    CTF::from_int(kPY[i] * pose.scale + pose.ty),
					    CTF::from_int(4000)};
			}
			CamVertex<MF> clipped[16] {};
			const u8 n = clip_camera_polygon(input, 5u, kCamF, clipped, 16u);
			ok = ok && n >= 3u;
			s16 xs[16] {}, ys[16] {};
			bool projected = n >= 3u;
			for (u8 i = 0u; i < n; ++i) {
				projected = project_camera_vertex(clipped[i], kCamF, xs[i], ys[i]) && projected;
			}
			ok = ok && projected;
			RecordingPlayfield pf;
			pf.init(320u, 200u, 2u);
			Surface surf {pf, SurfaceRect {0, 0, 320, 200}};
			ok = ok && surf.fill_polygon(xs, ys, n, 3u) && pf.polys.size() == 1u;
			s16 min_x = 32767, min_y = 32767, max_x = -32768, max_y = -32768;
			for (const auto& poly : pf.polys) {
				for (u8 i = 0u; i < poly.n; ++i) {
					if (poly.xs[i] < min_x) min_x = poly.xs[i];
					if (poly.xs[i] > max_x) max_x = poly.xs[i];
					if (poly.ys[i] < min_y) min_y = poly.ys[i];
					if (poly.ys[i] > max_y) max_y = poly.ys[i];
				}
			}
			if ((pose.edge & 1u) != 0u) ok = ok && min_x <= 2;
			if ((pose.edge & 2u) != 0u) ok = ok && max_x >= 317;
			if ((pose.edge & 4u) != 0u) ok = ok && min_y <= 2;
			if ((pose.edge & 8u) != 0u) ok = ok && max_y >= 197;
			const std::vector<u8> cpu = rasterize_recorded(pf, 320u, 200u, 2u);
			ok = ok && count_set_pixels(cpu) != 0u;
		}
		check(ok, "pentágono escalado: cuatro bordes y cobertura de referencia CPU");
	}

	// 12) Tetraedro irregular parcialmente fuera por la derecha. Las caras pasan por el
	//     renderer n-gon, el clipping lateral, Surface y el rasterizador CPU por plano.
	{
		using Aff = math::Affine<3, MF, MF>;
		const math3d::Vec3t<MF> verts[4] = {
			math3d::vec3<MF>(120, -40, 4000), math3d::vec3<MF>(190, -40, 4000),
			math3d::vec3<MF>(155, 40, 4000), math3d::vec3<MF>(155, 0, 4100)};
		static constexpr u16 kIndices[12] = {0u, 2u, 1u, 0u, 1u, 3u, 1u, 2u, 3u, 2u, 0u, 3u};
		static constexpr math3d::FaceSpan kFaces[4] = {{0u, 3u}, {3u, 3u}, {6u, 3u}, {9u, 3u}};
		const math3d::PolyMeshViewT<MF> mesh {
			Span<const math3d::Vec3t<MF>>(verts, 4u), Span<const u16>(kIndices, 12u),
			Span<const math3d::FaceSpan>(kFaces, 4u)};
		RecordingPlayfield pf;
		pf.init(320u, 200u, 4u);
		Surface surf {pf, SurfaceRect {0, 0, 320, 200}};
		math3d::Vec3t<MF> world[4] {};
		CamVertex<MF> scratch[16] {};
		s16 xs[16] {}, ys[16] {};
		const u32 drawn = render_solid(mesh, Aff::identity(), kCamF, surf,
					       [](u32 face) { return static_cast<u8>(face % 3u + 1u); },
					       world, scratch, xs, ys, true);
		bool bounded = drawn >= 2u && !pf.polys.empty();
		bool reaches_right = false;
		for (const auto& poly : pf.polys) {
			bounded = bounded && poly.n >= 3u;
			for (u8 i = 0u; i < poly.n; ++i) {
				bounded = bounded && poly.xs[i] >= 0 && poly.xs[i] < 320 && poly.ys[i] >= 0 &&
					 poly.ys[i] < 200;
				reaches_right = reaches_right || poly.xs[i] >= 317;
			}
		}
		const std::vector<u8> cpu = rasterize_recorded(pf, 320u, 200u, 4u);

		// Oracle CPU independiente de render_solid: recorre las cuatro caras desde sus
		// índices, aplica clip/proyección y rellena el Surface de referencia una por una.
		RecordingPlayfield reference_pf;
		reference_pf.init(320u, 200u, 4u);
		Surface reference_surf {reference_pf, SurfaceRect {0, 0, 320, 200}};
		bool reference_ok = true;
		for (u8 face = 0u; face < 4u; ++face) {
			eng::graphics::CamVertex<MF> input[3] {};
			for (u8 k = 0u; k < 3u; ++k) {
				const u16 vertex = kIndices[static_cast<u16>(face) * 3u + k];
				input[k] = {verts[vertex].v[0], verts[vertex].v[1], verts[vertex].v[2]};
			}
			eng::graphics::CamVertex<MF> clipped[16] {};
			const u8 n = clip_camera_polygon(input, 3u, kCamF, clipped, 16u);
			if (n < 3u) continue;
			s16 ref_x[16] {}, ref_y[16] {};
			for (u8 k = 0u; k < n; ++k) {
				reference_ok = project_camera_vertex(clipped[k], kCamF, ref_x[k], ref_y[k]) &&
					       reference_ok;
			}
			if (!reference_ok || screen_area2(ref_x, ref_y, n) == 0) continue;
			(void)reference_surf.fill_polygon(ref_x, ref_y, n,
							 static_cast<u8>(face % 3u + 1u));
		}
		const std::vector<u8> reference_cpu = rasterize_recorded(reference_pf, 320u, 200u, 4u);
		check(bounded && reaches_right && count_set_pixels(cpu) != 0u && cpu == reference_cpu,
		      "tetraedro irregular: clipping parcial derecho y cobertura CPU por plano");
	}

	if (g_fail != 0) {
		std::printf("HOST-481 FALLO: %d comprobaciones\n", g_fail);
		return 1;
	}
	std::printf("HOST-481 OK\n");
	return 0;
}
