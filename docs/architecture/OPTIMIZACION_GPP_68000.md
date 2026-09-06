# Optimización C++ para 68000 con amiga-gcc (g++ 15.x)

Documento vivo de recomendaciones para escribir C++23 eficiente orientado a 68000 (Amiga 500). El backend de GCC para m68k ha mejorado mucho con el trabajo reciente (PeyloW: cost models, dbra, autoincrement, peepholes), pero sigue siendo limitado comparado con backends modernos: pocos registros, sin caché de instrucciones útil, bus de 16 bits, operaciones de 32 bits más caras. El objetivo es **código pequeño + uso inteligente de registros + evitar runtime de la librería**, no "máxima agresividad de optimización".

Convención de marcas (es un doc que se completa con descubrimientos):
- `[✓]` afirmación **verificada** contra el toolchain de este repo (fecha y comando abajo).
- `[✗]` afirmación **corregida** porque el toolchain demostró lo contrario.
- `[P]` pendiente de verificación (anotar el resultado cuando se pruebe).

---

## 0. Entorno de verificación (evidencia reproducible)

```
Toolchain   : m68k-amiga-elf-g++ (GCC) 15.1.0 — fork amiga-gcc/bebbo (ELF, freestanding)
Ruta (VS Code): <ext>\bin\win32\opt\bin\m68k-amiga-elf-g++.exe
Compilado   : -m68000 -std=gnu++23 [-O2 | -Os] -fomit-frame-pointer -fno-exceptions -fno-rtti
Probe       : docs/performance/_probe_gpp68000.cpp (o fragmentos en engine/docs)
Verificado  : 2026-08-31
```

La comprobación rápida de cualquier patrón caliente: `m68k-amiga-elf-g++ -m68000 -O2 -S fichero.cpp` y leer el `.s` (o `-fverbose-asm`). El fork sigue generando cosas subóptimas que se detectan a ojo.

---

## 1. Flags de compilación (base)

```bash
-m68000 -Os -fomit-frame-pointer -fno-exceptions -fno-rtti \
-ffunction-sections -fdata-sections --gc-sections (en el link)
-flto (P: aceptado por el driver; validar el link completo en build-demo.sh)
```

- `[✓/✗]` `-mtune=68020` **sí** se acepta (y `68030/68040/68060`), pero **en este repo a `-O1` cuelga la init de la demo 107** (ver bitácora §8). No usar `-mtune` de momento; si interesase, investigar con UN solo origen optimizado (override `ENGINE_OPT`/`DEMO_OPT`, ver `build-demo.sh`).
- `[✗]` `-mcpu=68020-60` **no existe** en este fork: `-mcpu=` solo admite `51..` `5202..`, `68000..68060`, `cpu32`, `fidoa`. No usar `-mtune=68020-60` (ese valor no es válido ni para `-mcpu` ni se recomienda).
- `[✗]` `-O2 -Os` juntos son contradictorios: en GCC gana el ÚLTIMO. Decidir uno: `-Os` (código más compacto, y el único que fuerza `dbra` en bucles countdown, ver §4) o `-O2` (más agresivo, a veces transforma el bucle a límite de puntero en vez de `dbra`).
- `[✓]` `-fomit-frame-pointer`: libera A6. Con `-mshort` todo cambia el ABI; solo si se controla todo el código.
- `[✓]` El fork es freestanding: **no hay libstdc++** (`<cstdint>` no existe). Los tipos vienen de `eng/core/types.hpp`; nunca depender de la STL en código de demo.
- `[✓]` ICE a `-O0`: este fork **crashea en el pass dwarf2** con el patrón `x >> (registro)` (shift variable, p. ej. `(w & 0xf000u) >> 12` enmascarado de forma que GCC usa `lsr.w %dN`) compilando a `-O0` (aunque sea con `-g0`). Dejar de compilar juegos a `-O0`; usar `-O2`/`-Os`, o `-Og` si se depura (verificar `-Og`).
- `-fno-exceptions -fno-rtti`: obligatorios en el engine (ya lo exige `CODING_STYLE.md`); el runtime de excepciones es enorme y lento.

