// Test HOST-367: eng::graphics::c2p — la conversion chunky->planar como FUNCION con despacho por
// **banco de memoria**: si ambas memorias son Chip, delega en el Blitter (`blit_submit`); si
// alguna no lo esta, hace la via **CPU** (Kalms). Fija la regla «los tags eligen el metodo».
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/graphics/367_c2p

#include <eng/graphics/c2p.hpp>

#include <cstdio>

using eng::MemoryKind;
using eng::u32;

namespace {
int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}
} // namespace

int main() {
	std::printf("== HOST-367 c2p ==\n");

	eng::u8 chunky[16] {};
	for (u32 i = 0; i < 16u; ++i) {
		chunky[i] = static_cast<eng::u8>(i & 0x0fu);
	}
	int blit_calls = 0;
	auto submit = [&](auto, auto, u32, u32, u32, eng::u8) -> bool {
		++blit_calls;
		return true;
	};

	// --- Fuente y destino FUERA de Chip -> debe ir por CPU (no toca el Blitter) ---------
	eng::u8 planes_cpu[64] {};
	const eng::MemView<eng::ChunkyTag, MemoryKind::Fast> cf {
		eng::Address<MemoryKind::Fast>::from_storage(chunky), sizeof(chunky)};
	const eng::MemView<eng::PlaneTag, MemoryKind::Fast> pf {
		eng::Address<MemoryKind::Fast>::from_storage(planes_cpu), sizeof(planes_cpu)};
	check(eng::graphics::c2p(cf, pf, 16u, 1u, 8u, 4u, submit), "c2p CPU devuelve true");
	check(blit_calls == 0, "fuera de Chip -> CPU (no llama al Blitter)");
	bool wrote = false;
	for (u32 i = 0; i < 32u; ++i) {
		if (planes_cpu[i] != 0u) {
			wrote = true;
		}
	}
	check(wrote, "la via CPU escribio contenido planar");

	// --- Ambas en Chip -> delega en el Blitter (`blit_submit`), sin ejecutar la CPU ------
	eng::u8 chip_chunky[16] {};
	eng::u8 chip_planes[64] {};
	const eng::MemView<eng::ChunkyTag, MemoryKind::Chip> cc {
		eng::Address<MemoryKind::Chip>::from_storage(chip_chunky), sizeof(chip_chunky)};
	const eng::MemView<eng::PlaneTag, MemoryKind::Chip> pc {
		eng::Address<MemoryKind::Chip>::from_storage(chip_planes), sizeof(chip_planes)};
	check(eng::graphics::c2p(cc, pc, 16u, 1u, 8u, 4u, submit), "c2p Blitter devuelve true");
	check(blit_calls == 1, "ambas Chip -> usa el Blitter");
	check(chip_planes[0] == 0u, "la via Blitter no escribe en CPU");

	// --- C2P generico a 5 planos (32 colores): transposicion correcta por bits --------------
	// `c2p_1x1_naive` cubre profundidades que `c2p_1x1_4` no (5 = 32 colores, 6 = 64); es la via
	// del `IndexedDisplay<5>` (framebuffer indexado de 32 colores, p. ej. el PPU NES).
	eng::u8 idx5[16] {};
	for (u32 i = 0; i < 16u; ++i) {
		idx5[i] = static_cast<eng::u8>(i & 0x1fu); // indices 0..31
	}
	eng::u8 plan5[5u * 2u] {}; // 5 planos x 2 bytes (16 px, 1 fila)
	eng::graphics::c2p_1x1_naive(16u, 1u, 5u, 2u, eng::ChunkyView {idx5, 16u},
				     eng::PlaneBytes {plan5, sizeof(plan5)});
	bool ok5 = true;
	for (u32 p = 0; p < 5u; ++p) {
		eng::u8 want0 = 0u;
		eng::u8 want1 = 0u;
		for (u32 k = 0; k < 16u; ++k) {
			const eng::u8 bit = static_cast<eng::u8>((idx5[k] >> p) & 1u);
			if (k < 8u) {
				want0 = static_cast<eng::u8>(want0 | static_cast<eng::u8>(bit << (7u - k)));
			} else {
				want1 = static_cast<eng::u8>(want1 | static_cast<eng::u8>(bit << (15u - k)));
			}
		}
		if (plan5[p * 2u + 0u] != want0 || plan5[p * 2u + 1u] != want1) {
			ok5 = false;
		}
	}
	check(ok5, "c2p_1x1_naive 5 planos: transposicion correcta (32 colores)");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: c2p (despacho por banco + 5 planos) validado.\n");
	return 0;
}
