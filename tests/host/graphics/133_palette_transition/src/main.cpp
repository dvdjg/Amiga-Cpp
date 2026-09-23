// ============================================================================
// Test HOST-133: efecto de transición de paleta (palette_transition.hpp).
// ============================================================================
//
// Respalda `eng/graphics/effects/palette_transition.hpp`: el estado temporal `num/den`
// en modo una-pasada (`ping_pong = false`) y vaivén (`ping_pong = true`), el recorte de
// rango, y el parche de paleta que aporta al `FramePlan`. Es un efecto gráfico, pero su
// lógica es pura (sin hardware), así que se valida en host como el resto de algoritmos.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/133_palette_transition

#include <cstdio>

#include <eng/graphics/effects/palette_transition.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace effects = eng::graphics::effects;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

eng::Palette32 solid(eng::u16 color) {
	eng::Palette32 p {};
	for (eng::u8 i = 0; i < 32u; ++i) {
		p.color[i] = color;
	}
	return p;
}

} // namespace

int main() {
	std::printf("== HOST-133 palette_transition ==\n");

	const eng::Palette32 black = solid(0x000u);
	const eng::Palette32 white = solid(0xfffu);

	// --- Una sola pasada: 0 -> den y se queda (ping_pong = false) -------------
	{
		effects::PaletteTransitionEffect e;
		e.configure({0, 32, 8, false});
		e.bind(black, white);
		eng::graphics::FramePlan plan;

		// `runtime_palette()` se refresca en `apply_into` (igual que `PaletteCycleEffect`).
		auto at = [&](eng::u16 frame) -> const eng::Palette32& {
			e.update(frame);
			plan.clear();
			e.apply_into(plan);
			return e.runtime_palette();
		};

		check(at(0u).color[0] == 0x000u, "t=0 -> runtime = from (negro)");
		e.update(0u);
		check(e.num() == 0u && e.den() == 8u, "t=0 -> num=0");

		check(at(4u).color[0] == 0x777u, "t=4 -> runtime a mitad = 0x777");
		e.update(4u);
		check(e.num() == 4u && e.den() == 8u, "t=4 -> num=4");

		check(at(8u).color[0] == 0xfffu, "t=den -> runtime = to (blanco)");
		e.update(8u);
		check(e.num() == 8u && e.den() == 8u, "t=den -> num=den");

		check(at(20u).color[0] == 0xfffu, "una pasada: runtime sigue = to");
		e.update(20u); // más alla del final: NO envuelve
		check(e.num() == 8u, "una pasada: despues del final se queda en den");

		plan.clear();
		e.apply_into(plan);
		check(plan.palette_patch_count() == 1u, "apply_into -> 1 parche");
		const eng::graphics::PalettePatch& patch = plan.palette_patch(0u);
		check(patch.target == eng::graphics::PalettePatchTarget::Base, "parche base");
		check(patch.first == 0u && patch.count == 32u, "parche 0..31");
		check(patch.colors.data()[0] == 0xfffu && patch.colors.data()[31] == 0xfffu,
		      "parche con la paleta runtime");
	}

	// --- Vaivén: triangular (ping_pong = true) -------------------------------
	{
		effects::PaletteTransitionEffect e;
		e.configure({0, 32, 8, true});
		e.bind(black, white);

		const eng::u16 frames[5] = {0u, 8u, 16u, 9u, 6u};
		const eng::u16 want[5] = {0u, 8u, 0u, 7u, 6u};
		bool ok = true;
		for (int i = 0; i < 5; ++i) {
			e.update(frames[i]);
			if (e.num() != want[i]) {
				ok = false;
			}
		}
		check(ok, "ping_pong: 0,den,0,den-1,6");
	}

	// --- Parche de zona (`zone_line >= 0`) -----------------------------------
	{
		effects::PaletteTransitionEffect e;
		e.configure({0, 32, 8, false, 0x70});
		e.bind(black, white);
		eng::graphics::FramePlan plan;
		e.update(8u);
		plan.clear();
		e.apply_into(plan);
		check(plan.palette_patch_count() == 1u, "zona -> 1 parche");
		const eng::graphics::PalettePatch& patch = plan.palette_patch(0u);
		check(patch.target == eng::graphics::PalettePatchTarget::Zone, "parche de zona");
		check(patch.line == 0x70u, "parche en la linea de zona");
	}

	// --- Recorte de rango y efecto sin enlazar -------------------------------
	{
		effects::PaletteTransitionEffect e;
		e.configure({40, 8, 0, true}); // first fuera de rango -> {31,1}; frames 0 -> 1
		e.bind(black, white);
		check(e.range().first == 31u && e.range().count == 1u, "configure recorta el rango");
		check(e.range().frames == 1u, "configure frames=0 -> 1");

		effects::PaletteTransitionEffect unbound;
		unbound.configure({0, 32, 8, false});
		eng::graphics::FramePlan plan;
		unbound.apply_into(plan); // sin bind: no debe tocar el plan
		check(plan.palette_patch_count() == 0u, "sin bind -> no registra parche");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: palette_transition validado.\n");
	return 0;
}