```
  68000 de verdad                         68000 con -mtune=68020
  ┌──────────────────────────────┐        ┌──────────────────────────────┐
  │ ISA estricto 68000           │        │ ISA 68000 (mismo) + schedule │
  │ (mismo código ejecutable)    │        │ pensado para pipelines y     │
  │                             │        │ el cost model mejorado       │
  └──────────────────────────────┘        └──────────────────────────────┘
```

---

## 2. Tipos de variables

- `[✓]` Preferir `short`/`unsigned short` (16 bits) cuando el rango lo permita: el 68000 trabaja bien con word y las operaciones 16 bits son más baratas. **Con ambos operandos del mismo signo, GCC 15.1 emite `MULS.W`/`MULU.W`/`DIVS.W`/`DIVU.W` nativos** (verificado: `mul_short` → `muls.w`, `div_short` → `divu.w`), en lugar de llamar a `__mulsi3`/`__divsi3` (costosísimos).
- `int`/`long` = 32 bits. Multiplicar/dividir `int` acaba en llamadas runtime de 32 bits. Si el rango lo permite, cast a `short` y el compilador usa la instrucción nativa.
- Evitar `char` con signo en aritmética (extensiones de signo frecuentes). Usar `eng::u8`/`uint8_t` y tipos sin signo donde no haga falta signo.
- Punteros y direcciones: siempre 32 bits; cada puntero ocupa un registro de dirección (a0-a6, más escasos que los de datos).
- `[✓]` Bitfields: en el caso sondeado (campos `u16 a:4,b:4,c:4,d:4` en 68000 big-endian), GCC generó código **más corto y sin spill** con bitfield (`lsr.b #4; and.w #255; ...`) que con máscara+shift (`lsr.w %d2` con el contador en un registro spilleado a pila). No usar la regla como universal: en este fork mejoraron, pero **verificar cada patrón con `-S`** (el ICE de `-O0` del §1 salió de un `mask_shift`, o sea que los shifts con registro son justo el punto débil).

---

## 3. Argumentos de funciones y retorno (ABI)

El ABI de GCC m68k pasa los argumentos por **pila** (verificado: los `%sp` offsets en el prólogo) y devuelve todo (enteros y punteros) en **`%d0`**.

- `[✗]` La "mejor práctica" de poner `__asm("a0")` en los **parámetros de la firma** NO compila en este fork (`expected ',' or '...' before '__asm'`). No existe *register parameter* m68k.
- `[✓]` La variante válida es la **variable local de registro**: `register const u16* p __asm("a0");` — compila y sirve como pista fuerte (en el sondeo, `a0/a1` de trabajo se usaron libremente). No es una convención de llamada: los argumentos siguen entrando por pila.
- Para llamadas con convención de registros (típicas del sistema Amiga), lo limpio es un **trampolín asm**: un stub que lee `d0-d2/a0-a2` de la pila y salta al kernel C++, o escribir el hot kernel en asm puro y llamarlo desde C++ (el SDK incluye los trampolines de `__far`/Volkov para los OS calls).
- `[✓]` ABI de retorno: enteros y punteros en `%d0` (no en `a0`).
- Evitar structs grandes por valor; usar referencias/punteros. Structs pequeños (≤ 4-8 bytes, `u32`/`u64`) a veces caben por valor pero no es fiable.
- Funciones mínimas: `static inline` + `__attribute__((always_inline))`; el overhead de llamada + prólogo/epílogo es alto en 68000.

```
   convención de llamada GCC m68k (ABI estándar)
   caller:  move.w n,-(%sp)      ; argumentos por pila (orden inverso)
            move.l arr,-(%sp)
            jsr _func
            addq.l #6,%sp
   callee:  move.l 4(%sp),%a0    ; lee sus argumentos de %sp
            ...                  ; retorno SIEMPRE en %d0
            rts
```

---

## 4. Bucles: forzar `dbra` (muy importante)

GCC es reacio a emitir `dbra`/`dbf`. El truco clásico es un countdown con borde en `-1`/`0xffff`:

