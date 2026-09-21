// ============================================================================
// Test HOST-235: inventario de hardware `eng::hw` (eng/hw/info.hpp).
// ============================================================================
//
// Valida la parte **pura** de la API de inventario: consultas de capacidad, helpers de display,
// nombres y las heuristicas (modelo, clasificacion de RAM, RTC). No llama a `probe()` (que es
// del backend y sondea hardware real; se verifica en la demo 205 sobre A500).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/235_hw_info

#include <cstdio>
#include <cstring>

#include <eng/hw/info.hpp>

using namespace eng::hw;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

HwInfo make(CpuKind cpu, Chipset chipset) {
	HwInfo h {};
	h.cpu = cpu;
	h.chipset = chipset;
	return h;
}

void test_cpu() {
	HwInfo h = make(CpuKind::M68000, Chipset::OCS);
	check(!cpu_at_least(h, CpuKind::M68020), "68000 no es >= 68020");
	check(cpu_at_least(h, CpuKind::M68000), "68000 es >= 68000");
	check(!cpu_at_least(h, CpuKind::Unknown), "Unknown nunca cuenta como umbral");

	h.cpu = CpuKind::M68020;
	check(cpu_at_least(h, CpuKind::M68020), "68020 es >= 68020");
	check(cpu_at_least(h, CpuKind::M68010), "68020 es >= 68010");
	check(!cpu_at_least(h, CpuKind::M68030), "68020 no es >= 68030");

	h.cpu = CpuKind::M68040;
	check(cpu_at_least(h, CpuKind::M68030), "68040 es >= 68030");

	h.cpu = CpuKind::Other;
	check(!cpu_at_least(h, CpuKind::M68000), "Other no se compara como familia");
}

void test_caps() {
	HwInfo h {};
	check(!has_fast_ram(h), "sin Fast RAM");
	h.fast_ram_bytes = 1u;
	check(has_fast_ram(h), "con Fast RAM");

	h.chipset = Chipset::OCS;
	check(!is_aga(h), "OCS no es AGA");
	check(!is_ecs(h), "OCS no es ECS");
	check(!can_hires(h), "OCS no soporta hires");

	h.chipset = Chipset::ECS;
	check(is_ecs(h), "ECS es ECS");
	check(can_hires(h), "ECS soporta hires");

	h.chipset = Chipset::AGA;
	h.caps.aga = true;
	check(is_aga(h), "AGA es AGA");
	check(is_ecs(h), "AGA cuenta como ECS o superior");
	check(can_hires(h), "AGA soporta hires");

	check(!is_cd32(h), "AGA no es CD32");
	h.model = MachineModel::CD32;
	check(is_cd32(h), "modelo CD32");
	h.model = MachineModel::Unknown;
	h.caps.akiko = true;
	check(is_cd32(h), "Akiko implica CD32");
}

void test_planes() {
	HwInfo h = make(CpuKind::M68000, Chipset::OCS);
	check(max_planes(h) == 6u, "OCS: 6 planos");
	check(max_indexed_colors(h) == 64u, "OCS: 64 colores indexados");
	h.caps.aga = true;
	check(max_planes(h) == 8u, "AGA: 8 planos");
	check(max_indexed_colors(h) == 256u, "AGA: 256 colores indexados");
}

void test_display() {
	HwInfo h {};
	set_display(h, 320, 256, 5);
	check(h.display.width == 320u && h.display.height == 256u, "set_display fija tamano");
	check(h.display.depth == 5u && h.display.max_colors == 32u, "5 planos = 32 colores");

	set_display(h, 640, 256, 4, /*hires=*/true, /*lace=*/false);
	check(h.display.hires && !h.display.lace, "set_display fija flags");
	check(h.display.max_colors == 16u, "4 planos = 16 colores");

	set_display(h, 320, 256, 6, false, false, /*ham=*/true);
	check(h.display.ham && h.display.max_colors == 4096u, "HAM6 = 4096 colores");

	set_display(h, 320, 256, 8, false, false, /*ham=*/true);
	check(h.display.max_colors == 262144u, "HAM8 = 262144 colores");

	set_display(h, 320, 256, 6, false, false, false, /*ehb=*/true);
	check(h.display.extrahalfbrite && h.display.max_colors == 64u, "EHB = 64 colores");

	DisplayInfo d {};
	d.width = 200; d.height = 100; d.depth = 3; d.max_colors = 8;
	set_display(h, d);
	check(h.display.width == 200u && h.display.depth == 3u, "set_display desde DisplayInfo");
}

void test_names() {
	check(std::strcmp(cpu_name(CpuKind::M68030), "68030") == 0, "nombre 68030");
	check(std::strcmp(cpu_name(CpuKind::Unknown), "?") == 0, "nombre CPU desconocida");
	check(std::strcmp(chipset_name(Chipset::AGA), "AGA") == 0, "nombre AGA");
	check(std::strcmp(model_name(MachineModel::CD32), "CD32") == 0, "nombre CD32");
	check(std::strcmp(model_name(MachineModel::A4000T), "A4000T") == 0, "nombre A4000T");
}

void test_guess_model() {
	Caps c {};
	check(guess_model(c, Chipset::OCS, 512u * 1024u) == MachineModel::A500, "OCS -> A500");
	check(guess_model(c, Chipset::ECS, 1024u * 1024u) == MachineModel::A600, "ECS -> A600");
	c.aga = true;
	check(guess_model(c, Chipset::AGA, 2u * 1024u * 1024u) == MachineModel::A1200,
	      "AGA 2MB -> A1200");
	check(guess_model(c, Chipset::AGA, 4u * 1024u * 1024u) == MachineModel::A4000,
	      "AGA >2MB -> A4000");
	c.akiko = true;
	check(guess_model(c, Chipset::AGA, 2u * 1024u * 1024u) == MachineModel::CD32,
	      "Akiko -> CD32");
	check(guess_model(c, Chipset::Unknown, 0u) == MachineModel::CD32, "Akiko gana a Unknown");
}

void test_classify_region() {
	check(classify_region(true, false, 0x000000u) == MemRegionKind::Chip, "base 0 chip");
	check(classify_region(false, true, 0x00200000u) == MemRegionKind::Fast, "MEMF_FAST");
	check(classify_region(true, false, 0x00c00000u) == MemRegionKind::Slow, "slow RAM $C00000");
	check(classify_region(false, false, 0x00200000u) == MemRegionKind::Fast, "Zorro II autoconfig");
	check(classify_region(false, false, 0x00f00000u) == MemRegionKind::Other, "otro mapeo");
	// El slow RAM gana a MEMF_CHIP por direccion (Agnus no lo ve).
	check(classify_region(true, false, 0x00d00000u) == MemRegionKind::Slow, "slow por direccion");
}

void test_rtc() {
	check(!model_has_rtc(MachineModel::A500), "A500 sin RTC");
	check(!model_has_rtc(MachineModel::A1000), "A1000 sin RTC");
	check(model_has_rtc(MachineModel::A500Plus), "A500+ con RTC");
	check(model_has_rtc(MachineModel::A1200), "A1200 con RTC");
	check(model_has_rtc(MachineModel::CD32), "CD32 con RTC");
}

} // namespace

int main() {
	test_cpu();
	test_caps();
	test_planes();
	test_display();
	test_names();
	test_guess_model();
	test_classify_region();
	test_rtc();

	if (failures == 0) {
		std::printf("OK: eng::hw inventario (consultas y heuristicas) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
