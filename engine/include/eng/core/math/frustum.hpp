#pragma once

/// \file frustum.hpp
/// **Selección de objeto (culling de entrada)**: rechazar entidades ANTES de transformarlas.
/// La métrica de éxito de un juego 3D en A500 no es «culling perfecto» sino **objetos que
/// nunca llaman a `transform_vertices`** (transformar 60 vértices ≈ 57k ciclos medidos;
/// multiplicarlo por objetos fuera de pantalla mata el frame antes que cualquier orden de
/// caras). El culling por capas (`3D_GAME_ARCHITECTURE.md` §Visibilidad): sector/portales →
/// **esfera/AABB contra el volumen visible** (esta cabecera) → back-face y painter (ya en
/// `mesh3d`) → far y tamaño en píxeles.
///
/// Dos vías de volumen visible, ambas genéricas sobre el escalar `S`:
///
/// - **`YawView`** (vía rápida del género: cámara que sólo gira en yaw, como las demos 080/
///   F5): cuña 2D en el plano XZ + franja vertical + rango de profundidad. Sin planos
///   normalizados ni `sqrt`: los rechazos conservadores usan la cota `sqrt(1+W²) ≤ 1+W`
///   (nunca descarta un objeto visible) con 4–6 comparaciones y pares de `mul`/`add`.
/// - **`Frustum`** (caso general: pitch/roll, planos explícitos): test estándar esfera-planos.
///
/// Convenio de cámara (idéntico a `project_perspective`/las demos): el espacio de cámara se
/// mira hacia **+Z**, `x` derecha e **`y` positiva hacia abajo**; el yaw gira la escena con
/// `xc = x·cos − z·sin`, `zc = x·sin + z·cos`.
///
/// Convenio de borde: un objeto que **cruza el plano cercano se considera VISIBLE** (no se
/// descarta): el recorte fino lo hacen el clipping de cámara y el 2D del renderer. Culling
/// conservador = cero falsos negativos; los falsos positivos los paga el pipeline ya existente.

#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/math/scalar_fwd.hpp>
#include <eng/core/types/types.hpp>

