#!/usr/bin/env node
// Informe de codegen 68000 de la librería de matemáticas: compila una sonda con una
// función por construcción y reporta instrucciones, muls.w, shifts, libcalls y si el
// bucle queda plegado o desenrollado. Es la medición reproducible de "qué genera g++".
//
// Uso: node tools/analyze/codegen-report.mjs
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '../..');
const BIN = process.env.AMIGA_BIN_PATH || 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32';
const CXX = `${BIN}/opt/bin/m68k-amiga-elf-g++.exe`;
const SRC = `${ROOT}/out/tmp/codegen-probe.cpp`;
const ASM = `${ROOT}/out/tmp/codegen-probe.s`;

const probe = `#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/light.hpp>
#include <eng/core/interp.hpp>
#include <eng/core/geometry.hpp>
#include <eng/core/scalar_ops.hpp>
#include <eng/core/spline.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/core/minifloat.hpp>
#include <eng/core/minifloat_math.hpp>
#include <eng/retro/fixed_q.hpp>
#include <eng/retro/lib2d.hpp>
#include <eng/retro/minifloat_fixed.hpp>
#include <eng/core/util/hash.hpp>
#include <eng/core/util/hash_map.hpp>
#include <eng/core/util/hash_set.hpp>
#include <eng/core/util/vector.hpp>
#include <eng/core/util/chunked_vector.hpp>
#include <eng/core/util/pool.hpp>
#include <eng/core/util/priority_queue.hpp>
#include <eng/core/util/intrusive_list.hpp>
#include <eng/core/util/dynamic_hash_map.hpp>
#include <eng/core/util/stats.hpp>
#include <eng/core/util/color.hpp>
#include <eng/core/util/collision.hpp>
#include <eng/core/util/text.hpp>
#include <eng/core/util/grid.hpp>
#include <eng/core/util/broadphase.hpp>
#include <eng/core/util/pathfinding.hpp>
#include <eng/core/util/array.hpp>
#include <eng/ai/planning/goap.hpp>
#include <eng/core/util/state_machine.hpp>
#include <eng/core/util/event.hpp>
#include <eng/ai/decision/agent_fsm.hpp>
#include <eng/ai/decision/blackboard.hpp>
#include <eng/ai/decision/utility.hpp>
#include <eng/ai/decision/behavior_tree.hpp>
#include <eng/ai/navigation/flow_field.hpp>
#include <eng/ai/navigation/waypoints.hpp>
#include <eng/ai/navigation/navmesh_lite.hpp>
#include <eng/ai/steering/steering.hpp>
#include <eng/ai/perception/influence_map.hpp>
#include <eng/ai/perception/agent_memory.hpp>
#include <eng/board/rules/chess/rules.hpp>
#include <eng/board/explain/explain.hpp>
#include <eng/parallel/parallel.hpp>
#include <eng/core/util/union_find.hpp>
#include <eng/core/util/sparse_set.hpp>
#include <eng/core/util/bitstream.hpp>
#include <eng/core/util/dynamic_bitset.hpp>
#include <eng/core/util/string_interner.hpp>
#include <eng/core/util/graph.hpp>
#include <eng/core/util/lru_cache.hpp>
#include <eng/core/util/task.hpp>
#include <eng/core/util/interval.hpp>
#include <eng/core/util/variant.hpp>
#include <eng/core/random.hpp>
#include <eng/core/util/dsp.hpp>
#include <eng/core/fixed_math.hpp>
#include <eng/core/geometry.hpp>
#include <eng/core/sort.hpp>
#include <eng/graphics/mesh_renderer.hpp>
#include <eng/platform/amiga/lib3d.hpp>
#include <eng/platform/amiga/object3d.hpp>
using namespace eng::math;
using namespace eng::retro;
using namespace eng::math3d;
using eng::s16;
using eng::s32;
using eng::u16;

// Gate de layout GOAP (m68k): fija los sizeof medidos. Si cambian, la compilacion
// cruzada falla y hay que revisar el presupuesto de RAM del planificador.
static_assert(sizeof(eng::ai::Goap<>::State) == 4u, "Goap<32>::State");
static_assert(sizeof(eng::ai::Goap<>::Action) == 22u, "Goap<32>::Action");
static_assert(sizeof(eng::ai::Goap<>::Planner<128>) == 4358u, "Goap<32>::Planner<128>");
static_assert(sizeof(eng::ai::Goap<>::Planner<256>) == 8486u, "Goap<32>::Planner<256>");
static_assert(sizeof(eng::ai::Goap<64>::State) == 8u, "Goap<64>::State");
static_assert(sizeof(eng::ai::Goap<64>::Action) == 38u, "Goap<64>::Action");
static_assert(sizeof(eng::ai::Goap<64>::Planner<128>) == 6454u, "Goap<64>::Planner<128>");
static_assert(sizeof(eng::ai::Goap<64>::Planner<256>) == 12630u, "Goap<64>::Planner<256>");

struct HalfEvenPolicy { using Round = rounding::HalfEven; using Overflow = overflow::Wrap; };
using q14 = Fixed<s16, 14>;
eng::u16 g_tab[512] {}; // mutable: impide que el optimizador pliegue la tabla a constante
extern "C" s16 c_shade(s32 v, s32 e) { return light_ops<>::shade(v, e, g_tab); }

extern "C" q24 c_mul_q12(s16 a, s16 b) { return q12{a} * q12{b}; }
extern "C" Fixed<s32, 26> c_mul_mixed(s16 a, s16 b) { return q12{a} * q14{b}; } // 4.12*2.14 -> exp 26
extern "C" s16 c_mul_mixed_narrow(s16 a, s16 b) { return (q12{a} * q14{b}).rescale<12>().cast<s16>().v; }
extern "C" s16 c_norm_trunc(q24 a) { return a.rescale<12>().cast<s16>().v; }
extern "C" s16 c_norm_halfup(q24 a) { return a.retag<RoundPolicy>().rescale<12>().cast<s16>().v; }
extern "C" s16 c_norm_even(q24 a) { return a.retag<HalfEvenPolicy>().rescale<12>().cast<s16>().v; }
extern "C" q12 c_dot2(s16 a0,s16 b0,s16 c0,s16 d0){ return dot(q12{a0},q12{b0},q12{c0},q12{d0}); }
extern "C" q12 c_dot3(s16 a0,s16 b0,s16 c0,s16 d0,s16 e0,s16 f0){ return dot(q12{a0},q12{b0},q12{c0},q12{d0},q12{e0},q12{f0}); }
extern "C" q12 c_dot4(s16 a0,s16 b0,s16 c0,s16 d0,s16 e0,s16 f0,s16 g0,s16 h0){ return dot(q12{a0},q12{b0},q12{c0},q12{d0},q12{e0},q12{f0},q12{g0},q12{h0}); }
extern "C" q12 c_fx_mac(s16 a, s16 b, s16 c){ return mac(q12{a}, q12{b}, q12{c}); }
extern "C" void c_transform3(s16* out, const Mat<3,q12>* m, const Vec<3,q0>* t, const Vec<3,q0>* p) {
	const Affine<3,q12,q0> a{*m,*t}; const Vec<3,q0> r = transform(a,*p);
	for (int i=0;i<3;++i) out[i]=r.v[i].v;
}
extern "C" void c_matmul3(Mat<3,q12>* out, const Mat<3,q12>* a, const Mat<3,q12>* b){ *out = (*a)*(*b); }
extern "C" s16 c_dotrow(const q12* row, s16 x, s16 y, s16 z) {
	const Vec<3,q0> v {{q0{x},q0{y},q0{z}}};
	return dot(row, v).v;
}
extern "C" void c_proj(eng::object3d::Object3D* o, s16* bbox) { eng::lib3d::transform_vertices(*o, 128, 128, bbox); }

// --- Vocabulario generico sobre fixed (mul_norm/div_norm): debe ser nativo ---
extern "C" s16 c_fx_lerp(s16 a, s16 b, s16 t) { return lerp(q12{a}, q12{b}, q12{t}).v; }
extern "C" s16 c_fx_inv_lerp(s16 a, s16 b, s16 v) { return inv_lerp(q12{a}, q12{b}, q12{v}).v; }
extern "C" s16 c_fx_remap(s16 v, s16 lo, s16 hi, s16 olo, s16 ohi) { return remap(q12{v}, q12{lo}, q12{hi}, q12{olo}, q12{ohi}).v; }
extern "C" s16 c_fx_cross2(s16 ax, s16 ay, s16 bx, s16 by) { return cross2(Vec<2,q12>{{q12{ax},q12{ay}}}, Vec<2,q12>{{q12{bx},q12{by}}}).v; }
extern "C" void c_fx_rotate2(s16* o, s16 x, s16 y, s16 c, s16 s) { const Vec<2,q12> r = rotate2(Vec<2,q12>{{q12{x},q12{y}}}, q12{c}, q12{s}); o[0]=r.v[0].v; o[1]=r.v[1].v; }

// --- Vocabulario nuevo (scalar_ops/back/bezier/repeat): debe ser nativo en fixed ---
extern "C" s16 c_fx_move_towards(s16 cur, s16 target, s16 d) { return move_towards(q12{cur}, q12{target}, q12{d}).v; }
extern "C" s16 c_fx_deadzone(s16 x, s16 dead) { return deadzone(q12{x}, q12{dead}).v; }
extern "C" s16 c_fx_repeat(s16 t, s16 len) { return repeat(q12{t}, q12{len}).v; }
extern "C" s16 c_fx_pingpong(s16 t, s16 len) { return pingpong(q12{t}, q12{len}).v; }
extern "C" s16 c_fx_ease_back(s16 t) { return ease_in_out_back(q12{t}).v; }
extern "C" s16 c_fx_bezier3(s16 p0, s16 p1, s16 p2, s16 p3, s16 t) {
	return bezier3(q12{p0}, q12{p1}, q12{p2}, q12{p3}, q12{t}).v;
}
extern "C" s16 c_fx_minmax(s16 a, s16 b) { return max(min(q12{a}, q12{b}), q12{0}).v; }

// --- mesh3d: producto mixto del culling. Debe usar muls.w (nunca __mulsi3) porque las
// diferencias de coordenada son s16 y el arith<s16> fuerza la multiplicacion nativa.
extern "C" s32 c_face_area(const Vec3* a, const Vec3* b, const Vec3* c, const Vec3* cam) {
	return face_signed_area(*a, *b, *c, *cam);
}

// --- 2D/3D: proyeccion, recorte y transform del objeto. Productos 16x16 -> mul16. ---
extern "C" void c_proj_persp(s16* out, const Vec3* v, s16 focal, s16 cx, s16 cy) {
	eng::graphics::project_perspective(*v, focal, cx, cy, out[0], out[1]);
}
extern "C" int c_clip_line(const eng::retro::Rect* win, eng::retro::Vec2* a, eng::retro::Vec2* b) {
	return eng::retro::clip_line(*win, *a, *b) ? 1 : 0;
}
extern "C" void c_update_obj(eng::object3d::Object3D* o) {
	eng::object3d::update_object_transformation(*o);
}

// --- MiniFloat16: aritmetica, matematicas y puente con fixed (sin libgcc) ---
extern "C" u16 c_mf_mul(u16 a, u16 b) { return (MiniFloat16::from_raw(a) * MiniFloat16::from_raw(b)).raw; }
extern "C" u16 c_mf_div(u16 a, u16 b) { return (MiniFloat16::from_raw(a) / MiniFloat16::from_raw(b)).raw; }
extern "C" u16 c_mf_sqrt(u16 a) { return sqrt(MiniFloat16::from_raw(a)).raw; }
extern "C" u16 c_mf_math(u16 a) {
	const MiniFloat16 x = MiniFloat16::from_raw(a);
	return (sin(x) + exp(x) + log(x) + sqrt(x)).raw;
}
extern "C" s16 c_mf_fixed_mul(u16 r, s16 v) { return mul_fix(MiniFloat16::from_raw(r), v); }
extern "C" u16 c_mf_mac(u16 a, u16 b, u16 c) { return mac(MiniFloat16::from_raw(a), MiniFloat16::from_raw(b), MiniFloat16::from_raw(c)).raw; }
extern "C" u16 c_mf_sin(u16 a) { return sin(MiniFloat16::from_raw(a)).raw; }
extern "C" u16 c_mf_move_towards(u16 cur, u16 target, u16 d) {
	return move_towards(MiniFloat16::from_raw(cur), MiniFloat16::from_raw(target), MiniFloat16::from_raw(d)).raw;
}
extern "C" u16 c_mf_smooth_damp(u16 cur, u16 target, u16 rate, u16 dt) {
	return smooth_damp(MiniFloat16::from_raw(cur), MiniFloat16::from_raw(target),
			   MiniFloat16::from_raw(rate), MiniFloat16::from_raw(dt)).raw;
}
extern "C" u16 c_mf_bezier3(u16 p0, u16 p1, u16 p2, u16 p3, u16 t) {
	return bezier3(MiniFloat16::from_raw(p0), MiniFloat16::from_raw(p1), MiniFloat16::from_raw(p2),
		       MiniFloat16::from_raw(p3), MiniFloat16::from_raw(t)).raw;
}
extern "C" void c_matmul3_mf(u16* out, const u16* a, const u16* b) {
	Mat<3, MiniFloat16> A {}, B {};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) {
			A.m[i][j] = MiniFloat16::from_raw(a[i * 3 + j]);
			B.m[i][j] = MiniFloat16::from_raw(b[i * 3 + j]);
		}
	const Mat<3, MiniFloat16> R = A * B;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) out[i * 3 + j] = R.m[i][j].raw;
}
extern "C" u16 c_mf_loop(const u16* xs, int n) {
	MiniFloat16 acc = MiniFloat16::zero();
	for (int i = 0; i < n; ++i) {
		const MiniFloat16 x = MiniFloat16::from_raw(xs[i]);
		acc = acc + sqrt(x) + sin(x) + exp(x);
	}
	return acc.raw;
}
extern "C" void c_mf_transform(const u16* mm, const s16* pp, s16* out) {
	Mat<3, MiniFloat16> m {};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) m.m[i][j] = MiniFloat16::from_raw(mm[i * 3 + j]);
	const Vec<3, fix> p = {pp[0], pp[1], pp[2]};
	const Vec<3, fix> r = transform_fix(m, p);
	for (int i = 0; i < 3; ++i) out[i] = r.v[i];
}

// --- eng::util: hashes y contenedores sin heap. Deben compilarse a aritmetica nativa
// (los hashes no usan multiplicacion de 32x32) y sin libcalls de libgcc. El gate global
// de FORBIDDEN (__mulsi3/__divsi3/__udivsi3) cubre estas funciones.
namespace eu = eng::util;
extern "C" eng::u32 c_hash_u16(u16 a) { return eu::hash_u16(a); }
extern "C" eng::u32 c_hash_u32(eng::u32 a) { return eu::hash_u32(a); }
extern "C" u16 c_hashmap_find(u16 key) {
	eu::HashMap<u16, u16, 16> m;
	m.insert(key, 7u);
	m.erase(key);
	const u16* p = m.find(key);
	return static_cast<u16>(p != nullptr ? *p : 0u);
}
extern "C" u16 c_hashset_contains(u16 v) {
	eu::HashSet<u16, 16> s;
	s.insert(v);
	s.erase(v);
	return static_cast<u16>(s.contains(v) ? 1u : 0u);
}
extern "C" u16 c_vector_grow(eng::u8* scratch, eng::u32 bytes) {
	eu::BumpAlloc alloc {eng::Span<eng::u8> {scratch, bytes}};
	eu::Vector<u16, eu::BumpAlloc> v {alloc};
	for (u16 i = 0; i < 20u; ++i) v.push_back(i);
	return static_cast<u16>(v.size());
}
extern "C" u16 c_chunked_push(eng::u8* scratch, eng::u32 bytes) {
	eu::BumpAlloc alloc {eng::Span<eng::u8> {scratch, bytes}};
	eu::ChunkedVector<u16, 4, 4, eu::BumpAlloc> c {alloc};
	for (u16 i = 0; i < 12u; ++i) c.push_back(i);
	return static_cast<u16>(c.size());
}
struct ProbeNode : eu::IntrusiveLink<ProbeNode> { u16 v; };
extern "C" u16 c_pool_ops(u16 seed) {
	eu::Pool<u16, 8> p;
	const auto h0 = p.add();
	const auto h1 = p.add();
	if (u16* v = p.get(h0)) *v = seed;
	p.remove(h0);
	return static_cast<u16>(p.size() + (p.valid(h1) ? 1u : 0u));
}
extern "C" u16 c_pq_ops(u16 seed) {
	eu::PriorityQueue<u16, 8> q;
	q.push(seed);
	q.push(static_cast<u16>(seed + 3u));
	q.push(static_cast<u16>(seed + 1u));
	q.pop();
	return q.empty() ? 0u : q.top();
}
extern "C" u16 c_ilist_ops(u16 seed) {
	ProbeNode a, b;
	a.v = seed;
	b.v = static_cast<u16>(seed + 1u);
	eu::IntrusiveList<ProbeNode> l;
	l.push_back(&a);
	l.push_back(&b);
	l.erase(&a);
	return static_cast<u16>(l.size());
}
extern "C" void c_stable_sort(u16* data, u16* scratch, int n) {
	eng::Span<u16> items {data, static_cast<eng::usize>(n)};
	eng::Span<u16> sc {scratch, static_cast<eng::usize>(n)};
	eng::stable_sort(items, [](u16 a, u16 b) { return a < b; }, sc);
}
extern "C" u16 c_nth_element(u16* data, int n, int k) {
	eng::Span<u16> items {data, static_cast<eng::usize>(n)};
	eng::nth_element(items, static_cast<eng::usize>(k), [](u16 a, u16 b) { return a < b; });
	return items[static_cast<eng::usize>(k)];
}
extern "C" u16 c_radix_u16(u16* data, u16* scratch, int n) {
	eng::Span<u16> items {data, static_cast<eng::usize>(n)};
	eng::Span<u16> sc {scratch, static_cast<eng::usize>(n)};
	return eng::radix_sort_u16(items, sc) ? items[0] : static_cast<u16>(0u);
}
extern "C" u16 c_dyn_hashmap(eng::u8* scratch, eng::u32 bytes, u16 seed) {
	eu::BumpAlloc alloc {eng::Span<eng::u8> {scratch, bytes}};
	eu::DynamicHashMap<u16, u16, eu::BumpAlloc> m {alloc};
	for (u16 i = 0; i < 40u; ++i) m.insert_or_assign(static_cast<u16>(i * 7u), seed);
	const u16* p = m.find(seed);
	m.erase(seed);
	return static_cast<u16>(m.size() + (p != nullptr ? 1u : 0u));
}
extern "C" s16 c_stats_ops(const s16* data, int n) {
	eng::Span<const q12> xs {reinterpret_cast<const q12*>(data), static_cast<eng::usize>(n)};
	return static_cast<s16>(eu::mean(xs).v + eu::variance(xs).v);
}
extern "C" u16 c_color_lerp(u16 a, u16 b, u16 num, u16 den) {
	return eu::lerp444(a, b, num, den);
}
extern "C" u16 c_palette_ops(u16 num, u16 den) {
	u16 a[8];
	u16 b[8];
	u16 dst[8];
	for (u16 i = 0; i < 8u; ++i) {
		a[i] = static_cast<u16>(i * 0x111u);
		b[i] = static_cast<u16>(0xfffu - i * 0x111u);
	}
	const eng::usize n1 = eu::palette_lerp(eng::Span<eng::u16> {dst}, eng::Span<const eng::u16> {a},
					       eng::Span<const eng::u16> {b}, num, den);
	const eng::usize n2 = eu::palette_scale(eng::Span<eng::u16> {dst}, eng::Span<const eng::u16> {a},
						num, den);
	const eng::u16 grad = eu::gradient444(eng::Span<const eng::u16> {a}, num, den);
	return static_cast<u16>(dst[0] + dst[7] + grad + static_cast<u16>(n1 + n2));
}
extern "C" u16 c_collision_ops(s16 ax, s16 ay, s16 bx, s16 by, s16 cx, s16 cy, s16 dx, s16 dy) {
	const eng::Point2s a {ax, ay};
	const eng::Point2s b {bx, by};
	const eng::Point2s c {cx, cy};
	const eng::Point2s d {dx, dy};
	return static_cast<u16>((eu::segments_intersect(a, b, c, d) ? 1u : 0u) +
				(eu::circle_overlap(a, 4, c, 4) ? 2u : 0u));
}
extern "C" u16 c_text_ops(const char* s, eng::u32 n) {
	const eu::StringView text {s, static_cast<eng::usize>(n)};
	eng::u32 v = 0u;
	if (!eu::parse_u32(eu::trim(text), v)) v = 0u;
	eu::StaticString<16> buf;
	eu::to_chars_u32(buf, v);
	return static_cast<u16>(buf.size());
}
extern "C" s16 c_grid_ops(s16 tx, s16 ty) {
	const eng::Point2s px = eu::grid_to_world(eu::TileCoord {tx, ty}, 16u, 16u);
	const eu::TileCoord back = eu::world_to_grid<16, 16>(px.x, px.y);
	eu::Hex vecinos[6];
	eu::hex_neighbors(eu::Hex {tx, ty}, vecinos);
	return static_cast<s16>(back.x + back.y + eu::hex_distance(vecinos[0], vecinos[3]));
}
extern "C" u16 c_broadphase_ops(s16 x, s16 y) {
	eu::SpatialHash<8, 8, 8, 16> g;
	g.clear();
	g.insert(1u, x, y);
	g.insert(2u, static_cast<s16>(x + 3), y);
	eng::u16 hits[4];
	return static_cast<u16>(g.query(eu::Aabb {0, 0, 32, 32}, eng::Span<eng::u16> {hits, 4}));
}
extern "C" u16 c_pathfinding_ops(u16 start, u16 goal) {
	static eng::s16 came[64];
	static eng::u16 queue[64];
	static eng::u16 gs[64];
	static eng::u8 closed[64];
	static eng::u16 path[64];
	const auto walk = [](u16) { return true; };
	const u16 s = static_cast<u16>(start % 64u);
	const u16 g = static_cast<u16>(goal % 64u);
	if (!eu::bfs<8, 8>(s, g, walk, eng::Span<eng::s16> {came, 64},
			   eng::Span<eng::u16> {queue, 64})) {
		return 0u;
	}
	const eng::usize bl = eu::reconstruct_path<8, 8>(eng::Span<const eng::s16> {came, 64}, s, g,
							 eng::Span<eng::u16> {path, 64});
	const bool aok = eu::astar<8, 8>(s, g, walk,
					 [](u16, u16) { return static_cast<u16>(1u); },
					 eng::Span<eng::s16> {came, 64},
					 eng::Span<eng::u16> {gs, 64},
					 eng::Span<eng::u8> {closed, 64});
	return static_cast<u16>(bl + (aok ? 1u : 0u));
}
extern "C" u16 c_goap_ops(u16 seed) {
	using Ai = eng::ai::Goap<>;
	constexpr eng::util::Array<Ai::Action, 4> acts { {
		Ai::Builder {}.require(0).produce(1).build(),
		Ai::Builder {}.require(1).produce(2).build(),
		Ai::Builder {}.require(2).produce(3).build(),
		Ai::Builder {}.produce(3).build(),
	} };
	Ai::State start {};
	start.facts.set(static_cast<eng::usize>(seed % 8u));
	Ai::Goal goal {};
	goal.want_true.facts.set(static_cast<eng::ai::Fact>(3u));
	Ai::Planner<32> planner;
	eng::u16 plan[4] {};
	const eng::usize n = planner.plan(start, goal, acts.span(), eng::Span<eng::u16> {plan, 4});
	return static_cast<u16>(n + (planner.found() ? 1u : 0u));
}
extern "C" u16 c_goap64_ops(u16 seed) {
	using Big = eng::ai::Goap<64>;
	constexpr eng::util::Array<Big::Action, 2> acts { {
		Big::Builder {}.require(0).produce(63).build(),
		Big::Builder {}.require(63).produce(1).build(),
	} };
	Big::Goal goal {};
	goal.want_true.facts.set(static_cast<eng::ai::Fact>(1u));
	Big::Planner<16> planner;
	eng::u16 plan[2] {};
	Big::State start {};
	start.facts.set(static_cast<eng::ai::Fact>(seed % 8u));
	const eng::usize n = planner.plan(start, goal, acts.span(), eng::Span<eng::u16> {plan, 2});
	return static_cast<u16>(n + (planner.found() ? 1u : 0u));
}
extern "C" u16 c_state_machine_ops(u16 seed) {
	enum class S : eng::u8 { A, B, C };
	enum class E : eng::u8 { Go };
	static constexpr eng::util::Transition<S, E> table[] = {
		{S::A, E::Go, S::B},
		{S::B, E::Go, S::C},
		{S::C, E::Go, S::A},
	};
	eng::util::StateMachine<S, E> fsm {S::A, table};
	for (eng::u16 i = 0; i < (seed & 3u); ++i) {
		fsm.dispatch(E::Go);
	}
	return static_cast<u16>(static_cast<u16>(fsm.current()) + fsm.transition_count());
}
extern "C" u16 c_event_ops(u16 seed) {
	eng::util::Event<void(eng::u16), 4> ev;
	auto a = [](eng::u16) {};
	auto b = [](eng::u16) {};
	ev.subscribe(a);
	ev.subscribe(b);
	ev.emit(seed);
	ev.clear();
	return static_cast<u16>(ev.size());
}
extern "C" u16 c_agent_fsm_ops(u16 seed) {
	enum class S : eng::u8 { A, B, C };
	enum class E : eng::u8 { Go };
	static constexpr eng::util::Transition<S, E> table[] = {
		{S::A, E::Go, S::B},
		{S::B, E::Go, S::C},
		{S::C, E::Go, S::A},
	};
	eng::ai::AgentFsm<S, E, 3> fsm {S::A, table};
	for (eng::u16 i = 0; i < (seed & 3u); ++i) {
		fsm.dispatch(E::Go);
	}
	return static_cast<u16>(static_cast<u16>(fsm.current()) + fsm.transition_count());
}
extern "C" u16 c_blackboard_ops(u16 seed) {
	enum class K : eng::u16 { A, B, C, Count };
	eng::ai::Blackboard<K, eng::s32, static_cast<eng::usize>(K::Count)> bb;
	bb.set(K::A, seed);
	bb.set(K::B, static_cast<eng::s32>(seed) + 1);
	const eng::s32* p = bb.find(K::A);
	bb.erase(K::B);
	return static_cast<u16>((p != nullptr ? *p : 0) + static_cast<eng::s32>(bb.size()));
}
extern "C" s16 c_utility_ops(u16 a, u16 b) {
	eng::ai::Utility u;
	u.add(static_cast<eng::s32>(a), 2);
	u.add(static_cast<eng::s32>(b), 1);
	eng::ai::UtilitySelector<3> sel;
	sel.add(u.score());
	sel.add(static_cast<eng::s32>(b));
	return static_cast<s16>(static_cast<eng::s32>(sel.best()) + u.score());
}
extern "C" u16 c_behavior_tree_ops(u16 seed) {
	(void)seed;
	auto fail = []() { return eng::ai::BtStatus::Failure; };
	auto ok = []() { return eng::ai::BtStatus::Success; };
	eng::ai::BehaviorTree<4> bt;
	bt.add_leaf(fail);
	bt.add_leaf(ok);
	const eng::u16 sel = bt.add_selector(0u, 2u);
	bt.set_root(sel);
	const eng::ai::BtStatus st = bt.tick();
	return static_cast<u16>((st == eng::ai::BtStatus::Success ? 1u : 0u) + bt.node_count());
}
extern "C" u16 c_flow_field_ops(u16 goal) {
	static eng::u16 integ[64];
	static eng::u8 dir[64];
	const eng::u16 goals[1] = {static_cast<eng::u16>(goal % 64u)};
	auto cost = [](eng::u16) -> eng::u16 { return 1u; };
	if (!eng::ai::compute_flow_field<8, 8>(eng::Span<const eng::u16> {goals, 1}, cost,
					       eng::Span<eng::u16> {integ, 64},
					       eng::Span<eng::u8> {dir, 64})) {
		return 0u;
	}
	eng::u16 next = 0u;
	return eng::ai::flow_next<8>(eng::Span<const eng::u8> {dir, 64}, 0u, next)
		       ? next
		       : static_cast<eng::u16>(0u);
}
extern "C" s16 c_steering_ops(s16 px, s16 py, s16 tx, s16 ty) {
	const Vec<2, q12> pos {q12 {px}, q12 {py}};
	const Vec<2, q12> target {q12 {tx}, q12 {ty}};
	const Vec<2, q12> v = eng::ai::seek(pos, target, q12 {64});
	const Vec<2, q12> a = eng::ai::arrive(pos, target, q12 {64}, q12 {256});
	return static_cast<s16>(v.v[0].v + v.v[1].v + a.v[0].v + a.v[1].v);
}
extern "C" s16 c_steering_extra_ops(s16 px, s16 py, s16 tx, s16 ty) {
	const Vec<2, q12> pos {q12 {px}, q12 {py}};
	const Vec<2, q12> target {q12 {tx}, q12 {ty}};
	const Vec<2, q12> vel {q12 {8}, q12 {0}};
	const Vec<2, q12> pv = eng::ai::pursue(pos, target, vel, q12 {64});
	const Vec<2, q12> ev = eng::ai::evade(pos, target, vel, q12 {64});
	const Vec<2, q12> wv = eng::ai::wander(pos, q12 {0}, q12 {64}, q12 {16}, q12 {64});
	const eng::ai::SteerCircle<q12> circles[1] = {{{q12 {64}, q12 {8}}, q12 {16}}};
	const Vec<2, q12> av = eng::ai::avoid_circles(
		pos, target, eng::Span<const eng::ai::SteerCircle<q12>> {circles, 1}, q12 {16},
		q12 {32});
	return static_cast<s16>(pv.v[0].v + ev.v[0].v + wv.v[0].v + av.v[0].v);
}
extern "C" u16 c_waypoints_ops(u16 start, u16 goal) {
	eng::ai::WaypointGraph<8, 8> graph;
	const eng::u16 a = graph.add_node({0, 0});
	const eng::u16 b = graph.add_node({10, 0});
	const eng::u16 c = graph.add_node({20, 0});
	graph.add_edge(a, b, 10u);
	graph.add_edge(b, c, 10u);
	eng::u16 g[8];
	eng::s16 came[8];
	eng::u8 closed[8];
	eng::u16 path[8];
	const eng::u16 s = static_cast<eng::u16>(start % 3u);
	const eng::u16 t = static_cast<eng::u16>(goal % 3u);
	const eng::usize n = graph.find_path(s, t, eng::Span<eng::u16> {g, 8},
					     eng::Span<eng::s16> {came, 8},
					     eng::Span<eng::u8> {closed, 8},
					     eng::Span<eng::u16> {path, 8});
	return static_cast<u16>(n + (g[t] == 0xffffu ? 0u : g[t]));
}
extern "C" u16 c_navmesh_ops(u16 seed) {
	using Mesh = eng::ai::NavMesh<8, 4, 8>;
	Mesh mesh;
	const eng::Point2s a_verts[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
	const eng::Point2s b_verts[4] = {{10, 0}, {20, 0}, {20, 10}, {10, 10}};
	const eng::u16 a = mesh.add_polygon(eng::Span<const eng::Point2s> {a_verts, 4});
	const eng::u16 b = mesh.add_polygon(eng::Span<const eng::Point2s> {b_verts, 4});
	mesh.add_portal(a, b, {10, 0}, {10, 10});
	eng::u16 g[8];
	eng::s16 came[8];
	eng::u8 closed[8];
	eng::Point2s path[8];
	const eng::s16 gx = static_cast<eng::s16>(seed % 15u);
	const eng::usize n = mesh.find_path(
		{5, 5}, {gx, 5}, eng::Span<eng::u16> {g, 8}, eng::Span<eng::s16> {came, 8},
		eng::Span<eng::u8> {closed, 8}, eng::Span<eng::Point2s> {path, 8});
	const eng::usize sn = mesh.find_smooth_path(
		{5, 5}, {gx, 5}, eng::Span<eng::u16> {g, 8}, eng::Span<eng::s16> {came, 8},
		eng::Span<eng::u8> {closed, 8}, eng::Span<eng::Point2s> {path, 8});
	const eng::u16 loc = mesh.locate({5, 5});
	return static_cast<u16>(n + sn + (loc == Mesh::no_poly ? 0u : loc));
}
extern "C" s32 c_influence_map_ops(u16 cell, s32 amount) {
	eng::ai::InfluenceMap<8, 8> map;
	map.clear();
	const eng::u16 i = static_cast<eng::u16>(cell % 64u);
	map.deposit(i, amount);
	map.decay(amount / 2);
	const eng::u16 best = map.strongest();
	return map.at(i) + (best == eng::ai::InfluenceMap<8, 8>::no_cell ? 0 : 1);
}
extern "C" u16 c_agent_memory_ops(u16 ticks) {
	eng::ai::AgentMemory mem;
	mem.see({static_cast<eng::s16>(ticks), 4});
	for (eng::u16 i = 0; i < (ticks & 7u); ++i) {
		mem.tick();
	}
	return static_cast<u16>((mem.fresh(3u) ? 1u : 0u) + mem.ticks_since_seen +
				mem.last_position.y);
}
extern "C" u16 c_union_find_ops(u16 seed) {
	eng::util::UnionFind<64> uf;
	for (eng::u16 i = 0u; i + 1u < 64u; i = static_cast<eng::u16>(i + 2u)) {
		uf.unite(i, static_cast<eng::u16>(i + 1u));
	}
	uf.unite(0u, 2u);
	const eng::u16 r = uf.find(static_cast<eng::u16>(seed % 64u));
	return static_cast<u16>(r + uf.components() + uf.component_size(0u) +
				(uf.connected(0u, 2u) ? 1u : 0u));
}
extern "C" s32 c_sparse_set_ops(u16 seed) {
	eng::util::SparseSet<eng::s32, 64> set;
	for (eng::u16 i = 0u; i < 32u; ++i) {
		set.insert(static_cast<eng::u16>((i * 7u) % 64u), static_cast<eng::s32>(i));
	}
	set.erase(static_cast<eng::u16>(seed % 64u));
	const eng::s32* p = set.find(static_cast<eng::u16>((seed + 1u) % 64u));
	s32 sum = p != nullptr ? *p : 0;
	for (s32 v : set.values()) {
		sum += v;
	}
	return static_cast<s32>(sum + static_cast<s32>(set.size()));
}
extern "C" u16 c_bitstream_ops(eng::u32 value, u16 bits) {
	eng::u8 buf[8] {};
	eng::util::BitWriter bw {eng::Span<eng::u8> {buf, 8}};
	bw.write(value, static_cast<eng::u8>(bits % 32u + 1u));
	bw.write_bool(true);
	eng::util::BitReader br {eng::Span<const eng::u8> {buf, bw.byte_count()}};
	eng::u32 out = 0;
	bool b = false;
	br.read(static_cast<eng::u8>(bits % 32u + 1u), out);
	br.read_bool(b);
	return static_cast<u16>(out + (b ? 1u : 0u));
}
extern "C" u16 c_dynamic_bitset_ops(u16 seed) {
	eng::util::InlineAlloc<64> alloc;
	eng::util::DynamicBitSet<eng::util::InlineAlloc<64>> bits {alloc};
	if (!bits.init(70u)) {
		return 0u;
	}
	for (eng::u16 i = 0u; i < 70u; i = static_cast<eng::u16>(i + 3u)) {
		bits.set(i);
	}
	const eng::usize c = bits.count();
	bits.flip(static_cast<eng::usize>(seed % 70u));
	return static_cast<u16>(c + (bits.any() ? 1u : 0u) + bits.size());
}
extern "C" u16 c_string_interner_ops(u16 seed) {
	eng::util::InlineAlloc<128> arena;
	eng::util::StringInterner<16, eng::util::InlineAlloc<128>> names {arena};
	const eng::u16 a = names.intern("enemy_idle");
	const eng::u16 b = names.intern("enemy_run");
	const eng::u16 c = names.intern("enemy_idle");
	char buf[3] = {'i', static_cast<char>('0' + (seed % 10u)), '\0'};
	names.intern(eng::util::StringView(buf, 2u));
	return static_cast<u16>(a + b + c + names.size() +
				static_cast<eng::u16>(names.lookup(a).size()));
}
extern "C" u16 c_convex_overlap_ops(s16 ax, s16 ay, s16 bx, s16 by) {
	const eng::Point2s a[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
	const eng::Point2s b[4] = {{ax, ay},
				   {static_cast<s16>(ax + 10), ay},
				   {static_cast<s16>(ax + 10), static_cast<s16>(ay + 10)},
				   {ax, static_cast<s16>(ay + 10)}};
	const bool ov = eng::util::convex_overlap(eng::Span<const eng::Point2s> {a, 4},
						  eng::Span<const eng::Point2s> {b, 4});
	const bool in = eng::util::point_in_convex({bx, by}, eng::Span<const eng::Point2s> {a, 4});
	return static_cast<u16>((ov ? 1u : 0u) + (in ? 1u : 0u));
}
extern "C" u16 c_graph_ops(u16 seed) {
	eng::util::Graph<8, 12> g;
	for (eng::u16 i = 0u; i < 6u; ++i) {
		g.add_node();
	}
	g.add_edge(0u, 1u, 2u);
	g.add_edge(1u, 2u, 2u);
	g.add_edge(0u, 3u, 1u);
	g.add_edge(3u, 4u, 1u);
	g.add_edge(4u, 2u, 1u);
	eng::u16 gs[8];
	eng::s16 came[8];
	eng::u8 closed[8];
	eng::u16 out[8];
	eng::u16 q[8];
	auto h = [](eng::u16, eng::u16) { return static_cast<eng::u16>(0u); };
	const eng::usize a = eng::util::graph_astar(
		g, 0u, 2u, h, eng::Span<eng::u16> {gs, 8}, eng::Span<eng::s16> {came, 8},
		eng::Span<eng::u8> {closed, 8}, eng::Span<eng::u16> {out, 8});
	const eng::usize b = eng::util::graph_bfs(g, 0u, 2u, eng::Span<eng::s16> {came, 8},
						  eng::Span<eng::u16> {q, 8},
						  eng::Span<eng::u16> {out, 8});
	return static_cast<u16>(a + b + g.node_count() + g.edge_count() +
				static_cast<eng::u16>(seed & 0u));
}
extern "C" s32 c_lru_cache_ops(u16 seed) {
	eng::util::LruCache<eng::u16, eng::s32, 8> cache;
	for (eng::u16 i = 0u; i < 12u; ++i) {
		cache.put(static_cast<eng::u16>(i % 8u), static_cast<eng::s32>(i));
	}
	const eng::s32* v = cache.get(static_cast<eng::u16>(seed % 8u));
	return (v != nullptr ? *v : -1) + static_cast<eng::s32>(cache.size());
}
static eng::util::TaskStatus c_task_ok() { return eng::util::TaskStatus::Success; }
extern "C" u16 c_task_ops(u16 seed) {
	eng::util::Delay wait {2};
	eng::util::TaskSequence<2> seq;
	seq.add(wait);
	seq.add(c_task_ok);
	eng::util::TaskStatus last = eng::util::TaskStatus::Running;
	for (eng::u16 i = 0u; i < (seed & 7u); ++i) {
		last = seq.tick();
		if (last != eng::util::TaskStatus::Running) {
			seq.reset();
		}
	}
	return static_cast<u16>(static_cast<eng::u16>(last) + seq.step_count());
}
extern "C" u16 c_interval_ops(s32 lo, s32 hi) {
	eng::util::IntervalSet<8> s;
	s.add(lo, hi);
	s.add(0, 10);
	s.add(20, 30);
	return static_cast<u16>(s.size() + (s.contains(5) ? 1u : 0u) +
				(s.contains(25) ? 1u : 0u));
}
struct CVariantA {
	eng::s16 a;
};
struct CVariantB {
	eng::u16 b;
};
extern "C" u16 c_variant_ops(u16 seed) {
	eng::util::Variant<CVariantA, CVariantB> v {CVariantB {seed}};
	eng::u16 out = 0u;
	v.visit([&](const auto& c) {
		using T = eng::util::remove_cvref_t<decltype(c)>;
		if constexpr (eng::util::is_same_v<T, CVariantA>) {
			out = static_cast<eng::u16>(c.a);
		} else {
			out = c.b;
		}
	});
	return static_cast<u16>(out + v.index());
}
extern "C" u16 c_random_ops(u16 seed) {
	eng::Xoroshiro64pp rng {seed, static_cast<eng::u32>(seed + 1u)};
	eng::u16 data[8];
	for (eng::u16 i = 0; i < 8u; ++i) {
		data[i] = static_cast<eng::u16>(eng::next_range(rng, 0u, 1000u));
	}
	eng::shuffle(rng, eng::Span<eng::u16> {data, 8});
	return static_cast<eng::u16>(eng::pick(rng, eng::Span<const eng::u16> {data, 8}) +
				     (eng::chance(rng, 1u, 3u) ? 1u : 0u));
}
extern "C" float c_dsp_ops(float x) {
	eu::Adsr<float> env {0.5f, 0.25f, 0.1f, 0.5f};
	env.note_on();
	float a = env.tick();
	eu::OnePole<float> lp {0.5f, 0.0f};
	a += lp.process(x);
	eu::DelayLine<float, 4> dl;
	dl.clear();
	a += dl.process(x, 1u);
	return a + eu::osc_saw(x) + eu::osc_square(x) + eu::osc_triangle(x);
}
extern "C" s16 c_scalar16_ops(const s16* data, int n) {
	eng::Span<const q12> xs {reinterpret_cast<const q12*>(data), static_cast<eng::usize>(n)};
	const q12 m = eu::mean(xs);            // acumulador s32 (add.l)
	const q12 s = scalar_sin<q12>::op(q12 {1024}); // tabla de seno fixed
	eu::Adsr<q12> env {q12 {1024}, q12 {1024}, q12 {1024}, q12 {2048}};
	env.note_on();
	(void)env.tick();
	return static_cast<s16>(m.v + s.v + env.level.v);
}
extern "C" s16 c_scalar16_math(s16 a, s16 b) {
	const q12 t {a};
	const q12 e1 = ease_in_sine(t);
	const q12 e2 = ease_out_sine(t);
	const q12 sd = smooth_damp(q12 {0}, q12 {4096}, q12 {2048}, q12 {4096});
	const Vec<2, q12> v {q12 {a}, q12 {b}};
	const q12 len = length(v);
	const q12 e2v = scalar_exp2<q12>::op(q12 {1024});
	const q12 l2 = scalar_log2<q12>::op(q12 {4096});
	const q12 ex = scalar_exp<q12>::op(q12 {1024});
	const q12 pw = scalar_pow<q12>::op(q12 {4096}, q12 {1024});
	return static_cast<s16>(e1.v + e2.v + sd.v + len.v + e2v.v + l2.v + ex.v + pw.v);
}
extern "C" s16 c_scalar16_trig(s16 a, s16 b) {
	const q12 y {a};
	const q12 x {b};
	const q12 tn = scalar_tan<q12>::op(y);
	const q12 at = scalar_atan2<q12>::op(y, x);
	const q12 as = scalar_asin<q12>::op(y);
	const q12 ac = scalar_acos<q12>::op(x);
	q12 ss {};
	q12 cc {};
	scalar_sincos<q12>::op(y, ss, cc); // seno y coseno en una sola pasada
	return static_cast<s16>(tn.v + at.v + as.v + ac.v + ss.v + cc.v);
}
extern "C" s16 c_fx_rotate2_angle(s16 angle, s16 x, s16 y) {
	const Vec<2, q12> v {q12 {x}, q12 {y}};
	const Vec<2, q12> r = rotate2(v, q12 {angle}); // scalar_sincos: un solo indice
	return static_cast<s16>(r.v[0].v + r.v[1].v);
}
extern "C" s16 c_fx_rotate2_twice(s16 angle, s16 x, s16 y) {
	const q12 a {angle};
	const q12 s = scalar_sin<q12>::op(a); // dos indices (sin y cos por separado)
	const q12 c = scalar_cos<q12>::op(a);
	const Vec<2, q12> v {q12 {x}, q12 {y}};
	const Vec<2, q12> r = rotate2(v, c, s);
	return static_cast<s16>(r.v[0].v + r.v[1].v);
}
extern "C" s16 c_fx_aim(s16 dx, s16 dy) {
	const Vec<2, q12> d {q12 {dx}, q12 {dy}};
	const q12 ang = angle_of(d);          // atan2 fixed (tabla de atan)
	const Vec<2, q12> dir = from_angle(ang); // sincos fixed (tabla de seno)
	return static_cast<s16>(ang.v + dir.v[0].v + dir.v[1].v);
}

// --- eng::board / eng::parallel: generacion legal y perft no deben arrastrar
// libcalls de libgcc (la busqueda por nodo sera el camino caliente en 68000). ---
extern "C" u16 c_chess_gen() {
	using R = eng::board::ChessRules;
	R::Position pos = R::initial();
	R::MoveList moves;
	return static_cast<u16>(R::generate_legal(pos, moves));
}
extern "C" eng::u32 c_chess_perft(u16 depth) {
	using R = eng::board::ChessRules;
	R::Position pos = R::initial();
	return static_cast<eng::u32>(eng::board::chess::perft(pos, static_cast<eng::u32>(depth)));
}
extern "C" eng::u32 c_chess_search(u16 depth) {
	using R = eng::board::ChessRules;
	R::Position pos = R::initial();
	eng::board::ChessSearcher searcher;
	const eng::board::ChessSearcher::Result result =
	    searcher.search(pos, {static_cast<eng::u32>(depth), 0u});
	return static_cast<eng::u32>(result.best_move) ^ static_cast<eng::u32>(result.nodes);
}
extern "C" u16 c_chess_explain(u16 language) {
	using R = eng::board::ChessRules;
	R::Position pos = R::initial();
	char out[128] {};
	const eng::board::chess::Language lang = (language == 0u)
	                                             ? eng::board::chess::Language::Spanish
	                                             : eng::board::chess::Language::English;
	const eng::usize n = eng::board::chess::explain(
	    pos, {lang, eng::board::chess::Tone::Neutral, 3u}, eng::Span<char> {out, sizeof(out)});
	return static_cast<u16>(n + static_cast<eng::usize>(static_cast<eng::u8>(out[0])));
}
extern "C" u16 c_parallel_threads() {
	return static_cast<u16>(eng::parallel::hardware_threads());
}
`;

