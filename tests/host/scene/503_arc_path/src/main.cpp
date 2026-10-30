// ============================================================================
// Test HOST-503: ArcPath (longitud de arco y muestreo subpíxel).
// ============================================================================
//
// Valida la tabla que usa la cámara de 209 para avanzar a velocidad constante:
//   - distancia acumulada en Q8 sobre una polilínea;
//   - muestreo en los vértices y en puntos intermedios;
//   - interpolación subpíxel entre dos muestras;
//   - recorte fuera de rango.
//
//   bash tools/run-host-tests.sh tests/host/scene/503_arc_path

#include <cstdio>

#include <eng/scene/arc_path.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void eq(eng::s32 got, eng::s32 want, const char* what) {
	if (got != want) {
		std::printf("[FAIL] %s: got %d, want %d\n", what, got, want);
		++g_fail;
	}
}

} // namespace

int main() {
	eng::scene::ArcPath<8> path {};
	check(!path.valid(), "vacía: no válida");
	(void)path.push({0, 0});
	(void)path.push({600 << 8, 0});
	(void)path.push({600 << 8, 600 << 8});
	check(path.valid(), "tres puntos: válida");
	eq(path.count(), 3u, "count");
	eq(static_cast<eng::s32>(path.total_q8()), 1200 << 8, "longitud total Q8");

	eng::math::Vec<2, eng::s32> p {};
	check(path.sample(0u, p), "sample(0)");
	eq(p.v[0], 0, "inicio x");
	eq(p.v[1], 0, "inicio y");

	check(path.sample(300u << 8, p), "sample(300 px)");
	eq(p.v[0], 300 << 8, "300 px x");
	eq(p.v[1], 0, "300 px y");

	check(path.sample(900u << 8, p), "sample(900 px)");
	eq(p.v[0], 600 << 8, "900 px x");
	eq(p.v[1], 300 << 8, "900 px y");

	check(path.sample(5000u << 8, p), "sample fuera de rango");
	eq(p.v[0], 600 << 8, "fuera: final x");
	eq(p.v[1], 600 << 8, "fuera: final y");

	// 0 -> 2 px: a 0.5 px debe caer en el punto medio (Q8 = 128).
	eng::scene::ArcPath<4> half {};
	(void)half.push({0, 0});
	(void)half.push({2 << 8, 0});
	check(half.sample(128u, p), "subpíxel 0.5 px");
	eq(p.v[0], 128, "0.5 px -> 128 Q8");
	eq(p.v[1], 0, "0.5 px y");

	if (g_fail != 0) {
		std::printf("HOST-503: %d fallos\n", g_fail);
		return 1;
	}
	std::printf("HOST-503: ArcPath OK\n");
	return 0;
}