```cpp
ui16 n_ = count;               // o s16; en el sondeo: n = count; n -= 1;
do {
    // cuerpo, con puntero post-incrementado
} while (--n_ != (ui16)-1);
```

- `[✓]` **Con `-Os`, este patrón SÍ emite `dbra`** (verificado, 4 instrucciones):
```
.L5:
        move.w (%a0)+,%a1
        add.l %a1,%d0
        dbra %d1,.L5
```
- `[✓]` **Con `-O2` solo, NO**: GCC fuerza el bucle a comparación de límite de puntero (`add.l %d0,%d1; cmp.l %a0,%a1; jne .L9`) — denso pero sin `dbra`.
- `[✓]` Un bucle **hacia delante** (`for (i=0;i<n;++i)`) NO se convierte a `dbra` ni a `-Os` (emite `addq.l #1,%d1; jra .L2`). Escribir los hot loops en countdown.
- Preferir `*p++` (post-incremento, `move.w (%a0)+,%dN`) sobre `p[i]` con índice. Verificado que genera autoincremento nativo.

---

## 5. Otras consideraciones

- Evitar floating point a toda costa en hot paths (soft-float muy lento).
- Preferir APIs nativas de AmigaOS (`AllocVec`/`FreeVec`, el sistema del SDK) sobre `malloc`/`new`/stdlib; `ixemul` es especialmente malo. En este engine, sin heap: la memoria vive en `MemorySystem` (`chip`/`slow` arenas).
- No usar `iostream`/`stringstream` (enormes y, además, este fork no trae libstdc++).
- `constexpr`/`consteval`: cuanto más compile-time, mejor.
- Preferir aritmética ligera y algoritmos simples a tablas grandes (sin caché de datos útil; cada deref es dramático).
- Orden de miembros en structs: los campos de 32 bits y los más usados primero (facilita accesos y alineación).
- LTO (`-flto`) + `-ffunction-sections`/`-fdata-sections` + `--gc-sections` en el link eliminan código muerto. `[P]` el link completo con LTO en `build-demo.sh`.

---

## 6. Experiencia práctica de la comunidad

- Mezclar C++ limpio en la lógica y asm a mano (o inline asm) en los hot paths es el patrón ganador de demos/intros.
- Mirar el ensamblador generado constantemente (`-S` / `-fverbose-asm`); el backend todavía produce cosas subóptimas que se detectan a ojo.
- Kernels críticos: escribir en asm y llamar desde C++ con convención de registros explícita (trampolín).
- El Compiler Explorer 68k (documentado en amiga-gcc) es útil para experimentar rápido.

---

## 7. Resumen rápido de prioridades

1. Tipos de 16 bits siempre que el rango lo permita (`MULS/DIVS.W` nativos).
2. Bucles escritos en countdown → `dbra` (y usar `-Os` para que el truco funcione).
3. `-Os` + `-fomit-frame-pointer` + `-fno-exceptions -fno-rtti` + sections.
4. Sin STL, sin heap, sin float caliente.
5. Hot kernels en asm o con variables local `__asm("reg")` (no hay register-params en la firma).
6. Mirar el asm y ajustar; guardar cada descubrimiento en §8.

---

## 8. Bitácora de descubrimientos (completar en cada fase de optimización)