fs.mkdirSync(`${ROOT}/out/tmp`, { recursive: true });
fs.writeFileSync(SRC, probe);
try {
  execFileSync(CXX, ['-std=gnu++23', '-mcpu=68000', '-O2', '-fomit-frame-pointer',
    `-I${ROOT}/engine/include`, '-S', '-o', ASM, SRC], { stdio: 'pipe' });
} catch (e) {
  console.error('fallo el compilado cruzado:\n' + e.stderr?.toString());
  process.exit(1);
}

const lines = fs.readFileSync(ASM, 'latin1').split(/\r?\n/);
const fns = [];
let cur = null;
for (const ln of lines) {
  const m = ln.match(/^([A-Za-z_][A-Za-z0-9_]*):$/);
  if (m && m[1].startsWith('c_')) { cur = { name: m[1], ins: 0, mul: 0, asr: 0, lib: 0, branch: 0 }; fns.push(cur); continue; }
  if (!cur) continue;
  if (ln.includes('.size')) { cur = null; continue; }
  const s = ln.trim();
  if (!s || s.startsWith('.')) { if (/^(dbra|bra|bne|beq|jne|jmp|jhi|jeq|blt|bgt|blo|bhi)/.test(s)) cur.branch++; continue; }
  cur.ins++;
  if (/\b(muls|mulu)(\.[wl])?\b/.test(s)) cur.mul++;
  if (/asr\.l|lsr\.l/.test(s)) cur.asr++;
  if (/jsr|bsr/.test(s)) cur.lib++;
  if (/^(dbra|bra|bne|beq|jne|jmp|jhi|jeq|blt|bgt|blo|bhi)/.test(s)) cur.branch++;
}

