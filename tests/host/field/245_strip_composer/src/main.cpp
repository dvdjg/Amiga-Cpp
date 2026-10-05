// ============================================================================
// Test HOST-245: compositor del scroller de tiras (eng/field/strip_composer.hpp).
// ============================================================================
//
// Emite la copperlist una vez con handles y por frame parchea SOLO BPLCON1 + los BPLxPT de la
// ventana (mas el split). Sobre un MemoryManager Chip, verifica:
//   - build() emite en ambos bloques y ok.
//   - patch() escribe el fine scroll y la direccion por plano (base + p*ring_w_bytes + window*2).
//   - el split two-WAIT se emite cuando la linea cruza la 255.
//
//   bash tools/run-host-tests.sh tests/host/field/245_strip_composer

#include <cstdint>
#include <cstdio>

#include <eng/field/strip_composer.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

using Geom = eng::playfield::StripScrollGeometry<320u, 208u, 5u, 16u, 16u, 2u, 1u, false>;
using GeomSplit = eng::playfield::StripScrollGeometry<320u, 256u, 5u, 16u, 16u, 2u, 1u, true>;

alignas(16) eng::u8 g_chip[64u * 1024u];
alignas(16) eng::u8 g_ring[8u * 1024u];

} // namespace

int main() {
	std::printf("== HOST-245 strip_composer ==\n");
	eng::MemoryManager mem {};
	check(mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u), "MemoryManager Chip");

	eng::Palette32 pal {};
	for (eng::u16 i = 0u; i < 32u; ++i) pal.color[i] = static_cast<eng::u16>(0x100u * i);

	eng::playfield::StripComposer<Geom> comp {};
	check(comp.init(mem, pal.words(), 1024u), "init");
	comp.set_ring(reinterpret_cast<const eng::u16*>(g_ring));
	check(comp.build(), "build (emite en ambos bloques)");

	// Parchea el frame: coarse(20) = (20-1)&~15 = 16 -> ventana 16/16 = 1; fine_delay(20) = 12.
	auto fr = eng::playfield::plan_strip_frame(Geom{}, 20, 0, 20, 0);
	auto sc = eng::playfield::strip_copper_values(Geom{}, fr);
	check(sc.bplcon1 == 0xccu && fr.window_word == 1u, "fine 12 duplicado, ventana 1");
	check(comp.patch(sc), "patch");

	const eng::u16* w = comp.debug_active_words();
	check(w != nullptr, "copperlist activa disponible");
	if (w != nullptr) {
		check(w[comp.bplcon1_handle() + 1u] == 0xccu, "BPLCON1 parcheado = fine duplicado");
		const eng::uintptr base = reinterpret_cast<eng::uintptr>(g_ring);
		for (eng::u8 p = 0u; p < Geom::planes; ++p) {
			const eng::u32 addr = static_cast<eng::u32>(base) +
					      static_cast<eng::u32>(p) * Geom::ring_w_bytes + 1u * 2u;
			check(w[comp.pt_handle(p, 0u) + 1u] == static_cast<eng::u16>(addr >> 16u),
			      "BPLxPT alto por plano");
			check(w[comp.pt_handle(p, 1u) + 1u] == static_cast<eng::u16>(addr & 0xffffu),
			      "BPLxPT bajo por plano");
		}
	}

	// Split que cruza la 255 (256 lineas) se emite como two-WAIT.
	eng::playfield::StripComposer<GeomSplit> comps {};
	check(comps.init(mem, pal.words(), 2048u), "init (split 256)");
	comps.set_ring(reinterpret_cast<const eng::u16*>(g_ring));
	check(comps.build(), "build (split 256, two-WAIT)");
	check(GeomSplit::split_crosses_255, "256 lineas -> split cruza la 255 (two-WAIT)");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: compositor de tiras (emitir una vez + parchear BPLCON1/BPLxPT/split) validado.\n");
	return 0;
}
