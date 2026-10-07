// Test host del sombreado por cara (eng::math::light_ops) y de hi16: la parte del culling
// de lib3d que convierte `normal·vista` en un color 0..15 sin sqrt en runtime.
#include <eng/core/math/light.hpp>
#include <eng/core/math/inv_sqrt.hpp>

#include <cstdio>

using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::math::hi16;
using eng::math::light_ops;
using eng::math::shade_portable;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Tabla determinista de 512 entradas (la real es `eng::math::kInvSqrt`; aquí basta para fijar
// la fórmula). Indexada enmascarando a 511, como el clamp del algoritmo.
struct Tab {
	u16 v[512];
	constexpr Tab() : v {} {
		for (int i = 0; i < 512; ++i) v[i] = static_cast<u16>(60000 - i * 100);
	}
	constexpr u16 operator[](u32 i) const { return v[i & 511u]; }
};
inline constexpr Tab kTab {};

// Referencia independiente de la fórmula, escrita a mano con enteros SIN SIGNO (como el
// `mulu.w` del 68000; si se sign-extensionara daría otro valor).
static s16 ref(s32 v, s32 e1_sq) {
	if (v < 0) v = -v;
	s32 s = static_cast<s32>(static_cast<u32>(e1_sq) >> 16);
	if (s > 511) s = 511;
	const u32 vv = static_cast<u32>(v) >> 16;
	const u32 prod = static_cast<u32>(static_cast<u16>(vv)) * kTab.v[s];
	return static_cast<s16>(prod >> 16);
}

int main() {
	// hi16: parte alta como s16, con truncado de signo (comportamiento del original).
	check(hi16(0x12345678) == 0x1234, "hi16 parte alta");
	check(hi16(-1) == -1, "hi16(-1) = -1 (signo)");
	check(hi16(0x0001FFFF) == 1, "hi16 con parte baja grande");

	// shade == referencia en una rejilla (incluye v<0, vv con bit alto y clamp > 511).
	bool all = true;
	for (s32 v = -0x4000000; v <= 0x4000000; v += 0x00111111) {
		for (s32 e = 0; e < 0x04000000; e += 0x00099999) {
			if (light_ops<>::shade(v, e, kTab) != ref(v, e)) all = false;
			if (shade_portable(v, e, kTab) != ref(v, e)) all = false;
		}
	}
	check(all, "shade (light_ops y portable) == referencia");

	// El clamp a 511 mantiene el índice dentro de la tabla.
	check(light_ops<>::shade(0, 0x7fffffff, kTab) == ref(0, 0x7fffffff), "clamp e1_sq > 511");

	// La tabla `1/sqrt(x)` es genérica en (R, E): dos formatos con el exponente en el tipo.
	{
		using T16 = eng::math::InvSqrtTable<u16, 16>;
		using T12 = eng::math::InvSqrtTable<u16, 12>;
		check(T16::value[1] == 65535 && T16::value[2] == 46340 && T16::value[4] == 32768,
		      "InvSqrtTable<u16,16> (0.16)");
		check(T12::value[1] == 4096 && T12::value[2] == 2896 && T12::value[4] == 2048,
		      "InvSqrtTable<u16,12>");
		check(T16::kExponent == 16 && T12::kExponent == 12, "kExponent en el tipo");
		// Relación tabla·√x ≈ 2^E, con el radical escalado que usa el generador (2^26).
		bool rel = true;
		for (u32 x = 1; x < 64; ++x) {
			const eng::u64 s = eng::math::isqrt_exact(eng::u64{x} << 52u);
			const eng::u64 p = eng::u64{T12::value[x]} * s;
			const eng::u64 t = eng::u64{1} << (12 + 26); // 2^(E+26)
			if (p > t || p + s < t) rel = false;
		}
		check(rel, "tabla(12)*sqrt(x)*2^26 ~ 2^38");
		// La luz normaliza con el exponente que se le pasa (E=12, no 16).
		check(light_ops<>::shade<12>(0x01000000, 0x00040000, T12::value) ==
			      shade_portable<12>(0x01000000, 0x00040000, T12::value),
		      "shade<12>: nativa == portable");
	}

	// El clamp de la magnitud sale del tamaño de la tabla: con 64 entradas recorta a 63.
	{
		const auto& t64 = eng::math::InvSqrtTable<u16, 16, 64>::value;
		check(shade_portable<16>(0x01000000, 0x7fffffff, t64) ==
			      shade_portable<16>(0x01000000, 0x003f0000, t64),
		      "clamp por tamaño de tabla (64 entradas -> indice 63)");
	}

	if (failures == 0) {
		std::printf("OK: light_ops (sombreado por cara) e hi16 validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