console.log('construccion                     instr  muls.w  shifts  saltos  libcalls  bucle');
for (const f of fns) {
  const loop = f.branch > 0 ? 'plegado' : 'lineal';
  console.log(`${f.name.padEnd(32)} ${String(f.ins).padStart(5)} ${String(f.mul).padStart(7)} ${String(f.asr).padStart(7)} ${String(f.branch).padStart(7)} ${String(f.lib).padStart(9)}  ${loop}`);
}

// --- Gate: el camino caliente no puede llamar a las rutinas de 32 bits de libgcc ---
// (__mulsi3/__divsi3/…). Si aparece, una operacion que deberia ser `muls.w`/`mulu.w`
// nativos se ha convertido en una llamada (~50+ ciclos). Los `jsr` a funciones propias
// (cuerpos no inlined) son normales y solo se informan. `--report` no falla.
const asmText = fs.readFileSync(ASM, 'latin1');
const FORBIDDEN = ['__mulsi3', '__umulsi3', '__divsi3', '__udivsi3',
  '__ashldi3', '__lshrdi3', '__ashrdi3', '__muldi3', '__divdi3', '__udivdi3'];
const hit = FORBIDDEN.filter((s) => asmText.includes(s));
const called = fns.filter((f) => f.lib > 0).map((f) => `${f.name} (${f.lib} jsr)`);
if (hit.length) {
  console.error(`\n[codegen] FAIL: el camino caliente llama a libgcc -> ${hit.join(', ')}`);
  process.exit(1);
}
if (called.length && !process.argv.includes('--report')) {
  console.log(`\n[codegen] nota: jsr a funciones propias (no libgcc): ${called.join(', ')}`);
}

