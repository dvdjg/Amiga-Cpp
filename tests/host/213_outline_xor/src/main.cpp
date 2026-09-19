// Test host de `eng::retro::flat_shade_xor` (contorno EOR + area fill XOR, técnica Amiga):
// valida la SECUENCIA de Blitter sobre un backend mock (sin hardware): comunes fijados una
// vez, horizontales descartadas, una línea por plano del color y un solo area fill.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/213_outline_xor

#include <cstdio>

#include <eng/retro/flat_shade_xor.hpp>

using namespace eng;
using namespace eng::retro;

namespace {

struct MockBackend {
	struct LineEorParams {
		int dummy = 0;
	};
	int begins = 0;
	int prepares = 0;
	int draws = 0;
	int fills = 0;
	u16 fill_planes = 0;
	u16 fill_w = 0;
	u16 fill_h = 0;

	void blitter_lines_eor_begin(u16) { ++begins; }
	bool blitter_line_eor_prepare(LineEorParams& out, u16, s16, s16, s16, s16) {
		out.dummy = 1;
		++prepares;
		return true;
	}
	void blitter_line_eor_draw(const LineEorParams&, u8*, u8*) { ++draws; }
	bool blitter_area_fill(eng::PlaneBytes, u8 planes, u16, u32, u16 w, u16 h, bool) {
		++fills;
		fill_planes = planes;
		fill_w = w;
		fill_h = h;
		return true;
	}
};

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	// Tres aristas: diagonal en planos 0 y 2, horizontal (descartada), vertical en plano 1.
	const OutlineEdge edges[3] = {
		{10, 10, 50, 40, 0x05}, // bits 0 y 2
		{20, 60, 80, 60, 0x0f}, // horizontal -> se descarta
		{30, 10, 30, 70, 0x02}, // bit 1
	};

	MockBackend backend;
	const u32 lines = flat_shade_xor(backend, eng::PlaneBytes {}, 40u, 8192u, 4u, 256u, 256u,
					 eng::Span<const OutlineEdge> {edges, 3u});

	check(backend.begins == 1, "los comunes del modo linea se fijan UNA vez");
	check(backend.prepares == 2, "la arista horizontal se descarta (2 prepares)");
	check(backend.draws == 3, "una linea por plano del color (2 + 1)");
	check(lines == 3u, "flat_shade_xor devuelve las lineas-plano lanzadas");
	check(backend.fills == 1, "un solo area fill XOR");
	check(backend.fill_planes == 4u && backend.fill_w == 256u && backend.fill_h == 256u,
	      "el area fill cubre los planos y la pantalla pedidos");

	if (failures == 0) {
		std::printf("OK: flat_shade_xor (contorno EOR por plano + un area fill XOR).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