| Fecha | Claim | Resultado (evidencia) |
|-------|-------|----------------------|
| 2026-08-31 | Register params `int f(int* __asm("a0"))` | [✗] Error de sintaxis en `m68k-amiga-elf-g++ 15.1.0`; usar variable local `__asm` o trampolín asm. |
| 2026-08-31 | `-mcpu=68020-60` | [✗] No es argumento válido (lista: `68000..68060`, `cpu32`, `fidoa`). |
| 2026-08-31 | `-mtune=68020` sobre `-m68000` | [✓] Aceptado por el driver **pero a `-O1` cuelga la init de la demo 107** (A500_debug real, sin `-O0`/release implicados). No usarlo en este repo. |
| 2026-08-31 | Truco countdown → `dbra` con `-Os` | [✓] Emite `dbra %d1,.L5` (4 insns). Con `-O2` solo: bucle de límite de puntero. |
| 2026-08-31 | Mul/div 16-bit | [✓] `muls.w`/`divu.w` nativos (sin `__mulsi3`). |
| 2026-08-31 | Bitfields vs máscara+shift | [✓] Bitfield más corto y sin spill en el caso sondeado; verificar por patrón. |
| 2026-08-31 | ICE `-O0` + shift variable | [✓] El fork crashea en `dwarf2` con `lsr.w %dN` a `-O0` (incluso `-g0`). No compilar juegos a `-O0`. |
| 2026-08-31 | `-flto` | [✓] El driver acepta el flag y produce `.o` LTO; el enlace completo queda pendiente de validar. |
| 2026-08-31 | `<cstdint>` | [✗] No existe (toolchain freestanding, sin libstdc++); usar `eng/types.hpp`. |
| 2026-08-31 | **"Release (-Ofast/-O2/-Os) cuelga la init de la demo 107"** | [✗] **REVISADO: falso.** El cuelgue era del config `A500_o0` (`--o0`), que `run-demo` elegía por el orden de prioridad (rank 0 igual que `A500_debug`, desempate por mtime) cuando se buscaba probar release. Corregido el picker (`A500_debug`=0, `A500_o0`=1, debug con flags=2, release=4…). Con `A500_o0` eliminado, **release `-Os` corre**: READY + screenshot `OK white=4201`. El `-O0` en émulo de 68000 no llega a READY en 40s (init lenta), no es un bug de flags. |
| 2026-08-31 | Objetos idénticos but release timeout | [✗] **REVISADO**: el "release" que se ejecutaba era el exe `A500_o0` recién compilado (mismo exe bajo nombre debug corría perfecto). No había UB de optimización; era el runner eligiendo config. |
| 2026-08-31 | Divisiones en `ScrollEngine` (`/ tile_width`, `% display_height`) | [✓] **Mecanismo `eng::fast_div<N>`** (NTTP + `if constexpr` + `consteval`): con la geometría como constantes (tile 16/16, dh 288, dph 1152) el `scroll_right+left` a `-Os` emite **0 `__udivsi3`** (shifts para potencias de dos, multiplicación mágica para 288); con geometría runtime del sink emite **5 `__udivsi3`**. El `ScrollEngine` quedó templado sobre `ScrollConsts{...}` con fallback runtime (0 = preguntar al sink); cablear las constantes a priori en `XLimitedPlayfield` (templatizarlo/`Scene`) es el siguiente paso para que la demo real pague 0 divisiones. |
| 2026-08-31 | Divisiones de la fase del camino (`frame / phase_frames`, `fx*7/10`) | [✓] `XlimitedScene::update_auto` ahora avanza la fase con **contador incremental** (sin `frame/phase_frames` por frame) y usa la tabla constexpr `kLissY[(i*7/10)&63]` (sin `/10` por frame). El bucle principal queda con **0 `__udivsi3`** (solo `% display_height/planelines` constantes, y `% parallax` si se activa). |
| 2026-08-31 | Arrays de seno escritos a mano | [✓] **Patrón `SineTable<Amp, Steps>` + `ct_array`**: el seno Q8 (demo 107, `tile_demo.hpp`, y la copia duplicada de la demo 102) se GENERA en compile-time con Bhaskara I (λsin constexpr, sin float runtime; materializado como enteros). Cambiar la amplitud = instanciar `SineTable<48>`; ningún array copiado a mano. `ct_array<T,N>` es el helper genérico (LissYTable, y cualquier tabla derivable). |
| 2026-08-31 | `% display_height` no potencia de dos (224/288/896…) | [P] A `-Os` GCC los mantiene como `__umodsi3` con divisor constante (heurística de tamaño); a `-O2` se pliegan a multiplicación mágica. Alternativa: alinear `display_height` a potencia de 2 (con `K_HUD=0` pasa a 256 → refs 52→35). Decidir para el módulo estático. |

---