// --- Gate 68000: nada de instrucciones de 68020+ (p. ej. divsl.l/divul.l) ---
// Compilamos con -mcpu=68000, pero si alguien cambia el target la division 32/32 se
// convierte en `divsl.l` (68020) y crashea en un A500. Se detecta aqui.
const BAD_68020 = [
  ['divsl.l', /\bdivsl\.l\b/],
  ['divul.l', /\bdivul\.l\b/],
  ['divs.l', /\bdivs\.l\b/],
  ['divu.l', /\bdivu\.l\b/],
  ['muls.l', /\bmuls\.l\b/],
  ['mulu.l', /\bmulu\.l\b/],
];
const bad = BAD_68020.filter(([, re]) => re.test(asmText)).map(([name]) => name);
if (bad.length) {
  console.error(`\n[codegen] FAIL: instrucciones de 68020 en el target 68000 -> ${bad.join(', ')}`);
  process.exit(1);
}
// Evidencia positiva: la division de fixed (div_norm) debe ser `divs.w` nativa.
if (!/\bdivs(\.w)?\b/.test(asmText)) {
  console.error('\n[codegen] FAIL: no aparece divs.w (la division de fixed deberia ser nativa).');
  process.exit(1);
}

// El bucle de minifloat_math debe quedar INLINEADO (sin `jsr` a funciones propias): un
// `jsr` por operacion dentro de un bucle caliente cuesta mas que el propio calculo.
const loop = fns.find((f) => f.name === 'c_mf_loop');
if (loop && loop.lib > 0) {
  console.error(`\n[codegen] FAIL: minifloat_math no se inlinea en el bucle (${loop.lib} jsr).`);
  process.exit(1);
}

