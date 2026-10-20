// ============================================================================
// L1-005 — Clipping 3D y relleno por plano en las cuatro esquinas
// ============================================================================
// Sonda estática que lleva un cubo inclinado desde el espacio de cámara hasta el
// Blitter, y contrasta cada bitplane con la referencia CPU. El desplazamiento de
// una scanline del trazador ONEDOT+FILL_XOR se compara con la tolerancia de ±2 px
// documentada en `blitter-line-subpixel-fill.md` §2.1 y en `L1-003`.

#include <eng/api/api.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_mesh.hpp>
#include <eng/retro/fixed_trig.hpp>

#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kWorldH = 232u;
constexpr u8 kPlanes = 2u;
constexpr u16 kRowBytes = kWidth / 8u;
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kBandY = kWorldH;
constexpr u16 kBandH = kHeight - kWorldH;
constexpr s16 kCubeHalf = 150;
constexpr s16 kCubeZ = 900;
constexpr s16 kFocal = 340;
constexpr u16 kTilt = 512u; // 45°: la tabla `retro::Turns` tiene 4096 pasos por vuelta.
constexpr u8 kMaxCornerFaces = 24u;
constexpr u16 kRasterTolerance = 2u; // tolerancia del trazador de línea/area fill frente al CPU.

#ifndef K_L1_EDGE_STRESS
#define K_L1_EDGE_STRESS 0
#endif
#ifndef K_L1_EDGE_STRESS_CPU_DISPLAY
#define K_L1_EDGE_STRESS_CPU_DISPLAY 0
#endif
static_assert(K_L1_EDGE_STRESS == 0 || K_L1_EDGE_STRESS == 1,
	      "K_L1_EDGE_STRESS selecciona la matriz de esquinas o una pose estática de la animación");
static_assert(K_L1_EDGE_STRESS_CPU_DISPLAY == 0 ||
		  (K_L1_EDGE_STRESS == 1 && K_L1_EDGE_STRESS_CPU_DISPLAY == 1),
	      "la página CPU visible solo se usa en la pose estática de diagnóstico");

constexpr eng::Palette32 kPalette {{
	0x000, 0x00f, 0xf00, 0xf0f,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
}};
constexpr eng::debug::Version kVersion {1u, 0u};

enum : u32 {
	kBootMemory = 0x00009501u,
	kBootCompose = 0x00009502u,
	kBootReference = 0x00009503u,
	kBootFaceCapacity = 0x00009504u,
};

/// Four static camera translations; the fixed model tilt exposes the colored faces.
struct CornerPose {
	s16 x;
	s16 y;
};
constexpr CornerPose kCornerPoses[4] = {
	{-560, -270}, {450, -270}, {450, 270}, {-560, 270}};
constexpr u8 kExpectedFacesPerCorner[4] = {1u, 1u, 2u, 2u};
// Replay estático del frame de secuencia f0473 de la 088: dos advances al precargar
// las páginas y 473 updates de juego producen estos ángulos y la pose inferior-derecha.
constexpr CornerPose kEdgeStressPose {304, 288};
constexpr u16 kEdgeStressRotateX = 1641u;
constexpr u16 kEdgeStressRotateY = 395u;
constexpr u16 kEdgeStressRotateZ = 2375u;
constexpr u8 kPoseCount = K_L1_EDGE_STRESS ? 1u : 4u;

/// Visible corners in camera-space cube winding, colored by bitplane index.
constexpr u8 kCubeFaces[6][4] = {
	{0, 3, 2, 1}, {4, 5, 6, 7}, {1, 2, 6, 5},
	{0, 4, 7, 3}, {0, 1, 5, 4}, {3, 7, 6, 2}};
constexpr u8 kCubeColors[6] = {1u, 2u, 3u, 1u, 2u, 3u};
constexpr eng::math3d::Vec3t<eng::coord> kCube[8] = {
	{-kCubeHalf, -kCubeHalf, -kCubeHalf}, {kCubeHalf, -kCubeHalf, -kCubeHalf},
	{kCubeHalf, kCubeHalf, -kCubeHalf}, {-kCubeHalf, kCubeHalf, -kCubeHalf},
	{-kCubeHalf, -kCubeHalf, kCubeHalf}, {kCubeHalf, -kCubeHalf, kCubeHalf},
	{kCubeHalf, kCubeHalf, kCubeHalf}, {-kCubeHalf, kCubeHalf, kCubeHalf},
};

