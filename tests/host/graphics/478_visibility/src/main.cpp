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
//   5) Cajas (AABB) contra la vista yaw-only y el frustum general: casos por
//      frontera + ORÁCULO de conservadurismo (ninguna caja rechazada puede tener
//      un punto interior visible) + equivalencia entre ambas vías + vía `coord`
//      con margen de cuantización.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/478_visibility

#define ENG_SCALAR_RETRO16
#include <eng/core/math/frustum.hpp>
#include <eng/core/data/mesh3d.hpp>

#include <cstdio>
#include <cmath>

namespace {

using eng::math3d::YawView;
using eng::math3d::Frustum;
using eng::math3d::frustum_from_yaw_view;
using eng::math3d::screen_size_visible;
using eng::math3d::sphere_visible;
using eng::math3d::aabb_visible;
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
	const auto sweep = [&](const YawView<S>& vv, const Frustum<S>& ff, const eng::s32* rs,
			       eng::u32 nrs) {
		for (eng::s32 x = -400; x <= 400; x += 37) {
			for (eng::s32 y = -120; y <= 120; y += 29) {
				for (eng::s32 z = 40; z <= 1100; z += 53) {
					for (eng::u32 i = 0; i < nrs; ++i) {
						const S r = eng::math::scalar_traits<S>::from_int(rs[i]);
						const auto c = sp<S>(x, y, z);
						if (sphere_visible(vv, c, r) != sphere_visible(ff, c, r)) {
							agree = false;
						}
					}
				}
			}
		}
	};
	sweep(v, f, rads, 3u);
	// Con yaw GIRADO (3-4-5): los planos cerca/lejos van en profundidad de CÁMARA — esta
	// pasada es la regresión del bug corregido (antes usaban z de mundo y solo casaban
	// con yaw=0). Con radios >= 10 para no rozar fronteras por redondeo float.
	YawView<S> vr = v;
	using T = eng::math::scalar_traits<S>;
	vr.sin_yaw = T::from_int(3) / T::from_int(5);
	vr.cos_yaw = T::from_int(4) / T::from_int(5);
	const eng::s32 rads_rot[2] = {10, 40};
	sweep(vr, frustum_from_yaw_view(vr), rads_rot, 2u);
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

template <class S>
eng::math::Vec<3, S> hv(int x, int y, int z) {
	return eng::math3d::vec3<S>(x, y, z);
}

/// Casos por frontera con la vista yaw=0 (W=1, franja ±80, near 100, far 1000).
template <class S>
void test_aabb_yaw(const char* scalar_name, S margin) {
	const YawView<S> v = make_view<S>();
	check(aabb_visible(v, hv<S>(0, 0, 500), hv<S>(10, 10, 10), margin), "caja: dentro visible");
	check(!aabb_visible(v, hv<S>(530, 0, 500), hv<S>(10, 10, 10), margin),
	      "caja: fuera de la cuna (derecha)");
	check(!aabb_visible(v, hv<S>(-530, 0, 500), hv<S>(10, 10, 10), margin),
	      "caja: fuera de la cuna (izquierda)");
	// Cruza la frontera derecha: el vértice (505,510) del AABB está dentro de la cuña.
	check(aabb_visible(v, hv<S>(515, 0, 500), hv<S>(10, 10, 10), margin),
	      "caja: cruza la cuna (conservador)");
	check(!aabb_visible(v, hv<S>(0, 0, 50), hv<S>(5, 5, 5), margin), "caja: detras del near");
	check(aabb_visible(v, hv<S>(0, 0, 105), hv<S>(10, 10, 10), margin),
	      "caja: cruzando el near (conservador)");
	check(!aabb_visible(v, hv<S>(0, 0, 1020), hv<S>(5, 5, 5), margin), "caja: mas alla del far");
	check(!aabb_visible(v, hv<S>(0, -95, 500), hv<S>(10, 10, 10), margin),
	      "caja: encima de la franja");
	check(!aabb_visible(v, hv<S>(0, 95, 500), hv<S>(10, 10, 10), margin),
	      "caja: debajo de la franja");
	char msg[64];
	std::snprintf(msg, sizeof(msg), "%s: casos AABB", scalar_name);
	check(true, msg);
}

/// **Oráculo de conservadurismo**: ninguna caja RECHAZADA puede contener un punto interior
/// visible (esfera de radio 0 = punto). Muestreo de 27 puntos con inset; los falsos
/// positivos (cajas visibles aceptadas) no se penalizan: los paga el pipeline.
void test_aabb_oracle() {
	const YawView<float> v = make_view<float>();
	eng::u32 seed = 0x12345u;
	auto rnd = [&]() -> eng::s32 {
		seed = seed * 1103515245u + 12345u;
		return static_cast<eng::s32>((seed >> 9) & 0xffffu) - 32768;
	};
	eng::s32 bad = 0;
	for (eng::u32 it = 0; it < 20000u; ++it) {
		const float cx = static_cast<float>(rnd() % 2400 - 1200);
		const float cy = static_cast<float>(rnd() % 600 - 300);
		const float cz = static_cast<float>(rnd() % 1400 - 200);
		// Medias extensiones SIEMPRE >= 0 (un AABB con extension negativa no es una caja).
		const float hx = 1.0f + static_cast<float>(static_cast<eng::u32>(rnd()) % 200u);
		const float hy = 1.0f + static_cast<float>(static_cast<eng::u32>(rnd()) % 200u);
		const float hz = 1.0f + static_cast<float>(static_cast<eng::u32>(rnd()) % 200u);
		const auto c = eng::math3d::vec3<float>(cx, cy, cz);
		const auto h = eng::math3d::vec3<float>(hx, hy, hz);
		if (aabb_visible(v, c, h, 0.0f)) {
			continue;
		}
		bool any_visible = false;
		for (eng::s32 ix = -1; ix <= 1; ++ix) {
			for (eng::s32 iy = -1; iy <= 1; ++iy) {
				for (eng::s32 iz = -1; iz <= 1; ++iz) {
					const auto p = eng::math3d::vec3<float>(
						cx + static_cast<float>(ix) * (hx * 0.7f),
						cy + static_cast<float>(iy) * (hy * 0.7f),
						cz + static_cast<float>(iz) * (hz * 0.7f));
					if (sphere_visible(v, p, 0.0f)) {
						any_visible = true;
					}
				}
			}
		}
		if (any_visible) {
			if (bad < 3) {
				std::printf("    oraculo: rechazada con punto visible c=(%.1f,%.1f,%.1f) h=(%.1f,%.1f,%.1f)\n",
					    cx, cy, cz, hx, hy, hz);
			}
			++bad;
		}
	}
	check(bad == 0, "oraculo AABB: cero cajas rechazadas con punto interior visible");
}

/// Equivalencia caja: vista yaw-only == frustum general (mismos parámetros), con yaw
/// arbitrario y barrido de centros/tamaños.
void test_aabb_equivalence() {
	eng::u32 seed = 0x37ABu;
	auto rnd = [&]() -> eng::s32 {
		seed = seed * 1103515245u + 12345u;
		return static_cast<eng::s32>((seed >> 9) & 0xffffu) - 32768;
	};
	eng::s32 disagree = 0;
	for (eng::u32 angle = 0u; angle < 7u; ++angle) {
		const float th = 0.55f * static_cast<float>(angle) - 1.4f;
		YawView<float> v = make_view<float>();
		v.sin_yaw = static_cast<float>(std::sin(th));
		v.cos_yaw = static_cast<float>(std::cos(th));
		const Frustum<float> f = frustum_from_yaw_view(v);
		for (eng::u32 it = 0u; it < 4000u; ++it) {
			const auto c = eng::math3d::vec3<float>(
				static_cast<float>(rnd() % 1600 - 800),
				static_cast<float>(rnd() % 300 - 150),
				static_cast<float>(rnd() % 1300 - 150));
			const auto h = eng::math3d::vec3<float>(
				static_cast<float>(1u + static_cast<eng::u32>(rnd()) % 150u),
				static_cast<float>(1u + static_cast<eng::u32>(rnd()) % 150u),
				static_cast<float>(1u + static_cast<eng::u32>(rnd()) % 150u));
			// Posiciones enteras + ángulos irracionales: cerca del borde de decisión los dos
			// caminos (coeficientes sin escalar vs pre-escalados por 1/(1+W)) pueden diferir
			// por redondeo float. El contrato que se exige: si difieren, con la caja inflada
			// +0,5 (muy por encima del redondeo) deben COINCIDIR; un bug real persistiría.
			if (aabb_visible(v, c, h, 0.0f) != aabb_visible(f, c, h, 0.0f)) {
				const auto hi = eng::math3d::vec3<float>(
					h.v[0] + 0.5f, h.v[1] + 0.5f, h.v[2] + 0.5f);
				if (aabb_visible(v, c, hi, 0.0f) != aabb_visible(f, c, hi, 0.0f)) {
					++disagree;
				}
			}
		}
	}
	check(disagree == 0, "equivalencia AABB: vista yaw == frustum general");
}

/// Vía `coord` (Fixed<s16,0>) con margen: no puede rechazar nada que float vea visible.
void test_coord_margin_conservative() {
	const YawView<eng::coord> vc = make_view<eng::coord>();
	const YawView<float> vf = make_view<float>();
	using TC = eng::math::scalar_traits<eng::coord>;
	eng::s32 bad = 0;
	for (eng::s32 x = -600; x <= 600; x += 75) {
		for (eng::s32 y = -140; y <= 140; y += 57) {
			for (eng::s32 z = 40; z <= 1100; z += 91) {
				for (eng::s32 h = 5; h <= 60; h += 55) {
					if (aabb_visible(vc, hv<eng::coord>(x, y, z), hv<eng::coord>(h, h, h),
							 TC::from_int(2))) {
						continue;
					}
					const auto cf = eng::math3d::vec3<float>(
						static_cast<float>(x), static_cast<float>(y),
						static_cast<float>(z));
					const auto hf = eng::math3d::vec3<float>(
						static_cast<float>(h), static_cast<float>(h),
						static_cast<float>(h));
					if (aabb_visible(vf, cf, hf, 0.0f)) {
						++bad;
					}
				}
			}
		}
	}
	check(bad == 0, "coord con margen 2: nunca rechaza lo que float ve visible");
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
	test_aabb_yaw<eng::coord>("coord", eng::math::scalar_traits<eng::coord>::from_int(2));
	test_aabb_yaw<float>("float", 0.0f);
	test_aabb_oracle();
	test_aabb_equivalence();
	test_coord_margin_conservative();
	if (g_fail == 0) {
		std::printf("OK: culling de entrada (yaw view, frustum general, tamano, cajas y dos escalares)\n");
	} else {
		std::printf("FALLOS: %u\n", g_fail);
	}
	return g_fail == 0 ? 0 : 1;
}
