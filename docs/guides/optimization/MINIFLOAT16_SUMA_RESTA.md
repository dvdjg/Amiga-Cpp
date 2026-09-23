# `MiniFloat16`: optimización de suma/resta (referencia de investigación)

Documento de **referencia**: recoge el modelo de coste del `+`/`-` de `MiniFloat16` y las
técnicas candidatas para acelerarlo en 68000. **No describe una implementación**: señala
qué está ya en el código y qué queda por investigar, para no reabrir el análisis desde
cero. El tipo y su contrato están en
[MINIFLOAT16.md](../../engine/architecture/MINIFLOAT16.md); el código en
`engine/include/eng/core/math/minifloat.hpp`.

## 1. Modelo del `+` / `-`

La suma en coma flotante binaria no es conmutativa en coste: hay que **alinear** la mantisa
del operando menor con el mayor y luego **renormalizar**. El `-` se resuelve como
`a + (-b)` (un XOR del bit de signo).

```
   a = sa · ma · 2^(ea-15)        b = sb · mb · 2^(eb-15)
   ────────────────────────────────────────────────────────────
   1. extraer signo/exponente/mantisa (con el 1 implícito)
   2. dejar |a| >= |b|  (swap por exponente, y por mantisa a igual exponente)
   3. ALINEAR:  mb >>= (ea - eb)              ← desplazamiento de n variable
   4. sumar o restar mantisas según el signo
   5. RENORMALIZAR: llevar el 1 implícito al bit 10 y ajustar el exponente
   6. empaquetar signo/exponente/mantisa (con saturación a 0 o a ∞)
```

El punto caro en 68000 es el **5**: no hay instrucción de «primer bit a 1» (`BFFFO` es
68020+), así que un bucle de renormalización cuesta iteraciones impredecibles. El punto
**3** también pesa: un `lsr.w #n,dX` con `n` variable cuesta **6 + 2·n** ciclos.

## 2. Qué ya está implementado

El `operator+` actual (`minifloat.hpp`) ya aplica las dos técnicas de mayor ganancia:

| Técnica | Dónde | Efecto |
|---|---|---|
| **Early-out de ceros** | `operator+`, primeras líneas | `x + 0` no toca el resto |
| **Early-out por exponente** | `de >= 11` | el sumando pequeño queda bajo el ulp: devuelve el mayor sin alinear |
| **Orden canónico** | swap a `|a| >= |b|` | garantiza mantisa no negativa en la resta |
| **Renormalización por tabla** | `mf16_pack` con `mf16_clz8` (256 B) | sustituye el bucle por 1–3 lookups + desplazamiento |

`mf16_pack` absorbe además el *carry* del bit 11 (suma de dos mantisas), los casos
`m < 8` (donde un byte de tabla no distingue los 3 bits bajos), el redondeo a `2^-14`
cuando `e == 0`, el *underflow* a cero y la saturación a ∞.

**Conclusión de la investigación previa:** la «tabla de leading-one» y el *early-out* ya
están; no son trabajo pendiente.

## 3. Margen restante (candidatas)

| Técnica | Ganancia esperada | Memoria | Dificultad | Estado |
|---|---|---|---|---|
| Early-out por exponente | Alta | 0 | Baja | **ya implementada** |
| Renormalización por tabla (`mf16_clz8`) | Muy alta | 256 B | Media | **ya implementada** |
| Camino rápido de exponentes iguales (`de == 0`) | Baja–media | 0 | Baja | candidata |
| Tabla de máscaras de alineación | Media | 16–32 B | Baja | candidata |
| Tabla completa de suma de mantisas | Nula | Enorme | — | descartada |

Las dos candidatas atacan el punto **3** (el desplazamiento de alineación), no la
renormalización. Su interés depende del perfil real de los bucles calientes: si dominan
las sumas de **magnitudes similares** (`de == 0` o pequeño), el camino rápido paga; si
dominán diferencias grandes, el *early-out* ya las descarta antes de alinear.

## 4. Detalle de las candidatas

### 4.1 Camino rápido `de == 0`

Cuando los exponentes coinciden no hace falta alinear (`mb >> 0` es un no-op, pero sigue
emitiéndose un `lsr`). Un ramal explícito lo evita y, en la resta, permite renormalizar
directamente:

```cpp
// Ilustrativo: mismas extracciones que el operator+ actual.
if (de == 0) {
    if (sa == sb) {                       // suma: mantisas de 11 bits, carry en bit 11
        eng::u16 m = static_cast<eng::u16>(ma + mb);
        return detail::mf16_pack(sa, ea, m);   // mf16_pack absorbe el carry
    }
    eng::u16 m = static_cast<eng::u16>(ma - mb); // ma >= mb garantizado por el swap
    return detail::mf16_pack(sa, ea, m);         // renormaliza con mf16_clz8
}
```

No cambia la semántica (es el mismo camino que hoy, sin el desplazamiento por cero). La
ganancia es pequeña pero el coste es nulo; merece medirse antes de adoptarlo.

### 4.2 Tabla de máscaras de alineación

`mb >> de` con `de` variable cuesta `6 + 2·de`. Para `de` grande (cerca de 10) son ~26
ciclos. Una tabla de máscaras permite acotar el patrón, aunque en 68000 el ahorro es
modesto y solo compensa si el perfil muestra muchos `de` altos que hoy no salen por el
*early-out* (`de >= 11`):

```cpp
// align_mask[de] = (1u << (11 - de)) - 1  (de en 0..10)
inline constexpr eng::ct_array<eng::u16, 11> mf16_align_mask {[](eng::usize d) -> eng::u16 {
    return static_cast<eng::u16>((1u << (11u - d)) - 1u);
}};
// mb = (mb >> de) & mf16_align_mask[de];
```

La utilidad real depende de la distribución de `de`; conviene medirla antes de añadir la
tabla (16–22 B de datos constantes).

### 4.3 Nota sobre el coste del desplazamiento

En 68000 no hay *barrel shifter*; `lsr.w #n,dX` (n en registro) es `6 + 2·n`. Alternativas
a evaluar por caso: *unroll* de los `de` pequeños más frecuentes (0–3), o la máscara de
4.2. No conviene micro-optimizar sin datos de perfil.

## 5. Cómo medir

- **Perfil de operandos**: instrumentar temporalmente un contador de `de` por rango
  (`0`, `1–3`, `4–7`, `8–10`) en el bucle que se quiera acelerar.
- **Codegen**: `tools/analyze/codegen-report.mjs` (probes `c_mf_*`) y
  `tools/analyze/expr-asm-compare.mjs` cuentan instrucciones, escrituras a pila y
  `libcalls`; sirven para confirmar que una variante no introduce `__mulsi3`/`__divsi3`.
- **Correctitud**: `tests/host/core/056_minifloat16` y `058_minifloat_fixed` fijan el
  comportamiento (bordes, `∞`, redondeo); cualquier cambio debe seguir pasándolos.

## 6. Prioridad

1. **Medir la distribución de `de`** en el bucle objetivo (sin datos, cualquier cambio es
   a ciegas).
2. **Camino rápido `de == 0`** si dominan las magnitudes similares (coste 0, riesgo bajo).
3. **Máscara de alineación** si el perfil muestra `de` altos frecuentes.
4. Recordatorio: en un 68000, el mayor ahorro suele ser **usar `Fixed` en vez de
   `MiniFloat16`** donde el rango lo permita; `MiniFloat16` es para magnitudes de escalas
   muy distintas, no para bucles de acumulación (ver §5 de
   [MINIFLOAT16.md](../../engine/architecture/MINIFLOAT16.md)).
