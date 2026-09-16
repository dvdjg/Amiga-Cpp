#pragma once

/// \file minifloat.hpp
/// Escalar de **coma flotante de 16 bits** (`MiniFloat16`) pensado para el 68000.
///
/// Formato (1 signo | 5 exponente | 10 mantisa, sesgo 15, con bit implícito):
///
/// ```
///   bit 15  14 13 12 11 10   9 .......... 0
///   ┌─────┬──────────────┬─────────────────┐
///   │  S  │   exponente  │    mantisa      │
///   └─────┴──────────────┴─────────────────┘
///     1          5               10           = 16 bits
/// ```
///
/// El valor es `±1.mantisa · 2^(exp−15)`. El campo exponente `0` es **cero** (no hay
/// denormales): por debajo de `2^−15` redondea a cero y en `[2^−15, 2^−14)` redondea al
/// mínimo normal `2^−14`. El campo `31` es **infinito** (no hay NaN: el overflow satura
/// a ∞). El rango finito es `|x| ∈ [2^−14, 65504]` más el cero, con ~10 bits de mantisa
/// (≈ 3 dígitos decimales, error relativo de una operación de ≈ 2^−11 ≈ 4.9·10⁻⁴).
///
/// ¿Por qué existe? En un 68000 **no hay FPU**: el `float` IEEE-754 lo emula
/// `libgcc` con libcalls (`__addsf3`, `__mulsf3`, `__divsf3`) de cientos de ciclos y
/// con la pila de por medio. `MiniFloat16` cabe en un registro de datos (`d0`-`d7`),
/// usa la aritmética 16-bit nativa y su aritmética **no genera ni una llamada a la
/// biblioteca** (verificado en el `.o` de m68k: ni `__mulsi3`, ni `__divsi3`, ni
/// `__mulsf3`):
///
///   - suma/resta: alineado de mantisas por desplazamientos + tabla de 256 bytes para
///     renormalizar (`mf16_clz8`);
///   - producto: `mulu.w` de 16×16→32 (la instrucción más cara del 68000, ~40-70
///     ciclos, pero sin libcall ni división);
///   - división: tabla de recíprocos `1/(1+m)` (1024 entradas) + `mulu.w`, en vez del
///     `divu.w` (~140 ciclos) o del `__divsf3` emulado.
///
/// Es el escalar que la librería genérica de álgebra lineal
/// (`eng/core/linalg.hpp`) puede usar como cualquier otro: `scalar_traits<MiniFloat16>`
/// está especializado ahí, así que `Mat<N, MiniFloat16>` y `Affine<...>` funcionan con
/// el MISMO código que `float` o el fixed `Fixed<...>`. El uso previsto es **geometría
/// 3D/2D** (transformaciones, culling, proyección) donde no hace falta precisión
/// absoluta pero sí coste mínimo; **no** es un reemplazo general de `float` para
/// acumulaciones largas ni para valores por debajo de `2^−14`.
///
/// Restricciones del engine respetadas: `gnu++23`, sin STL, sin excepciones, sin RTTI,
/// sin asignación dinámica; las únicas dependencias son `eng/core/types.hpp` y la
/// utilidad de tablas `eng/core/ct_array.hpp`.
///
/// **Estado de verificación: verificada por demo** — la demo
/// `demos/amiga/083_fbm_noise` construye un mapa de altura con `fbm2<MiniFloat16>` en
/// hardware (build/run/analyze OK) y ejercita la aritmética, `from_int` y las
/// comparaciones. Ampliada por los tests host `tests/host/056_minifloat16` (aritmética
/// y matrices), `057` (matemáticas), `058` (puente con fixed) y `060` (ruido). Las
/// funciones de `minifloat_math.hpp` siguen sin demo propia.

#include <eng/core/arith.hpp>
#include <eng/core/ct_array.hpp>
#include <eng/core/types.hpp>

/// Fuerza el inline de los operadores (en 68000 un `jsr`/`rts` cuesta más que la propia
/// suma de mantisas). Macro local, anulada al final.
#if defined(__GNUC__) || defined(__clang__)
#define ENG_MF16_AI [[gnu::always_inline]]
#else
#define ENG_MF16_AI
#endif

