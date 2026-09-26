// ============================================================================
// Test HOST-042: ModeSwitchZone (conmutacion de geometria de video a mitad de frame)
// ============================================================================
//
// Valida el orden canonico MI09 que emite `Scheduler::emit_mode_switch_zone`:
//   WAIT top -> BPLCON0 -> [BPLCON4] -> DDFSTRT/DDFSTOP -> BPL1MOD/BPL2MOD
//     -> BPLxPT (par por plano) -> [paleta]
// y las guardas (DDF desalineado / planos fuera de rango / sin base). Base de la
// Fase 1b (HUD con menos planos) del refactor de playfields.

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/mem_bank.hpp>

namespace {

using eng::u16;
using eng::u32;
using eng::MemoryBlock;
using eng::MemoryKind;
using eng::copper::Register;
using eng::copper::Scheduler;
using eng::graphics::ModeSwitchZone;

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

u16 g_words[128];
eng::u8 g_planes[200];
eng::MemBank<eng::MemoryKind::Chip> g_planes_bank {};
u16 g_pal[4] = {0x0f00u, 0x00f0u, 0x0000u, 0x0000u};

MemoryBlock words_block() {
	return MemoryBlock { g_words, sizeof(g_words), MemoryKind::Chip };
}

constexpr u16 lo(eng::uintptr a) { return static_cast<u16>(a & 0xffffu); }
constexpr u16 hi(eng::uintptr a) { return static_cast<u16>((a >> 16) & 0xffffu); }

} // namespace

int main() {
	// --- Zona base: 2 planos, BPLCON4, DDF y paleta de 2 colores ---------------
	g_planes_bank.configure(g_planes, sizeof(g_planes), 2u);
	const eng::ChipPlaneView planes =
		g_planes_bank.reserve<eng::PlaneTag>(sizeof(g_planes), 2u).mem_view();
	Scheduler sched { words_block() };
	ModeSwitchZone zone {};
	zone.top = 0x50u;
	zone.bplcon0 = 0x2200u;   // COLOR + BPU=2
	zone.set_bplcon4 = true;
	zone.bplcon4 = 0x0000u;   // salir de EHB en el tramo
	zone.ddfstrt = 0x0038u;
	zone.ddfstop = 0x00d0u;
	zone.bpl1mod = 0x0000u;
	zone.bpl2mod = 0x0000u;
	zone.planes = 2u;
	zone.plane_bytes = 40u;
	zone.bitplanes = planes;
	zone.set_bplcon1 = true;
	zone.bplcon1 = 0x0000u;
	zone.palette = eng::PaletteWords { g_pal, 2u };
	zone.palette_colors = 2u;

	check(zone.ddf_aligned(), "DDF alineado");
	check(sched.emit_mode_switch_zone(zone), "zona emitida");

	const u16* w = sched.data();
	u16 i = 0u;
	// WAIT de la linea del corte (mascara V-only 0xff00).
	check(w[i] == 0x5001u && w[i + 1u] == 0xff00u, "WAIT top");
	i = static_cast<u16>(i + 2u);
	// BPLCON0 primero.
	check(w[i] == static_cast<u16>(Register::BPLCON0) && w[i + 1u] == 0x2200u, "BPLCON0");
	i = static_cast<u16>(i + 2u);
	// DDF antes que modulos.
	check(w[i] == static_cast<u16>(Register::DDFSTRT) && w[i + 1u] == 0x0038u, "DDFSTRT");
	i = static_cast<u16>(i + 2u);
	check(w[i] == static_cast<u16>(Register::DDFSTOP) && w[i + 1u] == 0x00d0u, "DDFSTOP");
	i = static_cast<u16>(i + 2u);
	check(w[i] == static_cast<u16>(Register::BPL1MOD) && w[i + 1u] == 0x0000u, "BPL1MOD");
	i = static_cast<u16>(i + 2u);
	check(w[i] == static_cast<u16>(Register::BPL2MOD) && w[i + 1u] == 0x0000u, "BPL2MOD");
	i = static_cast<u16>(i + 2u);
	// Punteros: plano 0 en la base, plano 1 a +plane_bytes.
	const eng::uintptr base = planes.address().value;
	check(w[i] == static_cast<u16>(Register::BPL1PTH) && w[i + 1u] == hi(base), "BPL1PTH");
	i = static_cast<u16>(i + 2u);
	check(w[i] == static_cast<u16>(Register::BPL1PTL) && w[i + 1u] == lo(base), "BPL1PTL");
	i = static_cast<u16>(i + 2u);
	check(w[i] == static_cast<u16>(Register::BPL2PTH) && w[i + 1u] == hi(base + 40u), "BPL2PTH");
	i = static_cast<u16>(i + 2u);
	check(w[i] == static_cast<u16>(Register::BPL2PTL) && w[i + 1u] == lo(base + 40u), "BPL2PTL");
	i = static_cast<u16>(i + 2u);
	// Modo (BPLCON4/BPLCON1) DESPUES de los punteros: no intercalarlo en la
	// geometria (el DMA perderia el ultimo plano; ver scheduler.hpp).
	check(w[i] == static_cast<u16>(Register::BPLCON4) && w[i + 1u] == 0x0000u, "BPLCON4 tras punteros");
	i = static_cast<u16>(i + 2u);
	check(w[i] == static_cast<u16>(Register::BPLCON1) && w[i + 1u] == 0x0000u, "BPLCON1 tras punteros");
	i = static_cast<u16>(i + 2u);
	// Paleta al final.
	check(w[i] == static_cast<u16>(Register::COLOR00) && w[i + 1u] == 0x0f00u, "COLOR00");
	i = static_cast<u16>(i + 2u);
	check(w[i] == 0x0182u && w[i + 1u] == 0x00f0u, "COLOR01");

	const auto& rep = sched.report();
	check(rep.display_moves == 11u, "display_moves = 7 + 2*2");
	check(rep.palette_moves == 2u, "palette_moves = 2");
	check(rep.waits == 1u, "un WAIT");

	// --- Sin BPLCON4 y sin paleta: se omiten --------------------------------
	Scheduler s2 { words_block() };
	ModeSwitchZone z2 {};
	z2.top = 0x40u;
	z2.bplcon0 = 0x1200u;
	z2.ddfstrt = 0x0038u;
	z2.ddfstop = 0x00d0u;
	z2.planes = 0u;          // solo geometria, sin punteros
	check(s2.emit_mode_switch_zone(z2), "zona sin punteros ni paleta");
	// WAIT + BPLCON0 + DDFSTRT + DDFSTOP + BPL1MOD + BPL2MOD = 12 words.
	check(s2.words_used() == 12u, "sin opcionales: 12 words");
	check(s2.report().palette_moves == 0u, "sin palette_moves");

	// --- Guardas (no emiten nada) -------------------------------------------
	Scheduler s3 { words_block() };
	ModeSwitchZone bad = zone;
	bad.ddfstrt = 0x0039u;   // no multiplo de 4
	check(!bad.ddf_aligned(), "DDF desalineado detectado");
	check(!s3.emit_mode_switch_zone(bad), "DDF desalineado rechazado");
	check(s3.words_used() == 0u, "rechazo no escribe words");

	Scheduler s4 { words_block() };
	ModeSwitchZone bad2 = zone;
	bad2.planes = 2u;
	bad2.bitplanes = {};     // sin base con planes > 0
	check(!s4.emit_mode_switch_zone(bad2), "sin base rechazado");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: ModeSwitchZone (orden canonico BPLCON0->DDF->mods->BPLxPT) validado.\n");
	return 0;
}
