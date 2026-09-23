// ============================================================================
// Test HOST-134: degradado por banda (raster_gradient.hpp).
// ============================================================================
//
// Respalda `eng/graphics/effects/raster_gradient.hpp`: el muestreo de la lista de colores
// clave (lineal y cíclico, con `phase`), la geometría de las bandas (`top`) y las
// intenciones `PaletteLine` que produce (`first`/`count`/vista de color). Es lógica pura:
// no necesita `copper::Plan` ni hardware, así que se prueba en host.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/134_raster_gradient

#include <cstdio>

#include <eng/api/effects.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>

namespace effects = eng::graphics::effects;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct FakePlan {
	eng::u16 count = 0;
	const eng::graphics::CopperIntent* last = nullptr;
	void add(const eng::graphics::CopperIntent* intents, eng::u16 n) {
		count = n;
		last = intents;
	}
};

} // namespace

int main() {
	std::printf("== HOST-134 raster_gradient ==\n");

	// --- Degradado lineal: extremos fijos -----------------------------------
	{
		effects::RasterGradientEffect e;
		e.configure({0x2cu, 8u, 4u, 0u});
		e.set_cyclic(false);
		const eng::u16 keys[2] = {0x000u, 0xfffu};
		e.set_keys(keys);

		eng::graphics::CopperIntent out[effects::RasterGradientEffect::max_bands] {};
		const eng::u16 n = e.fill_intents(out);
		check(n == 4u, "lineal: 4 bandas");
		check(out[0].top == 0x2cu && out[1].top == 0x34u && out[3].top == 0x44u,
		      "lineal: top = first_line + b*band_height");
		check(out[0].kind == eng::graphics::CopperIntentKind::PaletteLine &&
			      out[0].count == 1u && out[0].first == 0u,
		      "lineal: PaletteLine, count=1, first=0");
		check(out[0].colors[0] == 0x000u, "lineal: banda 0 = primera clave");
		check(out[1].colors[0] == 0x555u, "lineal: banda 1 = 1/3");
		check(out[2].colors[0] == 0xaaau, "lineal: banda 2 = 2/3");
		check(out[3].colors[0] == 0xfffu, "lineal: banda 3 = ultima clave");
	}

	// --- Degradado ciclico con phase (color cycling por banda) ---------------
	{
		effects::RasterGradientEffect e;
		e.configure({0x2cu, 16u, 3u, 0u});
		e.set_cyclic(true);
		const eng::u16 keys[3] = {0x00fu, 0x0f0u, 0xf00u};
		e.set_keys(keys);

		eng::graphics::CopperIntent out[effects::RasterGradientEffect::max_bands] {};
		e.fill_intents(out);
		check(out[0].colors[0] == 0x00fu && out[1].colors[0] == 0x0f0u &&
			      out[2].colors[0] == 0xf00u,
		      "ciclico: bandas = claves en orden");

		e.set_phase(1u);
		e.fill_intents(out);
		check(out[0].colors[0] == 0x0f0u && out[1].colors[0] == 0xf00u &&
			      out[2].colors[0] == 0x00fu,
		      "ciclico: phase=1 rota las claves");
	}

	// --- first=1: la vista abarca first+1 y el color va en el indice 1 -------
	{
		effects::RasterGradientEffect e;
		e.configure({0x2cu, 1u, 2u, 1u});
		const eng::u16 keys[2] = {0x123u, 0x456u};
		e.set_keys(keys);
		eng::graphics::CopperIntent out[effects::RasterGradientEffect::max_bands] {};
		e.fill_intents(out);
		check(out[0].first == 1u && out[0].colors.size() == 2u, "first=1: vista de 2");
		check(out[0].colors[1] == 0x123u, "first=1: color en el indice 1");
	}

	// --- Limites: cap, sin claves, plan --------------------------------------
	{
		effects::RasterGradientEffect e;
		e.configure({0x2cu, 4u, 8u, 0u});
		const eng::u16 keys[2] = {0x000u, 0xfffu};
		e.set_keys(keys);
		eng::graphics::CopperIntent out[effects::RasterGradientEffect::max_bands] {};
		check(e.fill_intents({out, 3u}) == 3u, "cap: recorta al maximo pedido");

		effects::RasterGradientEffect empty;
		empty.configure({0x2cu, 4u, 8u, 0u});
		check(empty.fill_intents({out, 8u}) == 0u, "sin claves: 0 intenciones");

		FakePlan plan;
		e.apply_into(plan);
		check(plan.count == 8u && plan.last != nullptr, "apply_into: plan.add con 8");
	}

	// --- effects::Gradient (envoltorio de API, mismo contrato `Effect`) ------
	{
		eng::effects::Gradient g;
		const eng::u16 keys[2] = {0x000u, 0xfffu};
		check(g.attach({0x2cu, 8u, 4u, 0u}, keys),
		      "Gradient: attach con claves");
		FakePlan plan;
		g.apply_into(plan);
		check(plan.count == 4u && plan.last != nullptr, "Gradient: apply_into emite 4 intenciones");

		eng::effects::Gradient empty;
		check(!empty.attach({0x2cu, 8u, 4u, 0u}, eng::Span<const eng::u16> {}),
		      "Gradient: sin claves -> false");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: raster_gradient validado.\n");
	return 0;
}
