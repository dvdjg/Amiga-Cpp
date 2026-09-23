// ============================================================================
// Test HOST-103: la librería de utilidades con MiniFloat16 y q12.
// ============================================================================
//
// Los contenedores/algoritmos de `eng::util` son agnósticos del tipo (almacenan `T`):
// este test los ejercita con los escalares de 16 bits del engine (`MiniFloat16` y el
// fixed `q12`) para fijar que funcionan sin especializaciones.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/103_scalar_util

#include <cmath>
#include <cstdio>

#include <eng/core/minifloat_math.hpp>
#include <eng/core/util/algorithm.hpp>
#include <eng/core/util/flat_map.hpp>
#include <eng/core/util/optional.hpp>
#include <eng/core/util/pool.hpp>
#include <eng/core/util/priority_queue.hpp>
#include <eng/core/util/ring_buffer.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/core/util/util.hpp>
#include <eng/retro/fixed_q.hpp>

using eng::math::MiniFloat16;
namespace em = eng::math;
namespace eu = eng::util;
namespace er = eng::retro;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

MiniFloat16 mf(float v) {
	return MiniFloat16(v);
}
er::q12 q(float v) {
	return er::q12 {static_cast<eng::s16>(v * 4096.0f + (v < 0 ? -0.5f : 0.5f))};
}

} // namespace

int main() {
	std::printf("== HOST-103 scalar util ==\n");

	// --- StaticVector<MiniFloat16> ------------------------------------------
	{
		eu::StaticVector<MiniFloat16, 4> v;
		check(v.push_back(mf(1.0f)) && v.push_back(mf(2.0f)), "StaticVector<MF> push");
		check(v.size() == 2u && std::fabs(em::to_double(v.back()) - 2.0) <= 1.0e-3,
		      "StaticVector<MF> back");
	}

	// --- RingBuffer<q12> -----------------------------------------------------
	{
		eu::RingBuffer<er::q12, 3> r;
		check(r.push(q(1.0f)) && r.push(q(2.0f)), "RingBuffer<q12> push");
		check(std::fabs(em::to_double(r.front()) - 1.0) <= 2.0e-3, "RingBuffer<q12> front");
		check(std::fabs(em::to_double(r.pop()) - 1.0) <= 2.0e-3, "RingBuffer<q12> pop FIFO");
	}

	// --- FlatMap<u16, MiniFloat16> ------------------------------------------
	{
		eu::FlatMap<eng::u16, MiniFloat16, 4> m;
		check(m.insert(3u, mf(0.5f)) != nullptr, "FlatMap<u16,MF> insert");
		const MiniFloat16* p = m.find(3u);
		check(p != nullptr && std::fabs(em::to_double(*p) - 0.5) <= 1.0e-3, "FlatMap<u16,MF> find");
	}

	// --- PriorityQueue<MiniFloat16> (max-heap) ------------------------------
	{
		eu::PriorityQueue<MiniFloat16, 4> pq;
		check(pq.push(mf(1.0f)) && pq.push(mf(3.0f)) && pq.push(mf(2.0f)), "PriorityQueue<MF> push");
		check(std::fabs(em::to_double(pq.top()) - 3.0) <= 1.0e-3, "PriorityQueue<MF> top = mayor");
	}

	// --- Pool<q12> con handles ----------------------------------------------
	{
		eu::Pool<er::q12, 4> pool;
		const auto h = pool.add();
		check(h.valid() && pool.get(h) != nullptr, "Pool<q12> add");
		*pool.get(h) = q(0.25f);
		check(std::fabs(em::to_double(*pool.get(h)) - 0.25) <= 2.0e-3, "Pool<q12> escribe");
		check(pool.remove(h) && !pool.valid(h), "Pool<q12> remove");
	}

	// --- Optional<MiniFloat16> ----------------------------------------------
	{
		eu::Optional<MiniFloat16> o;
		check(!o.has_value(), "Optional<MF> vacío");
		o = mf(0.75f);
		check(o.has_value() && std::fabs(em::to_double(*o) - 0.75) <= 1.0e-3,
		      "Optional<MF> con valor");
	}

	// --- algorithm sobre q12 -------------------------------------------------
	{
		er::q12 data[3] = {q(3.0f), q(1.0f), q(2.0f)};
		er::q12* mn = eu::min_element(eng::Span<er::q12> {data, 3});
		check(std::fabs(em::to_double(*mn) - 1.0) <= 2.0e-3, "min_element<q12>");
		check(eu::find(eng::Span<er::q12> {data, 3}, q(2.0f)) == &data[2], "find<q12>");
		const er::q12 clamped = eu::clamp(q(5.0f), q(0.0f), q(4.0f));
		check(std::fabs(em::to_double(clamped) - 4.0) <= 2.0e-3, "clamp<q12> por arriba");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: util con MiniFloat16/q12 validado.\n");
	return 0;
}