namespace eng::math {

// ============================================================================
//  El tipo
// ============================================================================

/// Coma flotante de 16 bits (1|5|10, sesgo 15). `sizeof(MiniFloat16) == 2`.
///
/// `raw` es la representación empaquetada; se deja pública porque el tipo es
/// freestanding y de coste cero, pero **no** se debe interpretar a mano en la lógica
/// de juego: se opera con los operadores o se convierte a `float` para depurar.
struct MiniFloat16 {
	using raw_type = eng::u16;

	static constexpr raw_type sign_mask = 0x8000u; ///< bit de signo
	static constexpr raw_type exp_mask = 0x7C00u;  ///< campo de exponente (5 bits)
	static constexpr raw_type man_mask = 0x03FFu;  ///< campo de mantisa (10 bits)
	static constexpr int bias = 15;                ///< sesgo del exponente
	static constexpr int exp_inf = 31;             ///< campo reservado para infinito

	raw_type raw = 0;

	constexpr MiniFloat16() = default;
	constexpr explicit MiniFloat16(raw_type r) : raw(r) {}

	/// Construye desde la representación empaquetada (nombrado, para no confundir un
	/// `u16` cualquiera con un valor válido).
	[[nodiscard]] static constexpr MiniFloat16 from_raw(raw_type r) { return MiniFloat16 {r}; }

	/// La conversión a/desde `float` es EXPLÍCITA: solo para constantes y depuración.
	/// En el camino caliente del 68000 no debe aparecer ni un solo `float`.
	constexpr explicit MiniFloat16(float f);
	constexpr explicit operator float() const;

	[[nodiscard]] static constexpr MiniFloat16 zero() { return from_raw(0u); }
	[[nodiscard]] static constexpr MiniFloat16 one() { return from_raw(0x3C00u); }