namespace eng::math3d {

/// Plano genérico `n·p + d >= 0` para el lado interior.
template <class S>
struct Plane {
	S nx {};
	S ny {};
	S nz {};
	S d {};
};

/// Volumen de visibilidad por planos (4–6: izquierda, derecha, arriba, abajo, cerca, lejos).
template <class S, eng::u8 MaxPlanes = 6>
struct Frustum {
	Plane<S> planes[MaxPlanes] {};
	eng::u8 count = 0u;
};

/// **Esfera contra frustum**: `false` = completamente fuera (rechazar sin transformar).
/// `center` y `radius` en el MISMO espacio que los planos (típicamente mundo→cámara antes).
template <class S>
[[nodiscard]] constexpr bool sphere_visible(const Frustum<S>& f, const eng::math::Vec<3, S>& center, S radius) {
	for (eng::u8 i = 0u; i < f.count; ++i) {
		const Plane<S>& p = f.planes[i];
		const S dist = eng::math::mul_norm(p.nx, center.v[0]) +
			       eng::math::mul_norm(p.ny, center.v[1]) +
			       eng::math::mul_norm(p.nz, center.v[2]) + p.d;
		if (dist < -radius) {
			return false;
		}
	}
	return true;
}

/// **Vista yaw-only** (la del género en A500): cuña horizontal + franja vertical + rango Z.
/// `half_w` = tangente de medio ángulo horizontal (`(ancho/2)/focal` en la proyección de las
/// demos); la cámara está en el origen de mundo y sólo gira en yaw.
template <class S>
struct YawView {
	S sin_yaw {};
	S cos_yaw {};
	S half_w {};  ///< tan(medio FOV horizontal) como ratio (p. ej. 160/focal)
	S y_min {};   ///< franja vertical visible (mundo = cámara; y positiva hacia abajo)
	S y_max {};
	S z_near {};
	S z_far {};
};

/// **Esfera contra la vista yaw-only** (conservador, sin `sqrt` ni normalizar planos).
/// Usa `sqrt(1+W²) ≤ 1+W` en el test de la cuña: nunca descarta un objeto de borde visible.
template <class S>
[[nodiscard]] constexpr bool sphere_visible(const YawView<S>& v, const eng::math::Vec<3, S>& c, S r) {
	using T = eng::math::scalar_traits<S>;
	// A cámara.
	const S xc = eng::math::mul_norm(c.v[0], v.cos_yaw) - eng::math::mul_norm(c.v[2], v.sin_yaw);
	const S yc = c.v[1];
	const S zc = eng::math::mul_norm(c.v[0], v.sin_yaw) + eng::math::mul_norm(c.v[2], v.cos_yaw);
	// Profundidad: cerca conservador (si cruza el plano cercano, visible).
	const S z_far_x = v.z_far + r;
	if (zc > z_far_x) {
		return false;
	}
	if (zc < v.z_near - r) {
		return false; // completamente detrás del plano cercano
	}
	// Franja vertical.
	if (yc + r < v.y_min || yc - r > v.y_max) {
		return false;
	}
	// Cuña horizontal: |xc| <= zc·W con holgura conservadora r·(1+W).
	const S slack = eng::math::mul_norm(r, T::one() + v.half_w);
	const S w_z = eng::math::mul_norm(zc, v.half_w);
	if (xc + slack < -w_z) {
		return false; // fuera por la izquierda
	}
	if (xc - slack > w_z) {
		return false; // fuera por la derecha
	}
	return true;
}

/// **Descarte por tamaño en pantalla**: `false` = subpíxel (radio proyectado < `min_px`) a esa
/// profundidad. Algebra sin división: `radio·focal >= min_px·z`.
template <class S>
[[nodiscard]] constexpr bool screen_size_visible(S radius, S focal, S z, S min_px) {
	return eng::math::mul_norm(radius, focal) >= eng::math::mul_norm(min_px, z);
}

/// Construye un **frustum general** desde la vista yaw-only (mismos parámetros): útil para el
/// caso con pitch/roll futuro y como oráculo de equivalencia del test de la vía rápida.
/// Los planos de la cuña van **pre-escalados por `1/(1+W)`**: con eso, el test genérico
/// `dist < -r` es EXACTAMENTE el conservador `|xc| - r·(1+W) > zc·W` de la vía rápida
/// (la cota `sqrt(1+W²) ≤ 1+W`), y ambas vías deben coincidir.
template <class S>
[[nodiscard]] constexpr Frustum<S> frustum_from_yaw_view(const YawView<S>& v) {
	using T = eng::math::scalar_traits<S>;
	const S w1 = T::one() + v.half_w;
	const S inv = eng::math::div_norm(T::one(), w1);
	Frustum<S> f {};
	// Cuña (cámara: zc·W + xc >= 0 izquierda y zc·W - xc >= 0 derecha, con xc/zc la rotación
	// de yaw) escalada por 1/(1+W):
	//   izquierda: x·(cos+W·sin) + z·(W·cos−sin) >= 0
	//   derecha:   x·(W·sin−cos) + z·(W·cos+sin) >= 0
	const S ws = eng::math::mul_norm(v.half_w, v.sin_yaw);
	const S wc = eng::math::mul_norm(v.half_w, v.cos_yaw);
	f.planes[0] = Plane<S> {eng::math::mul_norm(v.cos_yaw + ws, inv), T::zero(),
				eng::math::mul_norm(wc - v.sin_yaw, inv), T::zero()};
	f.planes[1] = Plane<S> {eng::math::mul_norm(ws - v.cos_yaw, inv), T::zero(),
				eng::math::mul_norm(wc + v.sin_yaw, inv), T::zero()};
	// Franja vertical.
	f.planes[2] = Plane<S> {T::zero(), T::one(), T::zero(), -v.y_min};
	f.planes[3] = Plane<S> {T::zero(), -T::one(), T::zero(), v.y_max};
	// Cerca (visible si delante): z - z_near >= 0.
	f.planes[4] = Plane<S> {T::zero(), T::zero(), T::one(), -v.z_near};
	// Lejos: z_far - z >= 0.
	f.planes[5] = Plane<S> {T::zero(), T::zero(), -T::one(), v.z_far};
	f.count = 6u;
	return f;
}

} // namespace eng::math3d
