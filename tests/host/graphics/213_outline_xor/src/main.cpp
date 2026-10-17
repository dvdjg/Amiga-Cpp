// Test host de `eng::retro::flat_shade_xor` (contorno EOR + area fill XOR, técnica Amiga) y
// de `eng::retro::wire_lines` (alambre en modo LINE con clip opcional): valida la SECUENCIA
// de Blitter sobre un backend mock (sin hardware): comunes fijados una vez, horizontales
// descartadas (EOR) o dibujadas (LINE), una línea por plano del color, un solo area fill y
// el recorte de librería (`clip_line`) cuando se pasa clip.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/213_outline_xor

#include <cstdio>

#include <eng/retro/flat_shade_xor.hpp>
#include <eng/retro/wireframe.hpp>

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
	int waits = 0;
	u16 fill_planes = 0;
	u16 fill_w = 0;
	u16 fill_h = 0;
	bool fill_wait = true;
	s16 last_x0 = 0, last_y0 = 0, last_x1 = 0, last_y1 = 0;

	void blitter_lines_begin(u16) { ++begins; }
	bool blitter_line_eor_prepare(LineEorParams& out, u16, s16 x0, s16 y0, s16 x1, s16 y1) {
		out.dummy = 1;
		++prepares;
		last_x0 = x0; last_y0 = y0; last_x1 = x1; last_y1 = y1;
		return true;
	}
	bool blitter_line_prepare(LineEorParams& out, u16, s16 x0, s16 y0, s16 x1, s16 y1) {
		out.dummy = 1;
		++prepares;
		last_x0 = x0; last_y0 = y0; last_x1 = x1; last_y1 = y1;
		return true;
	}
	void blitter_line_draw(const LineEorParams&, u8*, u8*) { ++draws; }
	bool blitter_area_fill(eng::PlaneBytes, u8 planes, u16, u32, u16 w, u16 h, bool wait) {
		++fills;
		fill_planes = planes;
		fill_w = w;
		fill_h = h;
		fill_wait = wait;
		return true;
	}
	bool blitter_area_fill_rect(eng::PlaneBytes, u16, u16 wx0, s16 y0, u16 words, u16 rows,
				    bool wait) {
		++rect_fills;
		rect_wx0 = wx0;
		rect_y0 = y0;
		rect_words = words;
		rect_rows = rows;
		fill_wait = wait;
		return true;
	}
	int rect_fills = 0;
	u16 rect_wx0 = 0;
	s16 rect_y0 = 0;
	u16 rect_words = 0;
	u16 rect_rows = 0;
	bool wait_blitter() { ++waits; return true; }
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
	check(backend.fill_wait, "el area fill espera por defecto");

	// `area_fill_wait = false`: el fill se encola (para solapar con la CPU).
	(void)flat_shade_xor(backend, eng::PlaneBytes {}, 40u, 8192u, 4u, 256u, 256u,
			     eng::Span<const OutlineEdge> {edges, 3u}, /*area_fill_wait=*/false);
	check(!backend.fill_wait, "area_fill_wait=false se propaga al backend");

	// --- wire_lines: sin clip (horizontales DIBUJADAS) y con clip de librería ---
	backend = MockBackend {};
	const OutlineEdge wire[4] = {
		{10, 10, 50, 40, 0x01},   // diagonal, plano 0
		{20, 60, 80, 60, 0x02},   // HORIZONTAL: en alambre SI se dibuja
		{30, 90, 30, 20, 0x03},   // vertical en planos 0 y 1
		{100, 100, 140, 140, 0x01}, // FUERA del clip de la prueba
	};
	const u32 wl = wire_lines(backend, eng::PlaneBytes {}, 40u, 8192u, 4u,
				  eng::Span<const OutlineEdge> {wire, 4u});
	check(backend.begins == 1, "wire_lines fija los comunes UNA vez");
	check(backend.prepares == 4, "wire_lines NO descarta las horizontales");
	check(backend.draws == 5, "una linea por plano del color (1 + 1 + 2 + 1)");
	check(wl == 5u && backend.waits == 1, "wire_lines devuelve lineas y espera al final");

	// Con clip: la arista de fuera se descarta y las parciales entran recortadas.
	backend = MockBackend {};
	const Rect win = rect(0, 0, 60, 70); // [0,60) x [0,70)
	const u32 wc = wire_lines(backend, eng::PlaneBytes {}, 40u, 8192u, 4u,
				  eng::Span<const OutlineEdge> {wire, 4u}, /*wait=*/true, &win);
	check(backend.prepares == 3, "con clip, la arista de fuera se descarta (3 prepares)");
	check(wc == 4u && backend.draws == 4, "con clip quedan 4 lineas (1+1+2)");
	check(backend.last_x0 == 30 && backend.last_x1 == 30 && backend.last_y1 == 20 &&
		      backend.last_y0 >= 68 && backend.last_y0 <= 70,
	      "la vertical llega recortada al borde del clip (ultimo prepare)");

	// Clip que deja fuera TODAS las aristas: ningun draw, pero el preludio se fija.
	backend = MockBackend {};
	const Rect far = rect(1000, 1000, 1100, 1100);
	const u32 wf = wire_lines(backend, eng::PlaneBytes {}, 40u, 8192u, 4u,
				  eng::Span<const OutlineEdge> {wire, 4u}, /*wait=*/true, &far);
	check(wf == 0u && backend.draws == 0, "clip sin intersecciones: 0 lineas");
	check(backend.waits == 1, "wire_lines espera al Blitter aunque no haya lineas");

	if (failures == 0) {
		std::printf("OK: flat_shade_xor (EOR + fill) y wire_lines (LINE con clip) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