	[[nodiscard]] constexpr bool is_zero() const { return (raw & 0x7FFFu) == 0u; }
	[[nodiscard]] constexpr bool is_inf() const { return (raw & 0x7FFFu) == exp_mask; }
};

// ============================================================================
//  Detalles: bit_cast, tablas y renormalización
// ============================================================================

namespace detail {

/// Reinterpretación de bits sin STL (`std::bit_cast`). El builtin de GCC/Clang es
/// `constexpr` y no arrastra cabeceras; fuera de ellos se cae a un `union` (no
/// `constexpr`, pero el target del engine es gnu++23).
template <typename To, typename From>
[[nodiscard]] constexpr To mf16_bit_cast(const From& src) {
#if defined(__GNUC__) || defined(__clang__)
	return __builtin_bit_cast(To, src);
#else
	union Pun {
		From f;
		To t;
	};
	Pun u {};
	u.f = src;
	return u.t;
#endif
}

/// `mf16_clz8[i]` = ceros a la izquierda de un byte `i` (8 si `i == 0`). 256 bytes; es
/// la tabla que usa la renormalización. En 68000 no hay `CLZ`, así que un byte de tabla
/// (~10 ciclos) gana al bucle de desplazamientos variable.
inline constexpr eng::ct_array<eng::u8, 256> mf16_clz8 {[](eng::usize i) -> eng::u8 {
	if (i == 0u) return 8u;
	eng::u8 v = static_cast<eng::u8>(i);
	eng::u8 n = 0u;
	while ((v & 0x80u) == 0u) {
		v = static_cast<eng::u8>(v << 1);
		++n;
	}
	return n;
}};

/// `mf16_rcp[m]` = `floor(2^20 / (1024 + m))` ≈ `1/(1+m/1024)` en aritmética de 20 bits
/// de fracción (escala 2^20). Con la mantisa del divisor en `[0x400,0x7FF]` el producto
/// `ma·rcp` queda ya con el bit 20 puesto (o el 19), listo para extraer la mantisa
/// normalizada. 1024 entradas × 2 B = 2 KB de tabla.
inline constexpr eng::ct_array<eng::u16, 1024> mf16_rcp {[](eng::usize i) -> eng::u16 {
	return static_cast<eng::u16>((1u << 20) / (1024u + i));
}};

/// Mínimo normal positivo `2^-14` (campo exponente 1), con el signo dado.
[[nodiscard]] constexpr MiniFloat16 mf16_min_normal(eng::u16 sign) {
	return MiniFloat16::from_raw(static_cast<eng::u16>(sign | (1u << 10)));
}

/// Normaliza la mantisa `m` (con su 1 implícito en algún bit ≤ 10) y empaqueta
/// `(signo, exponente, mantisa)`. Si `m == 0` devuelve el cero; si el exponente se sale
/// por arriba, infinito. Por abajo **no hay denormales**: con `e == 0` el valor ya
/// normalizado es `1.xxx·2^-15`, que redondea al mínimo normal `2^-14`; con `e < 0` cae
/// por debajo de la mitad de `2^-14` y redondea a cero. `m` puede traer un carry en el
/// bit 11 (suma de dos mantisas), que se absorbe incrementando el exponente.
[[nodiscard]] constexpr MiniFloat16 mf16_pack(eng::u16 sign, int e, eng::u16 m) {
	if (m == 0u) return MiniFloat16::from_raw(0u);
	if ((m & 0x0800u) != 0u) { // carry del bit 11 -> una posición a la derecha
		m = static_cast<eng::u16>(m >> 1);
		++e;
	}
	if ((m & 0x0400u) == 0u) { // el bit 10 (1 implícito) aún no está puesto: desplazar
		eng::u8 shift;
		const eng::u16 hi = static_cast<eng::u16>(m >> 3);
		if (hi != 0u) {
			shift = mf16_clz8[hi];
		} else if (m >= 4u) { // m en 4..7: un byte de tabla no distingue los 3 bits bajos
			shift = 8u;
		} else if (m >= 2u) {
			shift = 9u;
		} else {
			shift = 10u;
		}
		m = static_cast<eng::u16>(m << shift);
		e -= static_cast<int>(shift);
	}
	if (e == 0) return mf16_min_normal(sign); // 1.xxx·2^-15 redondea a 2^-14
	if (e < 0) return MiniFloat16::from_raw(sign); // < 2^-15 -> cero (conserva el signo)
	if (e >= MiniFloat16::exp_inf)
		return MiniFloat16 {static_cast<eng::u16>(sign | MiniFloat16::exp_mask)};
	return MiniFloat16 {static_cast<eng::u16>(sign | (static_cast<eng::u16>(e) << 10) |
						  (m & MiniFloat16::man_mask))};
}

/// Posición del bit más alto de `v` (0..31) por tabla de bytes (`mf16_clz8`), sin bucle.
/// Lo usa `mul_add`: sustituye un `while` de hasta 22 iteraciones por 3 comparaciones +
/// 1 lookup (el bucle dominaba el coste de `Mat*Mat`).
[[nodiscard]] constexpr int msb_u32(eng::u32 v) {
	if (v == 0u) return -1;
	if ((v >> 16) != 0u) return 16 + (7 - static_cast<int>(mf16_clz8[(v >> 16) & 0xFFu]));
	if ((v >> 8) != 0u) return 8 + (7 - static_cast<int>(mf16_clz8[(v >> 8) & 0xFFu]));
	return 7 - static_cast<int>(mf16_clz8[v & 0xFFu]);
}

/// Clave de orden TOTAL para la representación signo-magnitud: sin esto, comparar los/// `raw` como enteros con signo daría un orden incorrecto para negativos (el signo está
/// en el bit alto, no en complemento a 2). `-0` y `+0` colapsan al mismo valor.
[[nodiscard]] constexpr eng::s16 mf16_order_key(eng::u16 raw) {
	const eng::u16 mag = static_cast<eng::u16>(raw & 0x7FFFu);
	return (raw & MiniFloat16::sign_mask) != 0u ? static_cast<eng::s16>(-static_cast<eng::s16>(mag))
						    : static_cast<eng::s16>(mag);
}

} // namespace detail

// ============================================================================
//  Conversiones
// ============================================================================

constexpr MiniFloat16::MiniFloat16(float f) {
	const eng::u32 bits = detail::mf16_bit_cast<eng::u32>(f);
	const eng::u32 fexp = (bits >> 23) & 0xFFu;
	const eng::u32 man = bits & 0x7FFFFFu;
	const eng::u16 s = static_cast<eng::u16>((bits >> 16) & sign_mask);

	if (fexp == 0xFFu) { // ±inf o NaN del float -> infinito
		raw = static_cast<raw_type>(s | exp_mask);
		return;
	}
	if (fexp == 0u && man == 0u) { // ±0
		raw = 0;
		return;
	}

	int e = static_cast<int>(fexp) - 127 + bias;
	if (e < 0) { // por debajo de la mitad de 2^-14 (incluye subnormales del float)
		raw = 0;
		return;
	}
	if (e == 0) { // en [2^-15, 2^-14): redondea al mínimo normal (como la aritmética)
		raw = static_cast<raw_type>(s | (1u << 10));
		return;
	}
	if (e >= exp_inf) {
		raw = static_cast<raw_type>(s | exp_mask);
		return;
	}

	// Redondeo al más cercano del bit 13 (23 -> 10 bits de mantisa): +0x1000 = medio ulp.
	eng::u32 m = (man + 0x1000u) >> 13;
	if ((m & 0x400u) != 0u) { // el redondeo desbordó la mantisa: exponente++ y mantisa 0
		m = 0u;
		++e;
	}
	if (e >= exp_inf) {
		raw = static_cast<raw_type>(s | exp_mask);
		return;
	}
	raw = static_cast<raw_type>(s | (static_cast<raw_type>(e) << 10) | static_cast<raw_type>(m));
}

constexpr MiniFloat16::operator float() const {
	const raw_type mag = static_cast<raw_type>(raw & 0x7FFFu);
	if (mag == 0u) return 0.0f;
	const eng::u32 s = (raw & sign_mask) != 0u ? 0x80000000u : 0u;
	if (mag == exp_mask)
		return detail::mf16_bit_cast<float>(s | 0x7F800000u); // infinito
	const eng::u32 fexp = static_cast<eng::u32>((static_cast<int>((raw >> 10) & 31) - bias) + 127);
	const eng::u32 fman = static_cast<eng::u32>(raw & man_mask) << 13;
	return detail::mf16_bit_cast<float>(s | (fexp << 23) | fman);
}

// ============================================================================
//  Aritmética
// ============================================================================

/// Suma/resta. Alinea la mantisa menor desplazándola por la diferencia de exponentes y
/// renormaliza una sola vez. El caso `de >= 11` sale antes: el operando pequeño queda
/// por debajo del ulp y no cambia el resultado (truncado, sin redondeo).
[[nodiscard]] ENG_MF16_AI constexpr MiniFloat16 operator+(MiniFloat16 a, MiniFloat16 b) {
	const eng::u16 az = static_cast<eng::u16>(a.raw & 0x7FFFu);
	const eng::u16 bz = static_cast<eng::u16>(b.raw & 0x7FFFu);
	if (az == 0u) return bz == 0u ? MiniFloat16::from_raw(0u) : MiniFloat16 {b.raw};
	if (bz == 0u) return a;

	eng::u16 sa = static_cast<eng::u16>(a.raw & MiniFloat16::sign_mask);
	eng::u16 sb = static_cast<eng::u16>(b.raw & MiniFloat16::sign_mask);
	int ea = static_cast<int>((a.raw >> 10) & 31);
	int eb = static_cast<int>((b.raw >> 10) & 31);
	eng::u16 ma = static_cast<eng::u16>(0x400u | (a.raw & MiniFloat16::man_mask));
	eng::u16 mb = static_cast<eng::u16>(0x400u | (b.raw & MiniFloat16::man_mask));

	// Deja |a| >= |b| (mayor exponente, o mayor mantisa a igual exponente).
	if (ea < eb || (ea == eb && ma < mb)) {
		eng::u16 ts = sa;
		sa = sb;
		sb = ts;
		int te = ea;
		ea = eb;
		eb = te;
		eng::u16 tm = ma;
		ma = mb;
		mb = tm;
	}

	const int de = ea - eb;
	if (de >= 11) // el sumando pequeño no alcanza el ulp del grande
		return MiniFloat16 {static_cast<eng::u16>(sa | (static_cast<eng::u16>(ea) << 10) |
							  (ma & MiniFloat16::man_mask))};

	const eng::u16 al = static_cast<eng::u16>(mb >> de);
	int e = ea;
	eng::u16 m;
	if (sa == sb) {
		m = static_cast<eng::u16>(ma + al);
	} else {
		m = static_cast<eng::u16>(ma - al); // |a| >= |b| garantiza m >= 0
	}
	return detail::mf16_pack(sa, e, m);
}

/// Resta: `a - b == a + (-b)`. El signo se cambia con un XOR (el cero se trata aparte
/// para no generar `-0`).
[[nodiscard]] ENG_MF16_AI constexpr MiniFloat16 operator-(MiniFloat16 a, MiniFloat16 b) {
	return a + MiniFloat16 {static_cast<eng::u16>(b.raw ^ MiniFloat16::sign_mask)};
}

/// Negación. `-0` se canonicaliza a `+0` para que el signo no se propague gratis.
[[nodiscard]] ENG_MF16_AI constexpr MiniFloat16 operator-(MiniFloat16 a) {
	return MiniFloat16 {static_cast<eng::u16>(a.is_zero() ? 0u : (a.raw ^ MiniFloat16::sign_mask))};
}

/// Producto: los exponentes SUMAN (sesgo descontado) y las mantisas se multiplican con
/// `mulu.w` (11×11 -> 22 bits). El bit 21 indica que el resultado llegó a `[2,4)` y hay
/// que desplazar; el `+0x200` redondea al más cercano en el bit 10.
[[nodiscard]] ENG_MF16_AI constexpr MiniFloat16 operator*(MiniFloat16 a, MiniFloat16 b) {
	const eng::u16 az = static_cast<eng::u16>(a.raw & 0x7FFFu);
	const eng::u16 bz = static_cast<eng::u16>(b.raw & 0x7FFFu);
	const eng::u16 sign = static_cast<eng::u16>((a.raw ^ b.raw) & MiniFloat16::sign_mask);
	if (az == 0u || bz == 0u) return MiniFloat16 {sign};
	if (az >= MiniFloat16::exp_mask || bz >= MiniFloat16::exp_mask)
		return MiniFloat16 {static_cast<eng::u16>(sign | MiniFloat16::exp_mask)};

	int e = static_cast<int>((a.raw >> 10) & 31) + static_cast<int>((b.raw >> 10) & 31) -
		MiniFloat16::bias;
	const eng::u16 ma = static_cast<eng::u16>(0x400u | (a.raw & MiniFloat16::man_mask));
	const eng::u16 mb = static_cast<eng::u16>(0x400u | (b.raw & MiniFloat16::man_mask));
	eng::u32 prod = static_cast<eng::u32>(ma) * static_cast<eng::u32>(mb);

	if ((prod & 0x200000u) != 0u) { // bit 21: el producto está en [2,4)
		prod >>= 1;
		++e;
	}
	prod += 0x200u; // redondeo al más cercano
	if ((prod & 0x200000u) != 0u) {
		prod >>= 1;
		++e;
	}
	if (e >= MiniFloat16::exp_inf)
		return MiniFloat16 {static_cast<eng::u16>(sign | MiniFloat16::exp_mask)};
	if (e == 0) return detail::mf16_min_normal(sign);
	if (e < 0) return MiniFloat16 {sign};
	return MiniFloat16 {static_cast<eng::u16>(sign | (static_cast<eng::u16>(e) << 10) |
						  static_cast<eng::u16>((prod >> 10) & MiniFloat16::man_mask))};
}

/// División: exponentes RESTAN (más sesgo) y la mantisa se multiplica por el recíproco
/// `mf16_rcp` del divisor, evitando el `divu.w`. El producto cae en `[2^19, 2^21)`, así
/// que se normaliza a bit 20 y se redondea.
[[nodiscard]] ENG_MF16_AI constexpr MiniFloat16 operator/(MiniFloat16 a, MiniFloat16 b) {
	const eng::u16 az = static_cast<eng::u16>(a.raw & 0x7FFFu);
	const eng::u16 bz = static_cast<eng::u16>(b.raw & 0x7FFFu);
	const eng::u16 sign = static_cast<eng::u16>((a.raw ^ b.raw) & MiniFloat16::sign_mask);
	if (bz == 0u) // x/0 -> infinito
		return MiniFloat16 {static_cast<eng::u16>(sign | MiniFloat16::exp_mask)};
	if (az == 0u) return MiniFloat16 {sign};
	if (az >= MiniFloat16::exp_mask) // inf/x e inf/inf -> infinito
		return MiniFloat16 {static_cast<eng::u16>(sign | MiniFloat16::exp_mask)};
	if (bz >= MiniFloat16::exp_mask) return MiniFloat16 {sign}; // x/inf -> cero

	int e = static_cast<int>((a.raw >> 10) & 31) - static_cast<int>((b.raw >> 10) & 31) +
		MiniFloat16::bias;
	const eng::u16 ma = static_cast<eng::u16>(0x400u | (a.raw & MiniFloat16::man_mask));
	const eng::u16 rcp = detail::mf16_rcp[b.raw & MiniFloat16::man_mask];
	eng::u32 prod = static_cast<eng::u32>(ma) * static_cast<eng::u32>(rcp);

	if ((prod & 0x200000u) != 0u) {
		prod >>= 1;
		++e;
	} else if ((prod & 0x100000u) == 0u) { // cociente en [0.5,1): sube un bit
		prod <<= 1;
		--e;
	}
	prod += 0x200u;
	if ((prod & 0x200000u) != 0u) {
		prod >>= 1;
		++e;
	}
	if (e >= MiniFloat16::exp_inf)
		return MiniFloat16 {static_cast<eng::u16>(sign | MiniFloat16::exp_mask)};
	if (e == 0) return detail::mf16_min_normal(sign);
	if (e < 0) return MiniFloat16 {sign};
	return MiniFloat16 {static_cast<eng::u16>(sign | (static_cast<eng::u16>(e) << 10) |
						  static_cast<eng::u16>((prod >> 10) & MiniFloat16::man_mask))};
}

// ============================================================================
//  Asignaciones compuestas y comparaciones
// ============================================================================

constexpr MiniFloat16& operator+=(MiniFloat16& a, MiniFloat16 b) { return a = a + b; }
constexpr MiniFloat16& operator-=(MiniFloat16& a, MiniFloat16 b) { return a = a - b; }
constexpr MiniFloat16& operator*=(MiniFloat16& a, MiniFloat16 b) { return a = a * b; }
constexpr MiniFloat16& operator/=(MiniFloat16& a, MiniFloat16 b) { return a = a / b; }

[[nodiscard]] ENG_MF16_AI constexpr bool operator==(MiniFloat16 a, MiniFloat16 b) {
	return detail::mf16_order_key(a.raw) == detail::mf16_order_key(b.raw);
}
[[nodiscard]] ENG_MF16_AI constexpr bool operator!=(MiniFloat16 a, MiniFloat16 b) { return !(a == b); }
[[nodiscard]] ENG_MF16_AI constexpr bool operator<(MiniFloat16 a, MiniFloat16 b) {
	return detail::mf16_order_key(a.raw) < detail::mf16_order_key(b.raw);
}
[[nodiscard]] ENG_MF16_AI constexpr bool operator<=(MiniFloat16 a, MiniFloat16 b) { return !(b < a); }
[[nodiscard]] ENG_MF16_AI constexpr bool operator>(MiniFloat16 a, MiniFloat16 b) { return b < a; }
[[nodiscard]] ENG_MF16_AI constexpr bool operator>=(MiniFloat16 a, MiniFloat16 b) { return !(a < b); }

/// `a·b + c` con **un solo redondeo** (FMA): más preciso que `a*b + c` (dos redondeos)
/// en series, `dot` y transformaciones. El producto se acumula exacto (22 bits) junto con
/// `c` alineado en notación científica y se normaliza una vez. En 68000 usa `muls.w`
/// (producto de 16×16→32) y solo un bucle corto de normalización.
[[nodiscard]] ENG_MF16_AI constexpr MiniFloat16 mul_add(MiniFloat16 a, MiniFloat16 b, MiniFloat16 c) {
	using raw_type = MiniFloat16::raw_type;
	const raw_type az = static_cast<raw_type>(a.raw & 0x7FFFu);
	const raw_type bz = static_cast<raw_type>(b.raw & 0x7FFFu);
	const raw_type cz = static_cast<raw_type>(c.raw & 0x7FFFu);
	if (az == 0u || bz == 0u) return c; // 0 + c
	if (cz == 0u) return a * b;
	if (az >= MiniFloat16::exp_mask || bz >= MiniFloat16::exp_mask) // producto infinito
		return MiniFloat16::from_raw(
			static_cast<raw_type>(((a.raw ^ b.raw) & MiniFloat16::sign_mask) | MiniFloat16::exp_mask));
	if (cz >= MiniFloat16::exp_mask) return c; // c infinito
	const int ea = static_cast<int>((a.raw >> 10) & 31);
	const int eb = static_cast<int>((b.raw >> 10) & 31);
	const int ec = static_cast<int>((c.raw >> 10) & 31);
	const eng::s32 A = static_cast<eng::s32>(0x400u | (a.raw & MiniFloat16::man_mask));
	const eng::s32 B = static_cast<eng::s32>(0x400u | (b.raw & MiniFloat16::man_mask));
	const eng::s32 C = static_cast<eng::s32>(0x400u | (c.raw & MiniFloat16::man_mask));
	const eng::s32 P = arith<eng::s16>::mul(static_cast<eng::s16>(A), static_cast<eng::s16>(B)); // muls.w
	const bool nsp = ((a.raw ^ b.raw) & MiniFloat16::sign_mask) != 0u;
	const bool nsc = (c.raw & MiniFloat16::sign_mask) != 0u;
	const int kp = ea + eb - 50; // valor del producto = P · 2^kp
	const int kc = ec - 25;      // valor de c        = C · 2^kc
	const int k = kp > kc ? kp : kc;
	// Desplazamiento con redondeo al más cercano (medio hacia arriba).
	auto rshr = [](eng::s32 v, int s) {
		return s <= 0 ? v : static_cast<eng::s32>((v + (1 << (s - 1))) >> s);
	};
	const eng::s32 tp = rshr(P, k - kp);
	const eng::s32 tc = rshr(C, k - kc);
	const eng::s32 S = (nsp ? -tp : tp) + (nsc ? -tc : tc); // valor = S · 2^k
	if (S == 0) return MiniFloat16::zero();
	const bool neg = S < 0;
	eng::u32 mag = neg ? (0u - static_cast<eng::u32>(S)) : static_cast<eng::u32>(S);
	const int msb = detail::msb_u32(mag);
	int kk = k;
	const int shift = 10 - msb; // normaliza a msb = bit 10
	if (shift >= 0) {
		mag <<= shift;
		kk -= shift;
	} else {
		const int rs = -shift;
		mag += 1u << (rs - 1); // redondeo al más cercano
		mag >>= rs;
		if ((mag & 0x800u) != 0u) { // el redondeo desbordó el bit 10
			mag >>= 1;
			kk += 1;
		}
		kk += rs;
	}
	const int e = kk + 25;
	const raw_type s = static_cast<raw_type>(neg ? MiniFloat16::sign_mask : 0u);
	if (e <= 0) return MiniFloat16::from_raw(static_cast<raw_type>(e == 0 ? (s | (1u << 10)) : s));
	if (e >= MiniFloat16::exp_inf)
		return MiniFloat16::from_raw(static_cast<raw_type>(s | MiniFloat16::exp_mask));
	return MiniFloat16::from_raw(
		static_cast<raw_type>(s | (static_cast<raw_type>(e) << 10) | (mag & MiniFloat16::man_mask)));
}

/// `acc += a·b` (multiply-accumulate) con un solo redondeo. Alias de `mul_add`.
[[nodiscard]] ENG_MF16_AI constexpr MiniFloat16 mac(MiniFloat16 a, MiniFloat16 b, MiniFloat16 acc) {
	return mul_add(a, b, acc);
}

} // namespace eng::math

#undef ENG_MF16_AI