// Los helpers nuevos (scalar_ops/back/bezier/repeat) son bucles internos de gameplay:
// deben quedar INLINEADOS igual que minifloat_math. Un `jsr` por operacion costaria mas
// que el propio calculo. `c_mf_smooth_damp` queda fuera a proposito: llama a `exp2` de
// MiniFloat16, que como `sin`/`exp` es una funcion de tabla grande y g++ no la inlinea
// (es coste esperado del escalar, no del helper).
const HOT = ['c_fx_move_towards', 'c_fx_deadzone', 'c_fx_repeat', 'c_fx_pingpong', 'c_fx_ease_back',
  'c_fx_bezier3', 'c_mf_move_towards', 'c_mf_bezier3', 'c_proj_persp', 'c_clip_line', 'c_face_area'];
const notInlined = HOT.map((n) => fns.find((f) => f.name === n)).filter((f) => f && f.lib > 0)
  .map((f) => `${f.name} (${f.lib} jsr)`);
if (notInlined.length) {
  console.error(`\n[codegen] FAIL: helpers de gameplay no inlineados -> ${notInlined.join(', ')}`);
  process.exit(1);
}

console.log('[codegen] OK: sin libcalls (mul/div de 32 y 64 bits) ni instrucciones 68020.');

// --- Variante 68020 (nativo) ---------------------------------------------------
// La sonda debe compilar tambien para 68020 sin pedir libgcc de mul/div. En ese target
// `__mc68000__` no esta definido, asi que `arith<s16>` usa el camino portable nativo en
// vez del backend `muls.w`/`divs.w`; el gate 68000 de arriba (que exige `divs.w`) no
// aplica aqui. Se comprueba solo que no aparezcan libcalls prohibidas.
const ASM20 = `${ROOT}/out/tmp/codegen-probe-68020.s`;
try {
  execFileSync(CXX, ['-std=gnu++23', '-mcpu=68020', '-O2', '-fomit-frame-pointer',
    `-I${ROOT}/engine/include`, '-S', '-o', ASM20, SRC], { stdio: 'pipe' });
} catch (e) {
  console.error('fallo el compilado 68020:\n' + e.stderr?.toString());
  process.exit(1);
}
const asm20 = fs.readFileSync(ASM20, 'latin1');
const hit20 = FORBIDDEN.filter((s) => asm20.includes(s));
if (hit20.length) {
  console.error(`\n[codegen 68020] FAIL: libcalls -> ${hit20.join(', ')}`);
  process.exit(1);
}
console.log('[codegen 68020] OK: compila sin libcalls de mul/div (68020 nativo).');
