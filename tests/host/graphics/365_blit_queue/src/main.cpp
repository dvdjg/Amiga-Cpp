// Test host de eng::graphics::BlitQueue (API de blitter por intencion: cola FIFO + feeder poll).
// El Blitter real se dobla con un FakeBlitter: `blitter_free()` sondea (y "avanza el tiempo" un
// paso por sondeo) y `submit()` programa (queda ocupado unos sondeos). Sin hardware.
#define ENG_SCALAR_RETRO16
#include <eng/graphics/blit_queue.hpp>

#include <cstdio>

using eng::graphics::BlitOp;
using eng::ChipView;

// Test NEGATIVO de tipos (el compilador caza el error, §232): un `ChipView` NO se puede construir
// desde un puntero crudo ni desde una vista agnostica -> pasar pila/Fast al Blitter no compila.
using ChipPlane = eng::ChipView<eng::PlaneTag>;
using PlaneByteView = eng::ByteView<eng::PlaneTag>;

// Test NEGATIVO de tipos (fijo, §232): un `ChipView` (banco Chip en el tipo) NO se puede construir
// desde un puntero crudo, una `Span<u8>` agnostica ni una vista de dominio -> pasar pila/Fast al
// Blitter no compila. Solo nace de una fuente Chip (Block/Bitmap/ChipStorage) o del `from_storage`
// del backend/test.
template <class To, class From>
concept ConstructibleFrom = requires(From f) { To(f); };
static_assert(!ConstructibleFrom<ChipPlane, eng::u8*>);
static_assert(!ConstructibleFrom<ChipPlane, eng::Span<eng::u8>>);
static_assert(!ConstructibleFrom<ChipPlane, PlaneByteView>);

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

struct FakeBlitter {
	static constexpr int kBusyPolls = 3;
	int busy = 0;
	int submitted = 0;
	eng::u8 kinds[16] {};
	eng::u8 last_ashift = 0xff;
	// Sondea BBUSY: cada llamada "pasa un sondeo"; ocupado durante kBusyPolls.
	bool blitter_free() {
		if (busy > 0) {
			--busy;
		}
		return busy == 0;
	}
	void submit(const BlitOp& op) {
		if (submitted < 16) {
			kinds[submitted] = static_cast<eng::u8>(op.kind);
		}
		last_ashift = op.ashift;
		++submitted;
		busy = kBusyPolls;
	}
};

using Q = eng::graphics::BlitQueue<8, FakeBlitter>;

int main() {
	static eng::u8 dst_buf[8] {};
	static eng::u8 src_buf[8] {};
	// En host no hay Chip RAM: para probar la COLA (no el medio) se usa `from_storage`, el escape
	// documentado del test (§232). En objetivo, el `ChipView` viene de un `Block`/`Bitmap` Chip.
	const ChipPlane dst {eng::Address<eng::MemoryKind::Chip>::from_storage(dst_buf), 8};
	const ChipPlane src {eng::Address<eng::MemoryKind::Chip>::from_storage(src_buf), 8};
	FakeBlitter blitter;
	Q q;
	q.bind(blitter);

	// 1) Encolar NO ejecuta: es asincrono.
	q.fill(dst, 16, 768);
	q.stamp(src, dst, 3, 96, 0, 26, 5);
	check(!q.empty() && blitter.submitted == 0, "encolar no ejecuta (asincrono)");

	// 2) pump avanza solo mientras el Blitter esta libre (1 submit y queda ocupado).
	q.pump();
	check(blitter.submitted == 1 && blitter.kinds[0] == static_cast<eng::u8>(BlitOp::Kind::Fill),
	      "pump ejecuta 1 y respeta FIFO (Fill primero)");
	check(blitter.kinds[0] == 0 && blitter.last_ashift == 0, "Fill no toca ashift");

	// 3) El Blitter se libera con el tiempo; pump sigue sacando de la cola.
	while (!q.empty()) {
		q.pump();
	}
	check(q.empty() && blitter.submitted == 2, "pump drena el resto sin esperar");
	check(blitter.kinds[1] == static_cast<eng::u8>(BlitOp::Kind::Stamp) && blitter.last_ashift == 5,
	      "Stamp conserva el ashift");

	// 4) wait() vacia la cola (punto de dependencia) y respeta el orden.
	BlitOp ops[3] {};
	ops[0].kind = BlitOp::Kind::Stamp;
	ops[1].kind = BlitOp::Kind::Fill;
	ops[2].kind = BlitOp::Kind::Stamp;
	q.all(eng::Span<const BlitOp> {ops, 3});
	check(!q.empty() && blitter.submitted == 2, "all() encola en bloque sin ejecutar");
	q.wait();
	check(q.empty() && blitter.submitted == 5, "wait() vacia la cola (3 ejecutadas)");
	check(blitter.kinds[2] == static_cast<eng::u8>(BlitOp::Kind::Stamp) &&
		      blitter.kinds[3] == static_cast<eng::u8>(BlitOp::Kind::Fill) &&
		      blitter.kinds[4] == static_cast<eng::u8>(BlitOp::Kind::Stamp),
	      "FIFO con array de golpe");

	if (failures == 0) {
		std::printf("OK: BlitQueue (cola por intencion + feeder poll + FIFO) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