/// Four expected output regions; a color in every region proves all poses survive clipping.
struct Roi {
	u16 x0;
	u16 y0;
	u16 x1;
	u16 y1;
	bool left;
	bool right;
	bool top;
	bool bottom;
};
constexpr Roi kCornerRois[4] = {
	{0u, 0u, 80u, 80u, true, false, true, false},
	{240u, 0u, 320u, 80u, false, true, true, false},
	{240u, 150u, 320u, 232u, false, true, false, true},
	{0u, 150u, 80u, 232u, true, false, false, true}};
constexpr Roi kEdgeStressRoi {160u, 120u, 320u, 232u, false, false, false, false};

/// Reads one planar pixel; `planes` begins at plane 0, each plane occupying `kPlaneBytes`.
[[nodiscard]] bool pixel(const u8* planes, u8 p, u16 x, u16 y) {
	const u8 byte = planes[static_cast<u32>(p) * kPlaneBytes +
				static_cast<u32>(y) * kRowBytes + (x >> 3u)];
	return ((byte >> (7u - (x & 7u))) & 1u) != 0u;
}

/// Requires content near both viewport edges belonging to this corner, allowing the ±2 px raster tolerance.
[[nodiscard]] bool corner_is_clipped(const u8* planes, const Roi& r) {
	bool any = false, left = false, right = false, top = false, bottom = false;
	for (u16 y = r.y0; y < r.y1; ++y) {
		for (u16 x = r.x0; x < r.x1; ++x) {
			if (!pixel(planes, 0u, x, y) && !pixel(planes, 1u, x, y)) continue;
			any = true;
			left = left || x <= 2u + kRasterTolerance;
			right = right || x >= kWidth - 3u - kRasterTolerance;
			top = top || y <= 2u + kRasterTolerance;
			bottom = bottom || y >= kWorldH - 3u - kRasterTolerance;
		}
	}
	return any && (!r.left || left) && (!r.right || right) && (!r.top || top) &&
	       (!r.bottom || bottom);
}

/// Checks that every reference/Blitter pixel has a same-plane counterpart within ±2 px.
[[nodiscard]] bool corner_pixels_match(const u8* got, const u8* want) {
	for (u8 p = 0u; p < kPlanes; ++p) {
		for (u16 y = 0u; y < kWorldH; ++y) {
			for (u16 x = 0u; x < kWidth; ++x) {
				const bool expected = pixel(want, p, x, y);
				const bool actual = pixel(got, p, x, y);
				if (expected == actual) continue;
				bool nearby = false;
				for (s32 dy = -2; dy <= 2 && !nearby; ++dy) {
					for (s32 dx = -2; dx <= 2 && !nearby; ++dx) {
						const s32 nx = static_cast<s32>(x) + dx;
						const s32 ny = static_cast<s32>(y) + dy;
						if (nx >= 0 && nx < static_cast<s32>(kWidth) && ny >= 0 &&
						    ny < static_cast<s32>(kWorldH)) {
							nearby = pixel(expected ? got : want, p, static_cast<u16>(nx),
								       static_cast<u16>(ny));
						}
					}
				}
				if (!nearby) return false;
			}
		}
	}
	return true;
}

/// Stress replay scans only the lower-right ROI containing the suspect shared edge.
[[nodiscard]] u8 stress_color_mask(const u8* planes) {
	u8 colors = 0u;
	for (u16 y = kEdgeStressRoi.y0; y < kEdgeStressRoi.y1; ++y) {
		for (u16 x = kEdgeStressRoi.x0; x < kEdgeStressRoi.x1; ++x) {
			const u8 color = static_cast<u8>((pixel(planes, 0u, x, y) ? 1u : 0u) |
							(pixel(planes, 1u, x, y) ? 2u : 0u));
			if (color != 0u) colors = static_cast<u8>(colors | (1u << color));
		}
	}
	return colors;
}

