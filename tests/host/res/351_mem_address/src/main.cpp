// ============================================================================
// Test HOST-351: Address<K> (banco en el tipo) + aritmetica de direccion.
// ============================================================================
//
// Respalda `eng/core/types/memory_kind.hpp`: `Address<MemoryKind::Chip>` y `Address<MemoryKind::Fast>`
// son tipos distintos (el banco va en el TIPO, no en un dato de runtime). La aritmetica de direccion
// conserva el banco: `address + offset -> address`, `address - address -> offset`. Asi el resultado de
// sumar offsets puros se pasa tal cual a una API DMA sin `cast`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/351_mem_address

#include <cstdio>
#include <type_traits>
#include <utility>

#include <eng/core/types/domains.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/memory/mem_bank.hpp>

namespace {

int g_fail = 0;
using Chip = eng::Address<eng::MemoryKind::Chip>;
using Fast = eng::Address<eng::MemoryKind::Fast>;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// API ficticia que exige Chip (frontera DMA): solo acepta `Address<Chip>`.
struct DmaApi {
	/// Exige Chip RAM; con otro banco la llamada no compila.
	static void takes_chip(eng::Address<eng::MemoryKind::Chip>) {}
};
/// Concepto con parametro de plantilla (SFINAE): `DmaApi::takes_chip` con otro banco no compila.
template <class A>
concept DmaOk = requires(A a) { DmaApi::takes_chip(a); };

// El banco va en el TIPO: no se mezclan. Una API DMA acepta Chip y rechaza Fast/Slow.
static_assert(!std::is_same_v<Chip, Fast>, "Address<Chip> y Address<Fast> deben ser tipos distintos");
static_assert(DmaOk<Chip>, "DMA acepta Address<Chip>");
static_assert(!DmaOk<Fast>, "DMA rechaza Address<Fast>");
static_assert(!DmaOk<eng::Address<eng::MemoryKind::Slow>>, "DMA rechaza Address<Slow>");

} // namespace

int main() {
	std::printf("== HOST-351 mem_address ==\n");

	// Aritmetica: address + offset -> address (mismo banco).
	static_assert(std::is_same_v<decltype(std::declval<Chip>() + eng::uintptr {1}), Chip>,
		      "addr + offset conserva el banco");
	// address - address -> offset (uintptr).
	static_assert(std::is_same_v<decltype(std::declval<Chip>() - std::declval<Chip>()), eng::uintptr>,
		      "addr - addr -> offset");

	const Chip base {eng::uintptr {0x1000u}};
	check((base + 0x20u).value == 0x1020u, "base + offset");
	check((base + 4u + 4u).value == 0x1008u, "base + offset encadenado");
	Chip a = base;
	a += 0x40u;
	check(a.value == 0x1040u, "+=");
	a -= 0x10u;
	check(a.value == 0x1030u, "-=");

	const Chip other {base + 0x100u};
	check((other - base) == 0x100u, "addr - addr = offset");
	check(base < other, "orden por direccion");
	check(base != other && !(base == other), "comparacion");

	check(!Chip {}.valid(), "direccion nula no valida");
	check(base.valid(), "direccion no nula valida");

	// Dirección certificada: se obtiene de una fuente Chip (`MemBank<Chip>`), no con un cast.
	static eng::u8 buf[16] {};
	eng::MemBank<eng::MemoryKind::Chip> bank {};
	bank.configure(buf, sizeof(buf), 2u);
	const Chip p = bank.reserve<eng::PlaneTag>(16u, 2u).address();
	check(p.valid() && p.cptr() == buf, "Address certificada (MemBank<Chip>)");
	check((p + 8u - p) == 8u, "tamano por diferencia");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Address<K> (banco en el tipo + aritmetica) validado.\n");
	return 0;
}
