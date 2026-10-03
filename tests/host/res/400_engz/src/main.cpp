// ============================================================================
// Test HOST-400: contenedor `.engz` (cabecera + CRC + decode) — R6.4
// ============================================================================
//
// Verifica `eng::res` `.engz`: construir → parsear → decodificar (con el vector ZX0 de HOST-271),
// CRC del payload, detección de corrupción (`Corrupt`), magic inválido y truncado.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/res/400_engz

#include <cstdio>

#include <eng/res/engz.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Mismo vector ZX0 que HOST-271/398 (rampa 0x80..0x8F repetida 2×).
constexpr eng::u8 kZX0[] = {0x00, 0xF5, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
			    0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0xE0, 0xD5, 0x55, 0x60};
constexpr eng::u8 kPCM[] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
			    0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
			    0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F};

void test_build_parse_decode() {
	eng::u8 blob[128] {};
	const eng::usize n = eng::res::build_engz(eng::Span<eng::u8>(blob, sizeof(blob)),
						  eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
						  eng::res::Codec::Zx0, 32u);
	check(n == eng::res::kEngzHeaderSize + sizeof(kZX0), "build: tamaño = cabecera + payload");

	const auto h = eng::res::parse_engz(eng::Span<const eng::u8>(blob, n));
	check(h.has_value(), "parse: cabecera válida");
	if (h.has_value()) {
		check(h->codec == static_cast<eng::u8>(eng::res::Codec::Zx0), "parse: codec ZX0");
		check(h->uncompressed_size == 32u, "parse: tamaño descomprimido");
	}

	eng::u8 out[64] {};
	const auto d = eng::res::decode_engz(eng::Span<const eng::u8>(blob, n),
					     eng::Span<eng::u8>(out, sizeof(out)));
	check(d.has_value() && *d == 32u, "decode: 32 bytes");
	if (d.has_value() && *d == 32u) {
		bool same = true;
		for (eng::u8 i = 0u; i < 32u; ++i) {
			if (out[i] != kPCM[i]) same = false;
		}
		check(same, "decode: coincide byte a byte");
	}
}

void test_corruption_magic_truncation() {
	eng::u8 blob[128] {};
	const eng::usize n = eng::res::build_engz(eng::Span<eng::u8>(blob, sizeof(blob)),
						  eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
						  eng::res::Codec::Zx0, 32u);
	eng::u8 out[64] {};

	// Corromper el payload → CRC no coincide → Corrupt.
	blob[eng::res::kEngzHeaderSize + 2u] ^= 0xffu;
	const auto bad = eng::res::decode_engz(eng::Span<const eng::u8>(blob, n),
					       eng::Span<eng::u8>(out, sizeof(out)));
	check(!bad.has_value() && bad.error() == eng::Result::Corrupt, "payload corrupto → Corrupt");

	// Reconstruir y corromper el magic → InvalidArgument.
	(void)eng::res::build_engz(eng::Span<eng::u8>(blob, sizeof(blob)),
				   eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
				   eng::res::Codec::Zx0, 32u);
	blob[0] ^= 0xffu;
	const auto badmagic = eng::res::parse_engz(eng::Span<const eng::u8>(blob, n));
	check(!badmagic.has_value() && badmagic.error() == eng::Result::InvalidArgument,
	      "magic inválido → InvalidArgument");

	// Truncado en la cabecera → InvalidArgument.
	const auto trunc = eng::res::parse_engz(eng::Span<const eng::u8>(blob, 8u));
	check(!trunc.has_value(), "cabecera truncada → error");

	// Truncado en el payload (reconstruir y recortar) → InvalidArgument.
	(void)eng::res::build_engz(eng::Span<eng::u8>(blob, sizeof(blob)),
				   eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
				   eng::res::Codec::Zx0, 32u);
	const auto trunc2 = eng::res::parse_engz(
		eng::Span<const eng::u8>(blob, eng::res::kEngzHeaderSize + 1u));
	check(!trunc2.has_value(), "payload truncado → error");
}

} // namespace

int main() {
	test_build_parse_decode();
	test_corruption_magic_truncation();
	if (failures == 0) {
		std::printf("OK: contenedor .engz (cabecera + CRC + decode) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