/// ROI comparison uses the normal ±2 px raster tolerance but avoids scanning unrelated black rows.
[[nodiscard]] bool stress_pixels_match(const u8* got, const u8* want, u32& border_exceptions) {
	border_exceptions = 0u;
	for (u8 p = 0u; p < kPlanes; ++p) {
		for (u16 y = kEdgeStressRoi.y0; y < kEdgeStressRoi.y1; ++y) {
			for (u16 x = kEdgeStressRoi.x0; x < kEdgeStressRoi.x1; ++x) {
				const bool expected = pixel(want, p, x, y);
				const bool actual = pixel(got, p, x, y);
				if (expected == actual) continue;
				bool nearby = false;
				for (s32 dy = -2; dy <= 2 && !nearby; ++dy) {
					for (s32 dx = -2; dx <= 2 && !nearby; ++dx) {
						const s32 nx = static_cast<s32>(x) + dx;
						const s32 ny = static_cast<s32>(y) + dy;
						if (nx >= 0 && nx < static_cast<s32>(kWidth) && ny >= 0 &&
						    ny < static_cast<s32>(kWorldH)) {
							nearby = pixel(expected ? got : want, p, static_cast<u16>(nx),
								       static_cast<u16>(ny));
						}
					}
				}
				if (!nearby) {
					// At the last two viewport rows/columns, the ±2 comparison may point
					// outside the bitmap; count that allowed clip-edge difference explicitly.
					const bool at_viewport_edge = x <= kRasterTolerance ||
						static_cast<u32>(x) + static_cast<u32>(kRasterTolerance) >=
							static_cast<u32>(kWidth - 1u) ||
						y <= kRasterTolerance ||
						static_cast<u32>(y) + static_cast<u32>(kRasterTolerance) >=
							static_cast<u32>(kWorldH - 1u);
					if (at_viewport_edge) {
						++border_exceptions;
						continue;
					}
					return false;
				}
			}
		}
	}
	return true;
}

[[nodiscard]] u32 exact_stress_plane_differences(const u8* got, const u8* want) {
	u32 count = 0u;
	for (u8 p = 0u; p < kPlanes; ++p) {
		for (u16 y = kEdgeStressRoi.y0; y < kEdgeStressRoi.y1; ++y) {
			for (u16 x = kEdgeStressRoi.x0; x < kEdgeStressRoi.x1; ++x) {
				if (pixel(got, p, x, y) != pixel(want, p, x, y)) ++count;
			}
		}
	}
	return count;
}

/// Verifies the frozen animation sample against CPU and preserves exact-difference telemetry.
[[nodiscard]] u32 check_edge_stress_bitmap(const u8* got, const u8* want) {
	u32 detail = 0u;
	const u8 cpu_colors = stress_color_mask(want);
	const u8 blitter_colors = stress_color_mask(got);
	if (cpu_colors != 0u) detail |= 1u << 0u;
	if (blitter_colors == cpu_colors) detail |= 1u << 1u;
	detail |= static_cast<u32>(cpu_colors & 0x0fu) << 4u;
	u32 border_exceptions = 0u;
	if (stress_pixels_match(got, want, border_exceptions)) detail |= 1u << 2u;
	bool hud_clean = true;
	for (u8 p = 0u; p < kPlanes && hud_clean; ++p) {
		for (u16 y = kWorldH; y < kHeight && hud_clean; ++y) {
			for (u16 x = 0u; x < kWidth; ++x) {
				if (pixel(got, p, x, y)) { hud_clean = false; break; }
			}
		}
	}
	if (hud_clean) detail |= 1u << 3u;
	const u32 exact = exact_stress_plane_differences(got, want);
	const u32 bounded = exact > 0xffffu ? 0xffffu : exact;
	detail |= bounded << 16u;
	const u32 border_count = border_exceptions > 0xffu ? 0xffu : border_exceptions;
	detail |= border_count << 8u;
	return detail;
}

