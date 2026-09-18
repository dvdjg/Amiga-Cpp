// ============================================================================
// Test HOST-093: estadística básica (eng::util::stats).
// ============================================================================
//
// Respalda `eng/core/util/stats.hpp` con `double`, `MiniFloat16` y `q12` (fixed):
// media, varianza/desviación, estadísticos de orden, histograma y medias móviles.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/093_stats

#include <cmath>
#include <cstdio>

#include <eng/core/minifloat_math.hpp>
#include <eng/core/util/stats.hpp>
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

template <typename S, eng::usize N>
constexpr eng::Span<const S> cspan(const S (&a)[N]) {
	return eng::Span<const S> {a, N};
}

template <typename S, eng::usize N>
constexpr eng::Span<S> mspan(S (&a)[N]) {
	return eng::Span<S> {a, N};
}

template <typename S>
S mk(float x) {
	return S(x);
}
template <>
er::q12 mk<er::q12>(float x) {
	return er::q12 {static_cast<eng::s16>(std::lround(x * 4096.0f))};
}

template <typename S>
void check_stats(const char* tag, float tol) {
	// Datos que caben también en q12 (suma 6, media 1.5, varianza 0.25).
	const S data[4] = {mk<S>(1.0f), mk<S>(2.0f), mk<S>(1.0f), mk<S>(2.0f)};
	check(std::fabs(em::to_double(eu::sum(cspan(data))) - 6.0) <= tol, "sum = 6");
	check(std::fabs(em::to_double(eu::mean(cspan(data))) - 1.5) <= tol, "mean = 1.5");
	check(std::fabs(em::to_double(eu::variance(cspan(data))) - 0.25) <= tol,
	      "variance poblacional = 0.25");
	std::printf("  %s mean/variance OK (var=%.4f)\n", tag,
		    em::to_double(eu::variance(cspan(data))));
}

template <typename S>
void check_order(const char* tag, float tol) {
	S data[7] = {mk<S>(7.0f), mk<S>(3.0f), mk<S>(5.0f), mk<S>(1.0f),
		     mk<S>(6.0f), mk<S>(2.0f), mk<S>(4.0f)};
	S scratch[7] = {};
	check(std::fabs(em::to_double(eu::kth_smallest(eng::Span<const S> {data, 7}, mspan(scratch), 0u)) -
			1.0) <= tol,
	      "kth_smallest(0) = 1");
	check(std::fabs(em::to_double(eu::kth_smallest(eng::Span<const S> {data, 7}, mspan(scratch), 3u)) -
			4.0) <= tol,
	      "kth_smallest(3) = 4");
	check(std::fabs(em::to_double(eu::kth_smallest(eng::Span<const S> {data, 7}, mspan(scratch), 6u)) -
			7.0) <= tol,
	      "kth_smallest(6) = 7");

	S three[3] = {mk<S>(5.0f), mk<S>(1.0f), mk<S>(3.0f)};
	S scratch3[3] = {};
	check(std::fabs(em::to_double(eu::median(eng::Span<const S> {three, 3}, mspan(scratch3))) - 3.0) <=
		      tol,
	      "median = 3");
	std::printf("  %s orden/mediana OK\n", tag);
}

template <typename S>
void check_histogram(const char* tag) {
	const S xs[8] = {mk<S>(0.0f), mk<S>(1.0f), mk<S>(2.0f), mk<S>(3.0f),
			 mk<S>(4.0f), mk<S>(5.0f), mk<S>(6.0f), mk<S>(7.0f)};
	eng::u32 counts[3] = {0u, 0u, 0u};
	const eng::u32 dropped = eu::histogram(cspan(xs), eng::Span<eng::u32> {counts, 3},
					       mk<S>(0.0f), mk<S>(2.0f));
	check(counts[0] == 2u && counts[1] == 2u && counts[2] == 2u && dropped == 2u,
	      "histograma por cubos de 2 con 2 fuera");
	std::printf("  %s histograma OK (dropped=%u)\n", tag, static_cast<unsigned>(dropped));
}

} // namespace

int main() {
	std::printf("== HOST-093 stats ==\n");

	check_stats<double>("double", 1.0e-9f);
	check_stats<MiniFloat16>("MF    ", 5.0e-2f);
	check_stats<er::q12>("q12   ", 3.0e-3f);

	check_order<double>("double", 1.0e-9f);
	check_order<MiniFloat16>("MF    ", 3.0e-3f);
	check_order<er::q12>("q12   ", 3.0e-3f);

	check_histogram<double>("double");
	check_histogram<MiniFloat16>("MF    ");
	check_histogram<er::q12>("q12   ");

	// stddev solo para escalares con sqrt (no Fixed).
	{
		const double v[8] = {2, 4, 4, 4, 5, 5, 7, 9};
		check(std::fabs(em::to_double(eu::stddev(cspan(v))) - 2.0) <= 1.0e-9, "stddev = 2");
		const MiniFloat16 mf[2] = {MiniFloat16(1.0f), MiniFloat16(3.0f)};
		check(std::fabs(em::to_double(eu::stddev(cspan(mf))) - 1.0) <= 5.0e-3, "stddev MF");
	}

	// ema: prev + alpha*(x-prev).
	{
		const double e = eu::ema(0.0, 1.0, 0.5);
		check(std::fabs(e - 0.5) <= 1.0e-12, "ema(0,1,0.5) = 0.5");
		const double e2 = eu::ema(0.5, 1.0, 1.0);
		check(std::fabs(e2 - 1.0) <= 1.0e-12, "ema con alpha=1 toma x");
	}

	// RunningMean de ventana 3.
	{
		eu::RunningMean<double, 3> rm;
		check(std::fabs(rm.push(1.0) - 1.0) <= 1.0e-12, "RunningMean 1");
		check(std::fabs(rm.push(2.0) - 1.5) <= 1.0e-12, "RunningMean 1.5");
		check(std::fabs(rm.push(3.0) - 2.0) <= 1.0e-12, "RunningMean 2");
		check(std::fabs(rm.push(4.0) - 3.0) <= 1.0e-12, "RunningMean desliza {2,3,4}=3");
		check(rm.size() == 3u, "ventana llena");
	}

	// --- acumulador ancho en Fixed: la media no satura ----------------------
	{
		const er::q12 big[4] = {mk<er::q12>(7.0f), mk<er::q12>(7.0f), mk<er::q12>(7.0f),
					mk<er::q12>(7.0f)};
		check(std::fabs(em::to_double(eu::mean(cspan(big))) - 7.0) <= 3.0e-3,
		      "mean<q12> acumula en s32 (no satura)");
		check(em::to_double(eu::sum(cspan(big))) <= 8.0, "sum<q12> cabe/satura en rango");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: stats validado.\n");
	return 0;
}
