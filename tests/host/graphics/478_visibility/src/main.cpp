// ============================================================================
// Test HOST-478: culling de entrada 3D (eng::core/math/frustum.hpp)
// ============================================================================
//
// Valida la selección de objeto ANTES de transformar (la métrica de A500 es
// "objetos que nunca llaman a transform_vertices"):
//
//   1) Vista yaw-only: rechazos por cada frontera (delante/atrás del near, más
//      allá del far, fuera de la franja vertical, fuera de la cuña por cada
//      lado) y aceptación de bordes (cruzar el near = VISIBLE, conservador).
//   2) Equivalencia con el frustum general de 6 planos construido con los mismos
//      parámetros (la cuña va pre-escalada por 1/(1+W) para que ambos usen el
//      mismo test conservador): barrido de centro/radio.
//   3) Descarte por tamaño en pantalla (subpíxel) sin división.
//   4) Dos escalares (float y coord q0): la cabecera no impone el escalar.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/478_visibility

#define ENG_SCALAR_RETRO16
#include <eng/core/math/frustum.hpp>
#include <eng/core/data/mesh3d.hpp>

#include <cstdio>

namespace {

using eng::math3d::YawView;
using eng::math3d::Frustum;
using eng::math3d::frustum_from_yaw_view;
using eng::math3d::screen_size_visible;
using eng::math3d::sphere_visible;
using eng::math3d::Plane;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Cámara yaw=0 mirando +Z (convenio: x derecha, y hacia abajo), FOV horizontal ratio 0.5,
/// franja y en [−80, 80], near 100, far 1000.
template <class S>
YawView<S> make_view() {
	using T = eng::math::scalar_traits<S>;
	YawView<S> v {};
	v.sin_yaw = T::zero();
	v.cos_yaw = T::one();
	v.half_w = T::from_int(1); // tan(45): representable en q0
	v.y_min = T::from_int(-80);
	v.y_max = T::from_int(80);
	v.z_near = T::from_int(100);
	v.z_far = T::from_int(1000);
	return v;
}

template <class S>
eng::math::Vec<3, S> sp(int x, int y, int z) {
	return eng::math3d::vec3<S>(x, y, z);
}

template <class S>
void test_yaw_view(const char* scalar_name) {
	using T = eng::math::scalar_traits<S>;
	const YawView<S> v = make_view<S>();
	const S r = T::from_int(10);

	// Dentro: centro delante, holgura.
	check(sphere_visible(v, sp<S>(0, 0, 500), r), "centro: visible");
	// Fuera por detrás del near: z < near − r.
	check(!sphere_visible(v, sp<S>(0, 0, 80), r), "detras del near: descartado");
	// Cruzando el near (z − r < near < z + r): conservador -> VISIBLE.
	check(sphere_visible(v, sp<S>(0, 0, 105), r), "cruzando el near: visible (conservador)");
	// Más allá del far: z > far + r.
	check(!sphere_visible(v, sp<S>(0, 0, 1020), r), "mas alla del far: descartado");
	// Franja vertical: y + r < -80.
	check(!sphere_visible(v, sp<S>(0, -95, 500), r), "por encima de la franja: descartado");
	check(!sphere_visible(v, sp<S>(0, 95, 500), r), "por debajo de la franja: descartado");
	// Cuña a z=500 con W=1 (semi-ancho 500): x=505 dentro con holgura (slack r·2=20),
	// x=530 fuera.
	check(sphere_visible(v, sp<S>(505, 0, 500), r), "borde de la cuna con holgura: visible");
	check(!sphere_visible(v, sp<S>(530, 0, 500), r), "fuera de la cuna (derecha): descartado");
	check(!sphere_visible(v, sp<S>(-530, 0, 500), r), "fuera de la cuna (izquierda): descartado");
	(void)scalar_name;
}

template <class S>
void test_equivalence(const char* scalar_name) {
	const YawView<S> v = make_view<S>();
	const Frustum<S> f = frustum_from_yaw_view(v);
	const eng::s32 rads[3] = {0, 10, 40};
	bool agree = true;
	for (eng::s32 x = -400; x <= 400; x += 37) {
		for (eng::s32 y = -120; y <= 120; y += 29) {
			for (eng::s32 z = 40; z <= 1100; z += 53) {
				for (eng::u8 i = 0; i < 3u; ++i) {
					const S r = eng::math::scalar_traits<S>::from_int(rads[i]);
					const auto c = sp<S>(x, y, z);
					if (sphere_visible(v, c, r) != sphere_visible(f, c, r)) {
						agree = false;
					}
				}
			}
		}
	}
	char msg[96];
	std::snprintf(msg, sizeof(msg), "%s: vista yaw == frustum general (barrido)", scalar_name);
	check(agree, msg);
}

void test_screen_size() {
	using eng::coord;
	// focal 256, min 1.5 px -> radio proyectado < 1.5 px a esa z: descartar.
	const coord focal = eng::math::scalar_traits<coord>::from_int(256);
	const coord minpx = eng::math::scalar_traits<coord>::from_int(2);
	// z=1024, radio 4: 4*256=1024 < 2*1024=2048 -> subpixel.
	const coord r4 = eng::math::scalar_traits<coord>::from_int(4);
	const coord z1024 = eng::math::scalar_traits<coord>::from_int(1024);
	check(!screen_size_visible(r4, focal, z1024, minpx), "radio 4 a z=1024: subpixel");
	// radio 16: 4096 >= 2048 -> visible.
	const coord r16 = eng::math::scalar_traits<coord>::from_int(16);
	check(screen_size_visible(r16, focal, z1024, minpx), "radio 16 a z=1024: visible");
}

void test_float_scalar() {
	YawView<float> v {};
	v.sin_yaw = 0.0f;
	v.cos_yaw = 1.0f;
	v.half_w = 0.5f;
	v.y_min = -80.0f;
	v.y_max = 80.0f;
	v.z_near = 100.0f;
	v.z_far = 1000.0f;
	check(sphere_visible(v, eng::math3d::vec3<float>(0, 0, 500), 10.0f), "float: centro visible");
	check(!sphere_visible(v, eng::math3d::vec3<float>(400, 0, 500), 10.0f),
	      "float: fuera de cuna descartado");
	const Frustum<float> f = frustum_from_yaw_view(v);
	check(sphere_visible(f, eng::math3d::vec3<float>(0, 0, 500), 10.0f), "float: frustum general");
	// Yaw a 90 grados (mirando +X): un objeto en +Z ya no se ve.
	v.sin_yaw = 1.0f;
	v.cos_yaw = 0.0f;
	check(!sphere_visible(v, eng::math3d::vec3<float>(0, 0, 500), 10.0f),
	      "float yaw 90: fuera de la cuna");
	check(sphere_visible(v, eng::math3d::vec3<float>(500, 0, 0), 10.0f),
	      "float yaw 90: el de +X visible");
}

} // namespace

int main() {
	test_yaw_view<eng::coord>("coord");
	// La equivalencia con el frustum general necesita el pre-escalado 1/(1+W) (fracciones):
	// se verifica con float (el frustum general es para escalares fraccionarios; la vía
	// yaw-only funciona con cualquier escalar).
	test_equivalence<float>("float");
	test_screen_size();
	test_float_scalar();
	if (g_fail == 0) {
		std::printf("OK: culling de entrada (yaw view, frustum general, tamano y dos escalares)\n");
	} else {
		std::printf("FALLOS: %u\n", g_fail);
	}
	return g_fail == 0 ? 0 : 1;
}