/// Checks four nonempty corner ROIs, same-plane CPU agreement, and a clean HUD band.
[[nodiscard]] u32 check_corner_bitmap(const u8* got, const u8* want, u32 face_count,
				      const u8* faces_per_corner) {
	if constexpr (K_L1_EDGE_STRESS) {
		(void)face_count;
		(void)faces_per_corner;
		return check_edge_stress_bitmap(got, want);
	}
	u32 detail = 0u;
	bool all_reference_corners = true;
	bool all_blitter_corners = true;
	for (u8 i = 0u; i < 4u; ++i) {
		const Roi& r = kCornerRois[i];
		all_reference_corners = all_reference_corners && corner_is_clipped(want, r);
		all_blitter_corners = all_blitter_corners && corner_is_clipped(got, r);
	}
	if (all_reference_corners) detail |= 1u << 0u;
	if (all_blitter_corners) detail |= 1u << 1u;
	if (corner_pixels_match(got, want)) detail |= 1u << 2u;
	bool hud_clean = true;
	for (u8 p = 0u; p < kPlanes && hud_clean; ++p) {
		for (u16 y = kWorldH; y < kHeight && hud_clean; ++y) {
			for (u16 x = 0u; x < kWidth; ++x) {
				if (pixel(got, p, x, y)) { hud_clean = false; break; }
			}
		}
	}
	if (hud_clean) detail |= 1u << 3u;
	bool expected_faces = face_count == 6u;
	for (u8 i = 0u; i < 4u; ++i) {
		expected_faces = expected_faces && faces_per_corner[i] == kExpectedFacesPerCorner[i];
	}
	if (expected_faces) detail |= 1u << 4u;
	// Per-corner face counts fail READY if culling removes a visible polygon or retains a hidden one.
	if (all_reference_corners && all_blitter_corners) detail |= 1u << 5u;
	return detail;
}

