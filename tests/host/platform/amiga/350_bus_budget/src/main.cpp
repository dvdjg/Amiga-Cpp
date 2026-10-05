// ============================================================================
// Test HOST-350: presupuesto de bus DMA del Amiga 500 (eng/hw/bus_budget.hpp).
// ============================================================================
//
// Herramienta de planificacion: verifica que el modelo acumula los recursos que
// compiten por el bus de Chip RAM (display/bitplanes+sprites, Copper, Blitter, CPU),
// devuelve el margen restante, el cuello de botella y las pistas, y responde a
// cambios (planos, hires, Fast RAM, FPS).
//
//   bash tools/run-host-tests.sh tests/host/platform/amiga/350_bus_budget

#include <cstdio>

#include <eng/hw/bus_budget.hpp>

using namespace eng;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

hw::BusBand band(u16 h, u16 w, u8 planes, bool hires = false, u8 sprites = 0u) {
	hw::BusBand b {};
	b.height = h;
	b.width = w;
	b.bitplanes = planes;
	b.hires = hires;
	b.sprites_active = sprites;
	return b;
}

} // namespace

int main() {
	std::printf("== HOST-350 bus_budget ==\n");

	// Base: 320x256, 4 planos, sin nada mas. total = 227*312; display = (4+80)*256.
	{
		hw::BusBudgetInput in {};
		in.bands_count = 1u;
		in.bands[0] = band(256u, 320u, 4u);
		const auto r = hw::amiga500_bus_budget(in);
		check(r.total_slots == 227u * 312u, "total slots PAL");
		check(r.display_slots == 84u * 256u, "display = (refresh 4 + bp 80) * 256");
		check(r.used_slots == r.display_slots && r.copper_slots == 0u && r.blitter_slots == 0u,
		      "sin copper/blitter/cpu");
		check(r.remaining_slots == static_cast<s32>(r.total_slots - r.used_slots), "margen restante");
		check(r.bottleneck == static_cast<u8>(hw::BusResource::Display), "cuello: display");
		check((r.hints & hw::kHintSixPlanesWide) == 0u, "4 planos no dispara 6-planos");
	}

	// 6 planos a 320 satura (hint clasico) y sube el display.
	{
		hw::BusBudgetInput in {};
		in.bands_count = 1u;
		in.bands[0] = band(256u, 320u, 6u);
		const auto r = hw::amiga500_bus_budget(in);
		check(r.display_slots == (4u + 120u) * 256u, "6 planos: bp = 20*6 = 120");
		check((r.hints & hw::kHintSixPlanesWide) != 0u, "6 planos + width>288 -> hint");
	}
	// 6 planos a 288 ya no dispara el hint.
	{
		hw::BusBudgetInput in {};
		in.bands_count = 1u;
		in.bands[0] = band(240u, 288u, 6u);
		const auto r = hw::amiga500_bus_budget(in);
		check((r.hints & hw::kHintSixPlanesWide) == 0u, "6 planos a 288 no dispara el hint");
	}

	// Hires 640x256, 4 planos: roba casi todo el even.
	{
		hw::BusBudgetInput in {};
		in.bands_count = 1u;
		in.bands[0] = band(256u, 640u, 4u, true);
		const auto r = hw::amiga500_bus_budget(in);
		check(r.display_slots == (4u + 160u) * 256u, "hires: 40 palabras/plano");
		check((r.hints & hw::kHintHiresHeavy) != 0u, "hires 4 planos -> hint");
	}

	// Audio + sprites suman al coste fijo por linea.
	{
		hw::BusBudgetInput in {};
		in.audio_channels = 4u;
		in.bands_count = 1u;
		in.bands[0] = band(256u, 320u, 4u, false, 4u);
		const auto r = hw::amiga500_bus_budget(in);
		const u32 fixed = 4u + 4u + 4u * 2u; // refresh + audio(4) + sprites(4*2)
		check(r.display_slots == (fixed + 80u) * 256u, "audio y sprites suman por linea");
	}

	// Blitter: palabras * canales.
	{
		hw::BusBudgetInput in {};
		in.bands_count = 1u;
		in.bands[0] = band(256u, 320u, 4u);
		in.blitter_words = 1000u;
		in.blitter_channels = 4u;
		const auto r = hw::amiga500_bus_budget(in);
		check(r.blitter_slots == 4000u, "blitter = palabras * canales");
		check(r.used_slots == r.display_slots + 4000u, "blitter suma al usado");
	}

	// CPU en Chip vs Fast RAM.
	{
		hw::BusBudgetInput in {};
		in.bands_count = 1u;
		in.bands[0] = band(256u, 320u, 4u);
		in.cpu_chip_cycles = 10000u;
		const auto slow = hw::amiga500_bus_budget(in);
		check(slow.cpu_slots == 10000u, "CPU en Chip cuenta");
		in.use_fastram = true;
		const auto fast = hw::amiga500_bus_budget(in);
		check(fast.cpu_slots == 0u, "Fast RAM alivia el bus (CPU = 0)");
	}

	// 25 fps: el frame logico cubre 2 VBlanks -> doble presupuesto.
	{
		hw::BusBudgetInput in {};
		in.bands_count = 1u;
		in.bands[0] = band(256u, 320u, 4u);
		const auto at50 = hw::amiga500_bus_budget(in);
		in.fps = 25u;
		const auto at25 = hw::amiga500_bus_budget(in);
		check(at25.total_slots == at50.total_slots * 2u, "25 fps duplica el presupuesto logico");
	}

	// Sobre-presupuesto: CPU enorme -> remaining negativo + hint.
	{
		hw::BusBudgetInput in {};
		in.bands_count = 1u;
		in.bands[0] = band(256u, 320u, 4u);
		in.cpu_chip_cycles = 100000u;
		const auto r = hw::amiga500_bus_budget(in);
		check(r.remaining_slots < 0, "exceso -> remaining negativo");
		check((r.hints & hw::kHintOverBudget) != 0u, "exceso -> hint over-budget");
		check((r.hints & hw::kHintCpuNeedsFast) != 0u, "CPU muy caro -> hint Fast RAM");
	}

	// Multi-franja: el coste se acumula (status 3 planos + juego 6 + panel hires 2).
	{
		hw::BusBudgetInput in {};
		in.bands_count = 3u;
		in.bands[0] = band(32u, 320u, 3u);
		in.bands[1] = band(180u, 288u, 6u);
		in.bands[2] = band(24u, 640u, 2u, true);
		const auto r = hw::amiga500_bus_budget(in);
		const u32 expect = (4u + 20u * 3u) * 32u + (4u + 18u * 6u) * 180u + (4u + 40u * 2u) * 24u;
		check(r.display_slots == expect, "multi-franja acumula el display");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: presupuesto de bus A500 (recursos + margen + cuello de botella + pistas) validado.\n");
	return 0;
}