Cada vez que una fase de optimización confirme o contradiga una recomendación, se apunta aquí con una fila nueva (fecha + claim + evidencia) y se ajusta la sección correspondiente.
---

## 9. Auditoría de divisiones/multiplicaciones en el hot path (demo 201 EHB, corkscrew 8-way)

Proveniencia: respuesta de una IA externa a la pregunta "¿qué divisiones/multiplicaciones quedan en
el hot path del scroll de la demo 201 y cómo eliminarlas con el patrón `fast_div`/NTTP?". Se anexa
tal cual (con la numeración original) y después §9.1 la evaluación contra el código real de este
repo, con las marcas `[✓]/[✗]/[P]` de este documento.

### Respuesta de la IA

**1. Lo que ya haces bien (y debes generalizar).** En `scroll_engine.hpp` ya tienes el mecanismo
ideal (`template<class Sink, ScrollConsts C>` con `fast_div<C.tile_width>`), y en `main.cpp`
instancias `ScrollConsts{kTileWidth,kTileHeight,kDisplayH,kDisplayH*kPlanes,kPlanes}`: es
especialización total por NTTP (potencia de 2 → shift; constante genérica → multiplicación mágica;
0 → runtime). Recomendación: extender el mismo contrato a todos los denominadores/factores
calientes del layout (`tile_width`, `planes`, `bytes_per_row`, `bitmap_blocks_per_row`,
`display_height/planelines`, `viewport_w`), o un `LayoutConsts` separado, propagados por el sink.

**2. Divisiones/multiplicaciones que aún quedan en hot path.**
- En `TourDriver::lissajous_move`: `s = kSin[(m_phaseStart + t*m_ratioB)/256u]`, `tx = m_cx +
  c*m_radius/127`, `ty = m_cy + s*m_radius/127`. `/256` es potencia de 2 (verificar u32). `/127`
  no es potencia de 2 → se sugiere amplitud 128 y `>>7`, o tabla preescalada, o `fast_div<127>`.
- `randomize_lissajous`: `m_radius = 48 + next_rand()%56`, `cx = m_maxX/2`, `next_rand()%(rx+1)`
  con divisor runtime → caro; para rangos pequeños usar rechazo o máscara+ajuste.
- Verificador de framebuffer (aunque OFF): `bx = wx/kTileWidth` (signed) y doble módulo → forzar u32.
- `draw_block`: `block%20` y `block/20` (20 no es potencia de 2) → encapsular en
  `BlockBankLayout<BlocksPerRow=20>` con `fast_div`.

**3. Recomendaciones generales.** Preferir `u16/s16` cuando el rango quepa (`MULS.W`/`DIVS.W`
nativos); `mapposx` como `s32` está bien pero dividir sobre `u32` con `fast_div`. Argumentos en
registros (`__asm("a0")`) si el ABI lo permite. Mantener bucles countdown (`do{}while(--n!=0xffff)`
para `dbra`). `draw_hud()` solo en cambio de fase → bien. Acotar los `tries` de `lissajous_move`.

**4. Patrón genérico recomendado.** Un `LayoutConsts<TW,TH,Planes,BPR,DH,DPH>` NTTP con
`if constexpr` para que cada operación sea shift/mul nativa cuando el valor se conoce, dejando el
algoritmo de Steger idéntico; otra demo (288×224, tiles 32) solo instancia otra especialización.

**5. Checklist concreto.** (1) extender `ScrollConsts` con `bytes_per_row`,
`bitmap_blocks_per_row`, `block_planes_lines`; (2) amplitud del seno 128 para eliminar `/127`;
(3) forzar `u32`+`fast_div` en el verificador y driver; (4) marcar `scroll_*`/`add_draw` con
parámetros en registros si el toolchain lo soporta; (5) compilar `-m68000 -Os
-fomit-frame-pointer` y revisar el `.s` de `scroll_right` y `lissajous_move` (no debe aparecer
`jsr ___divsi3`/`___mulsi3` en el bucle de 1 px); (6) si aparece, el valor no llegó como NTTP o se
perdió por conversión signed.

