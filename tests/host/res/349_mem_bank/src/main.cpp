// ============================================================================
// Test HOST-349: banco de memoria tipado (eng::MemBank) + proteccion compile-time.
// ============================================================================
//
// Respalda `eng/memory/mem_bank.hpp`: `MemBank<Bank>` entrega `TypedBlock<Tag, Bank>` con
// `Address<Bank>`. Una API que exige Chip RAM (DMA) acepta `Address<Chip>` y NO compila si le
// pasas `Address<Fast>` (los `static_assert` lo fijan). El banco se conoce en runtime (tamaños en
// el setup); un banco sin bytes (A500) devuelve bloques invalidos.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/349_mem_bank

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

int g_fail = 0;
constexpr eng::MemoryKind Chip = eng::MemoryKind::Chip;
constexpr eng::MemoryKind Fast = eng::MemoryKind::Fast;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// API que exige memoria Chip (p. ej. punteros de bitplanes): solo acepta Address<Chip>.
void dma_uses(eng::Address<Chip>) {}

// El medio va en el TIPO: Chip encaja, Fast NO (no compila). Proteccion en compilacion.
template <class A>
concept DmaAddress = requires(A a) { dma_uses(a); };
static_assert(DmaAddress<eng::Address<Chip>>, "Address<Chip> es DMA");
static_assert(!DmaAddress<eng::Address<Fast>>, "Address<Fast> no es DMA");

} // namespace

int main() {
	std::printf("== HOST-349 mem_bank ==\n");

	eng::u8 chip_buf[256] {};
	eng::MemBank<Chip> chip;
	chip.configure(chip_buf, sizeof(chip_buf), 16u);

	const auto planes = chip.reserve<eng::PlaneTag>(64u);
	check(planes.valid(), "reserva tipada valida");
	check(planes.size() >= 64u, "tamano de la vista");
	const eng::Address<Chip> addr = planes.address();
	check(addr.valid(), "Address<Chip> valida");
	check(reinterpret_cast<eng::uintptr>(planes.data()) % 16u == 0u, "alineado a 16");
	dma_uses(addr); // solo compila porque es Chip

	check(chip.free_bytes() < chip.capacity(), "hueco tras reservar");
	chip.release(planes);
	check(chip.free_bytes() == chip.capacity(), "release devuelve el bloque");

	// Banco ausente (A500 sin Fast): no hay bytes -> reserva invalida.
	eng::MemBank<Fast> fast;
	check(!fast.reserve<eng::PlaneTag>(16u).valid(), "banco vacio -> invalido");
	check(fast.capacity() == 0u, "sin capacidad");

	// MemoriaManager expone los bancos tipados + la reserva por uso.
	eng::u8 slow_buf[128] {};
	eng::MemoryManager mm {};
	check(mm.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), nullptr, 0u, 16u),
	      "configure");
	const auto tb = mm.chip().reserve<eng::PlaneTag>(32u);
	check(tb.valid() && tb.address().valid(), "MemoryManager::chip().reserve<Tag>");
	const auto tb2 = mm.slow().reserve<eng::PlaneTag>(32u);
	check(tb2.valid(), "MemoryManager::slow().reserve<Tag>");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: MemBank (reserva tipada + Address<Bank> + compile-fail) validado.\n");
	return 0;
}
