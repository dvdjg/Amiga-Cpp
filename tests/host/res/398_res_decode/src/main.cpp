// ============================================================================
// Test HOST-398: etapa GENÉRICA de decode (`eng::res::decode`)
// ============================================================================
//
// Verifica que la decodificación de un blob comprimido es una etapa de `eng::res` (no de audio):
// `Raw` (copia), `Zx0` (con el mismo vector del compresor de referencia que HOST-271) y un codec
// desconocido (falla). Corresponde a `ROADMAP_RESOURCES.md` R6.5 (ZX0 como etapa genérica).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/res/398_res_decode

#include <cstdio>

#include <eng/res/decode.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Flujo ZX0 de 32 bytes (rampa 0x80..0x8F repetida 2×), del compresor de referencia `zx0` (n=22).
constexpr eng::u8 kZX0[] = {0x00, 0xF5, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
			    0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0xE0, 0xD5, 0x55, 0x60};
constexpr eng::u8 kPCM[] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
			    0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
			    0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F};

void test_raw() {
	eng::u8 dst[4] {};
	const eng::u8 src[4] = {0x11, 0x22, 0x33, 0x44};
	const eng::s32 n = eng::res::decode(eng::res::Codec::Raw,
					    eng::Span<const eng::u8>(src, 4),
					    eng::Span<eng::u8>(dst, 4));
	check(n == 4, "Raw: copia los 4 bytes");
	check(dst[0] == 0x11 && dst[1] == 0x22 && dst[2] == 0x33 && dst[3] == 0x44,
	      "Raw: contenido byte a byte");
}

void test_zx0() {
	eng::u8 out[64] {};
	const eng::s32 n = eng::res::decode(eng::res::Codec::Zx0,
					    eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
					    eng::Span<eng::u8>(out, sizeof(out)));
	check(n == 32, "Zx0: 32 bytes descomprimidos");
	if (n == 32) {
		bool same = true;
		for (int i = 0; i < 32; ++i) {
			if (out[i] != kPCM[i]) same = false;
		}
		check(same, "Zx0: coincide byte a byte con el esperado");
	}
	// Destino pequeño: no cabe → -1.
	eng::u8 small[2] {};
	check(eng::res::decode(eng::res::Codec::Zx0,
			       eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
			       eng::Span<eng::u8>(small, sizeof(small))) == -1,
	      "Zx0: -1 si no cabe en el destino");
}

void test_unknown_codec() {
	eng::u8 out[8] {};
	const eng::s32 n = eng::res::decode(static_cast<eng::res::Codec>(99),
					    eng::Span<const eng::u8>(kZX0, sizeof(kZX0)),
					    eng::Span<eng::u8>(out, sizeof(out)));
	check(n == -1, "codec desconocido → -1");
}

} // namespace

int main() {
	test_raw();
	test_zx0();
	test_unknown_codec();
	if (failures == 0) {
		std::printf("OK: decode genérico (Raw + ZX0 + codec desconocido) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