### 9.1 Evaluación contra el código real (2026-09-06)

- `[✓]` El mecanismo `fast_div` + `ScrollConsts` NTTP ya está implementado y verificado: con la
  geometría como constantes, `scroll_right/left` a `-Os` emite **0 `__udivsi3`** (bitácora §8,
  2026-08-31). El checklist (5) de la IA ya se cumple en el motor.
- `[✓]` Extender el contrato a `bytes_per_row`/`bitmap_blocks_per_row`/`block_planes_lines` es
  correcto como arquitectura (`LayoutConsts`), pero en la demo actual esos valores llegan por NTTP
  o se pliegan por ser constantes; no hay `__udivsi3` en el scroll. Refactor mayor → futuro
  (sin objetivo de fps medible no tocar, regla de rendimiento de AGENTS).
- `[✗→✓]` La afirmación "`c*m_radius/127` → `__divsi3`" es INCORRECTA tal cual: `/127` con
  divisor CONSTANTE se pliega a multiplicación mágica (no llama a `__divsi3`). El coste real por
  frame es la multiplicación 32-bit `c*m_radius` (2 `__mulsi3` en Lissajous), <1 % de CPU.
  **Aplicado 2026-09-06**: `kSin` pasa a `SineTable<128,256>` y el cálculo a `(c*m_radius)>>7`
  (amplitud potencia de 2, sin división ni multiplicación mágica). Demo verificada correcta.
  Beneficio marginal (la demo NO está CPU-bound: el fps del harness lo limita el gdbserver,
  ver §7.10 del README de la 201).
- `[✓]` `/256u` en `lissajous_move` ya es potencia de 2 sobre `u32` → `lsr` nativo. Nada que hacer.
- `[P]` `randomize_lissajous`: `%56` y `m_maxX/2` son constantes (plegadas). `% (rx+1)` con
  divisor runtime es real pero corre CADA 150 frames (cambio de segmento), no por frame →
  no merece la pena optimizarlo ahora. Rechazo/máscara si algún día se vuelve caliente.
- `[✓]` `wx/kTileWidth` del auto-verificador: `kTileWidth` es `constexpr`, divisor constante →
  plegado; además `K_FB_SELFCHECK=0` (OFF) no se compila. Si se activa, usar `u32`. Pendiente menor.
- `[✓]` `block%20`/`block/20` en `draw_block_job`: `20` constante → multiplicación mágica (no
  `__divsi3`); se ejecuta por blit pero ya es plegado. Encapsular en `BlockBankLayout` es higiene
  opcional, no elimina ninguna llamada runtime.
- `[✗]` La recomendación de parámetros en registros `int f(int* __asm("a0"))` NO compila en este
  fork (ya documentado §3: usar variable local `__asm("reg")` o trampolín). La sugerencia de la IA
  es incorrecta para `m68k-amiga-elf-g++ 15.1.0`.
- `[✗]` `-mtune=68020-60` no es un valor válido en este fork (ya documentado §1: `-mcpu=`/`-mtune=`
  admiten `68000..68060`, `cpu32`, `fidoa`). Además `-mtune=68020` a `-O1` cuelga la init de la 107.
- `[✓]` Bucles countdown para `dbra`, `draw_hud()` solo en cambio de fase, y acotar los `tries` de
  `lissajous_move`: ya se cumplen (el bucle de `tries` rompe en la primera muestra no nula y tiene
  tope de 12).

**Veredicto (2026-09-06).** La demo 201 NO está CPU-bound: corre a 50 fps emulados (vblank-gated)
y el límite visible es el harness/gdbserver de WinUAE-DBG (ver §7.10 del README de la demo). Las
micro-optimizaciones propuestas son higiene para margen en hardware real, no cambian el fps del
emulador. Se implementó la única limpia y de bajo riesgo (amplitud 128 + `>>7` en el Lissajous).
El resto (extender `ScrollConsts`/`LayoutConsts`, forzar u32 en el verificador, encapsular
`block%20`) queda documentado como futuro y solo se abordará si hay un objetivo de CPU medible.
