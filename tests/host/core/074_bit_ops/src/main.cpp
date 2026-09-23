// ============================================================================
// Test HOST-074: operaciones de bits (eng::util::bit).
// ============================================================================
//
// Respalda `eng/core/util/bit.hpp`. Comprueba el conteo de bits y la aritmética
// de rotación/endian sobre anchos EXACTOS (8/16/32), que es lo que exige el
// 68000 y lo que diferencia a `eng::u32` (4 bytes en m68k/MinGW) de `unsigned
// long` en otros hosts.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/074_bit_ops

#include <cstdio>

#include <eng/core/util/bit.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-074 bit ops ==\n");

	// --- popcount (anchos 8/16/32, con signo) --------------------------------
	check(eu::popcount(static_cast<eng::u8>(0x00u)) == 0, "popcount u8(0) = 0");
	check(eu::popcount(static_cast<eng::u8>(0x0Bu)) == 3, "popcount u8(1011) = 3");
	check(eu::popcount(static_cast<eng::s8>(-1)) == 8, "popcount s8(-1) = 8 (ancho real)");
	check(eu::popcount(static_cast<eng::u16>(0xFF00u)) == 8, "popcount u16(0xFF00) = 8");
	check(eu::popcount(static_cast<eng::u32>(0xFFFFFFFFul)) == 32, "popcount u32(~0) = 32");
	check(eu::popcount(static_cast<eng::s32>(-1)) == 32, "popcount s32(-1) = 32");

	// --- countl_zero / countr_zero -------------------------------------------
	check(eu::countl_zero(static_cast<eng::u32>(0u)) == 32, "countl_zero(0) = ancho");
	check(eu::countl_zero(static_cast<eng::u32>(1u)) == 31, "countl_zero(1) = 31");
	check(eu::countl_zero(static_cast<eng::u32>(0x80000000ul)) == 0, "countl_zero(bit alto) = 0");
	check(eu::countl_zero(static_cast<eng::u16>(0x8000u)) == 0, "countl_zero u16(bit alto) = 0");
	check(eu::countr_zero(static_cast<eng::u32>(0u)) == 32, "countr_zero(0) = ancho");
	check(eu::countr_zero(static_cast<eng::u32>(8u)) == 3, "countr_zero(8) = 3");
	check(eu::countr_zero(static_cast<eng::u8>(0x80u)) == 7, "countr_zero u8(0x80) = 7");

	// --- countl_one / countr_one ---------------------------------------------
	check(eu::countl_one(static_cast<eng::u8>(0xFFu)) == 8, "countl_one u8(0xFF) = 8");
	check(eu::countl_one(static_cast<eng::u8>(0xF0u)) == 4, "countl_one u8(0xF0) = 4");
	check(eu::countr_one(static_cast<eng::u16>(0x000Fu)) == 4, "countr_one u16(0x000F) = 4");

	// --- bit_width / has_single_bit / bit_floor / bit_ceil -------------------
	check(eu::bit_width(static_cast<eng::u32>(0u)) == 0, "bit_width(0) = 0");
	check(eu::bit_width(static_cast<eng::u32>(1u)) == 1, "bit_width(1) = 1");
	check(eu::bit_width(static_cast<eng::u32>(255u)) == 8, "bit_width(255) = 8");
	check(eu::has_single_bit(static_cast<eng::u32>(8u)), "has_single_bit(8)");
	check(!eu::has_single_bit(static_cast<eng::u32>(0u)), "has_single_bit(0) falso");
	check(!eu::has_single_bit(static_cast<eng::u16>(3u)), "has_single_bit(3) falso");
	check(eu::bit_floor(static_cast<eng::u32>(5u)) == 4u, "bit_floor(5) = 4");
	check(eu::bit_floor(static_cast<eng::u32>(0u)) == 0u, "bit_floor(0) = 0");
	check(eu::bit_ceil(static_cast<eng::u32>(5u)) == 8u, "bit_ceil(5) = 8");
	check(eu::bit_ceil(static_cast<eng::u32>(1u)) == 1u, "bit_ceil(1) = 1");
	check(eu::bit_ceil(static_cast<eng::u32>(0u)) == 1u, "bit_ceil(0) = 1");

	// --- rotaciones ----------------------------------------------------------
	check(eu::rotl(static_cast<eng::u8>(0x81u), 1u) == 0x03u, "rotl u8(0x81,1) = 0x03");
	check(eu::rotr(static_cast<eng::u8>(0x03u), 1u) == 0x81u, "rotr u8(0x03,1) = 0x81");
	check(eu::rotl(static_cast<eng::u32>(1u), 0u) == 1u, "rotl por 0 no cambia");
	check(eu::rotl(static_cast<eng::u32>(1u), 32u) == 1u, "rotl por el ancho vuelve al inicio");
	check(eu::rotr(eu::rotl(static_cast<eng::u16>(0x1234u), 5u), 5u) == 0x1234u,
	      "rotl y rotr se deshacen (u16)");

	// --- byteswap y bit_cast -------------------------------------------------
	check(eu::bswap16(static_cast<eng::u16>(0x1234u)) == 0x3412u, "bswap16");
	check(eu::bswap32(0x12345678ul) == 0x78563412ul, "bswap32");
	const float pi = 3.5f;
	const eng::u32 bits = eu::bit_cast<eng::u32>(pi);
	check(eu::bit_cast<float>(bits) == pi, "bit_cast ida y vuelta (u32 <-> float)");

	check(eu::one_bit<eng::u16>() == 1u, "one_bit");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: operaciones de bits validadas.\n");
	return 0;
}