struct Render3dCornersProbe {
	/// Builds four static clipped cubes, runs Blitter and CPU fills, then publishes the checks.
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = 1u;
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::display(res), scene::palette(kPalette, 0u, 4u))) {
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<u32>(m_scene.config_error().code));
			return;
		}
		m_scene.takeover(backend);
		const eng::PlaneBytes display = m_scene.buffer(0u);
		m_reference = backend.memory_manager().chip().reserve<eng::PlaneTag>(
			static_cast<u32>(kPlaneBytes) * kPlanes + 16u, 16u);
		if (!m_reference.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootReference);
			return;
		}
		(void)backend.blitter_clear(display, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);
		(void)backend.blitter_clear(m_reference.view, kPlanes, kRowBytes, kPlaneBytes,
					    kWidth, kHeight);

		s16 corner_xs[kMaxCornerFaces][16] {};
		s16 corner_ys[kMaxCornerFaces][16] {};
		eng::graphics::PlanePolygon corner_faces[kMaxCornerFaces] {};
		u8 faces_per_corner[4] {};
		const u8 face_count = build_corner_faces(corner_faces, corner_xs, corner_ys,
							 faces_per_corner);
		if (face_count > kMaxCornerFaces) {
			eng::debug::mark_failed(g_eng_run_status, kBootFaceCapacity);
			return;
		}
		(void)backend.fill_polygons_by_plane(corner_faces, face_count, display, kRowBytes,
						     kPlaneBytes, kPlanes, kWidth, kWorldH);
		(void)eng::graphics::fill_polygons_by_plane_cpu(
			eng::Span<const eng::graphics::PlanePolygon>(corner_faces, face_count),
			m_reference.view, kRowBytes, kPlaneBytes, kPlanes, kWidth, kWorldH);
		const u32 detail = check_corner_bitmap(display.data(), m_reference.view.data(), face_count,
						       faces_per_corner);
		if constexpr (K_L1_EDGE_STRESS_CPU_DISPLAY) {
			// El segundo build muestra el oráculo CPU en la misma pose para inspección A/B.
			for (u8 p = 0u; p < kPlanes; ++p) {
				const u32 plane_offset = static_cast<u32>(p) * kPlaneBytes;
				for (u16 y = 0u; y < kWorldH; ++y) {
					const u32 row_offset = plane_offset + static_cast<u32>(y) * kRowBytes;
					for (u16 x = 0u; x < kRowBytes; ++x) {
						display.data()[row_offset + x] = m_reference.view.data()[row_offset + x];
					}
				}
			}
		}

		playfield::ContiguousPlayfield pf {};
		(void)pf.bind_raw(display.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth,
				  kHeight, kPlanes, kPlaneBytes);
		playfield::Surface hud {pf, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY), "L1-005 CAMERA CORNERS", 3u);
		char label[40] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY + 10u), label, 3u);
		eng::debug::mark_ready(g_eng_run_status, detail);
	}

	/// No per-frame work: the four corner poses are immutable test fixtures.
	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	/// The bitmap is complete before READY; no additional render pass is required.
	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	/// Uses the demo 088 camera/model constants to emit only front-facing clipped cube faces.
	[[nodiscard]] static u8 build_corner_faces(eng::graphics::PlanePolygon* out,
						   s16 xs[kMaxCornerFaces][16],
						   s16 ys[kMaxCornerFaces][16],
						   u8 faces_per_corner[4]) {
		using CT = eng::math::scalar_traits<eng::coord>;
		using CamVec = eng::math3d::Vec3t<eng::coord>;
		eng::math::Affine<3, eng::real, eng::coord> model =
			eng::math::Affine<3, eng::real, eng::coord>::identity();
		if constexpr (K_L1_EDGE_STRESS) {
			eng::math3d::load_rotate(model.m, eng::retro::turns(kEdgeStressRotateX),
					 eng::retro::turns(kEdgeStressRotateY),
					 eng::retro::turns(kEdgeStressRotateZ));
		} else {
			eng::math3d::load_rotate(model.m, eng::retro::turns(kTilt), eng::retro::turns(kTilt),
					  eng::retro::turns(0u));
		}
		CamVec world[8] {};
		eng::math3d::mesh_transform(eng::Span<const CamVec>(kCube, 8u), model,
					    eng::Span<CamVec>(world, 8u));
		eng::graphics::Camera3D<eng::coord> cam {};
		cam.focal = CT::from_int(kFocal);
		cam.cx = 160;
		cam.cy = 116;
		cam.near_z = CT::from_int(64);
		cam.half_w = CT::from_int(160);
		cam.half_h = CT::from_int(116);
		const CamVec origin {};
		u8 count = 0u;
		for (u8 pose_index = 0u; pose_index < kPoseCount; ++pose_index) {
			const CornerPose pose = K_L1_EDGE_STRESS ? kEdgeStressPose : kCornerPoses[pose_index];
			const u8 first_face = count;
			CamVec camera_vertices[8] {};
			for (u8 i = 0u; i < 8u; ++i) {
				camera_vertices[i] = CamVec {{world[i].v[0] + CT::from_int(pose.x),
							     world[i].v[1] + CT::from_int(pose.y),
							     world[i].v[2] + CT::from_int(kCubeZ)}};
			}
			for (u8 fi = 0u; fi < 6u; ++fi) {
				if (!eng::math3d::face_visible(camera_vertices[kCubeFaces[fi][0]],
							       camera_vertices[kCubeFaces[fi][1]],
							       camera_vertices[kCubeFaces[fi][2]], origin)) {
					continue;
				}
				if (count >= kMaxCornerFaces) return static_cast<u8>(kMaxCornerFaces + 1u);
				eng::graphics::CamVertex<eng::coord> input[4] {}, clipped[16] {};
				for (u8 k = 0u; k < 4u; ++k) {
					const CamVec& v = camera_vertices[kCubeFaces[fi][k]];
					input[k] = {v.v[0], v.v[1], v.v[2]};
				}
				const u8 n = eng::graphics::clip_camera_polygon(input, 4u, cam, clipped, 16u);
				if (n < 3u) continue;
				s16 min_x = 32767, max_x = -32768, min_y = 32767, max_y = -32768;
				bool projected = true;
				for (u8 k = 0u; k < n; ++k) {
					projected = eng::graphics::project_camera_vertex(clipped[k], cam, xs[count][k],
										 ys[count][k]) && projected;
					if (xs[count][k] < min_x) min_x = xs[count][k];
					if (xs[count][k] > max_x) max_x = xs[count][k];
					if (ys[count][k] < min_y) min_y = ys[count][k];
					if (ys[count][k] > max_y) max_y = ys[count][k];
				}
				if (!projected || max_x - min_x < 2 || max_y - min_y < 2 ||
				    eng::graphics::screen_area2(xs[count], ys[count], n) >= 0) {
					continue;
				}
				out[count] = {xs[count], ys[count], n, kCubeColors[fi]};
				++count;
			}
			faces_per_corner[pose_index] = static_cast<u8>(count - first_face);
		}
		return count;
	}

	/// Scene owns the visible bitmap; the independent CPU oracle uses this second Chip buffer.
	scene::Scene m_scene {};
	eng::Block<eng::PlaneTag> m_reference {};
};

Render3dCornersProbe g_game {};

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
