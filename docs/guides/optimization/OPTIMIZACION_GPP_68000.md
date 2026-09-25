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
Probe       : docs/guides/optimization/_probe_gpp68000.cpp (o fragmentos en engine/docs)
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
- `[✓]` El fork es freestanding: **no hay libstdc++** (`<cstdint>` no existe). Los tipos vienen de `eng/core/types/types.hpp`; nunca depender de la STL en código de demo.
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
- **Coordenadas de píxel: `eng::pix`.** Las firmas de las primitivas de rasterizado (`Playfield::draw_span`/`write_pixel`/`planeline_for`/`byte_for`, `cpu_fill_rect`/`cpu_line`, `Span` del polígono) usan `eng::pix`, definido en `eng/core/types/types.hpp` como **`s16` en 68000 y `s32` en host**. Así el bucle por fila del rasterizado opera en word sin arrastrar `__mulsi3`/`__divsi3`, pero los tests y algoritmos de host no desbordan. **No** confundir con `eng::coord` (coordenada de simulación, `Fixed`) ni con `eng::intw` (entero de palabra natural). Los **intermedios que pueden desbordar** (`dx*dy`, `x + w`, `xb-xa)*(y-ya)`, áreas, divisiones de línea) se calculan en `s32`/`s64` **local y explícito**, con comentario. El clip lógico y las coordenadas de mundo de `Surface` siguen en `s32` a propósito: no son el bucle interior.

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
| 2026-09-06 | Ports `eng::core` (isqrt/sort/crc32/random) a `-Os -m68000` | [✓] **0 `jsr`/`jbsr` y 0 `__udivsi3/__divsi3/__mulsi3`** en todo el probe (`_probe_gpp68000.cpp`). `isqrt` usa `lsl.l`+`swap`+`mulu.w` (mul 16-bit nativa, sin `__mulsi3`); `sort_items` salen `move.w`/`cmp.w`/`jle` puros; `crc32` es un bucle de tablas+`lsr`. Los ports no degradan el hot path vs. el asm original. |
| 2026-09-14 | Coste de `eng::Block<Tag>`/`MemoryKind` (campos y reservas tipadas) | [✓] `-S` a `-O2` con `m68k-amiga-elf-g++`: `Block<CopperTag>` por valor devuelve `.view.data()`/`.view.size()` con las **mismas** instrucciones que `MemoryBlock` (`move.l N(%sp),%d0; rts`); `.kind` es un único `move.b` (idéntico a `MemoryBlock::kind`); `allocate_block<Tag>()` se pliega a `allocate()` sin trabajo extra. El sistema de tipos no añade coste al hot path (solo 1 byte de struct para el `kind`). Referencia: `out/tmp/verify-block-asm.cpp`. |
| 2026-09-14 | fps de 201/202 tras tipar `Bitmap`/`XlimitedScene` | [✓] Medido con `tools/debug/measure-fps.mjs` (ciclos emulados, A500): 201 `-O1`=25.4 fps/279560 c/f, 202 `-O1`=26.7/265669, 103 `-O1`=33.2/213864; 103 `-Os`=29.3. En 68000 `-Os` no siempre gana a `-O1`. La cifra **varía por fase del tour** (`detail` cambia), así que no es un gate fiable por sí sola; la ausencia de coste del tipado la respalda el `-S` (fila anterior), no el fps. |
| 2026-09-14 | Coste de `ModeSwitchZone` (CPU y Copper) | [✓] CPU: `emit_mode_switch_zone` compila a MOVEs en línea, **0 `jsr` y 0 `__udivsi3/__mulsi3`** (`-S` a `-O2`; `out/tmp/verify-zone-asm.cpp`). Copper: una zona de N planos = 1 WAIT + 5 MOVEs de geometría (BPLCON0/DDFSTRT/DDFSTOP/BPL1MOD/BPL2MOD) + 2·N MOVEs de puntero (+1 BPLCON4/+1 BPLCON1 si aplican, + paleta opcional), todo en el H-BLANK del raster de corte. |
| 2026-09-14 | **Orden de las escrituras en una `ModeSwitchZone`** | [✓] Intercalar `BPLCON4`/`BPLCON1` entre `BPLCON0` y los punteros (`BPLxPT`) hace que el **DMA pierda el último plano del tramo** en WinUAE-DBG (verificado: una franja de 4/5 planos mostraba 3/4). Orden seguro: `BPLCON0`→`DDF`→`BPL1/2MOD`→`BPLxPT`→(BPLCON4/BPLCON1/paleta). Fijado en `Scheduler::emit_mode_switch_zone` y en HOST-042. |
| 2026-09-14 | Dirección del area fill inclusivo (`blit_fill_region`) | [✓] El fill con `FILL_OR` **ascendente** sobre la máscara rayaba la silueta (cancelaba mal el bit de arranque por fila); el port correcto de `BlitterFillArea` es **descendente** (`BLITREVERSE|FILL_OR`, semilla al final de la región). Corregido en `blit_fill_region`; beneficia a las rutas por máscara (demo 116 flatshade-convex) y deja 078 sin regresión. |
| 2026-09-14 | **`BLTDPTR` en modo línea = base del bitmap** (contorno + area fill XOR) | [✓] El area fill `XOR` deja una **raya horizontal por vértice** si `BLTDPTR` = dirección calculada: en modo línea el primer píxel va por D y rompe la paridad par/impar del contorno en el vértice. Con `BLTDPTR` = **base del bitmap** (truco del original `DrawObject`) el relleno sale exacto: huecos internos **0.00 %** (vs 0.50 %), IoU vs original **94.4 %** (vs 92.4 %), y ~20 % más rápido que el relleno por cara (9.92 vs 8.29 fps; 715k vs 855k ciclos). Además `BLTSIZE` altura 0 = **1024 líneas** (anchura 0 = 64 words), por lo que `BitmapClearFast`/`BitmapFillFast` barren los 4 planos contiguos de 256×256 con un solo blit. |
| 2026-09-14 | **`__divsi3`/`__mulsi3` en el hot path del transform** | [✓] `math2d::div16` (`s32 / s16`) generaba `__divsi3` (division 32-bit por software) y `(u32)y * row_bytes` / `s32 * s16` generaban `__mulsi3`. Con `divs`/`muls`/`mulu` en asm (misma forma que `common.h` del origen: `"=d"(r):"0"(a),"dm"(b)`) los libcalls caen de 11+13+10 a ~1/0/0 en la demo 116, el `transform` baja de **174k a 97k ciclos** y el frame de 15.1 a **19.1 fps**. Regla: todo `s32*s16`, `s32/s16` o `u32*u16` del hot path debe pasar por `math2d::mul16`/`mulu16`/`div16`. |
| 2026-09-14 | **Solape CPU/Blitter y buffer a medias** (flatshade-convex) | [✓] El original lanza el `BitmapClearFast` **sin esperar** (se solapa con el transform CPU); `blitter_clear`/`blitter_area_fill` aceptan `wait=false` y se expone `AmigaBackend::wait_blitter()`. Pero solapar el **fill** que se va a mostrar **parpadea**: el buffer se muestra a mitad de relleno (frames con hasta 16 % de huecos internos). Regla: solapar un blit con la CPU **solo si escribe un buffer que NO se está mostrando** (el clear del buffer trasero); el fill del buffer que se muestra debe esperarse (`WaitBlitter` antes del swap, como el original). Neto: 10.6 → **16.7 fps** con la imagen estable. |
| 2026-09-14 | **Escrituras de registro por línea de blit** (flatshade-convex) | [✓] Reescribir los 6 comunes (`BLTAFWM/BLTALWM/BLTADAT/BLTBDAT/BLTCMOD/BLTDMOD`) en cada arista×plano costaba ~180 ciclos/línea. Con `blitter_lines_begin` (1×/frame, como el `DrawObject` del original) la línea baja de **1,752 a 1,569** ciclos, los edges de 129k a **118k** y el `update` de 383k a 362k. |
| 2026-09-14 | **Cuantización del frame por vblank** | [✓] El frame está cuantizado a múltiplos de vblank (~142k ciclos): 2 vblanks = 25 fps, 3 = 16.6, 4 = 12.5. Con el `update` en 362k seguimos en **3 vblanks**; recortar 10-20k no cambia el fps hasta cruzar 284k. El profiler del original (`system/profiler.c`, líneas de raster) da **Transform 156 / Draw 130 / Fill 289** (575 líneas < 626 = 2 vblanks, 25 fps); nuestros edges (~2x) y transform (1.4x) son el exceso a recortar. |
| 2026-09-14 | **Coste de una escritura a registro custom** | [✓] Micro-benchmark: un blit de **1 palabra** cuesta **1,404 ciclos** con `cpu_cycle_exact`; es decir, todo blit tiene un coste fijo (~1,400) y **cada escritura a registro custom ~57 ciclos**. Regla de diseño: minimizar el numero de escrituras a registros custom y de arranques de blit por frame. **Ojo:** `blitter_lines_begin` (fijar los comunes del modo linea 1×/frame) **rompio el render** (caras deformes) pese a `verify-116` PASS → revertido; en cambio `write_custom_pointer` en **una escritura de 32 bits** (fiel al original, que declara `bltcpt` como `void*`) **se mantiene**. Un ahorro de escrituras que altere el estado del Blitter entre blits puede cambiar la imagen: validar **visualmente/estructura**, no solo con cobertura/tonos. |
| 2026-09-14 | **`blitter_line_eor_multi`** (una llamada por arista, comunes 1×) | [✗] Fusionar los planos de una arista en una sola llamada medía **mas lento** (114k vs 106k ciclos de edges): el emulador no premia reducir las escrituras comunes y el bucle extra cuesta. Descartado. |
| 2026-09-14 | **BLITHOG mal activado en un intento previo** | [✓] El intento previo usaba `dma_blithog = 0x0080`, que es `DMAF_COPPER`, NO `DMAF_BLITHOG` (0x0400, ver `<hardware/dmabits.h>`). Nunca activó BLITHOG. La vía correcta es `AmigaBackend::set_blitter_priority(true)` (`DMACON = 0x8400` = SETCLR\|BLTPRI). Con BLITHOG el fill baja de 143.8k a 131.8k (igual que el original). |
| 2026-09-14 | **`blitter_line_eor_prepare/draw`** (Bresenham 1× por arista) | [✓] `LineEorParams` + `prepare`/`draw`: los parámetros (con0/1, mods, size, derr, row_offset) son independientes del plano; el `DrawObject` del original avanza `bltcpt += plane_bytes` sin recalcular el octante. Los edges bajan de ~119-123k a ~114k. |
| 2026-09-14 | **Pipeline de 3 buffers (transform+clear escondidos bajo el fill)** | [✓] Con 2 buffers el clear del buffer trasero es inseparable del camino crítico (se muestra hasta el swap). Con 3 buffers, el buffer a pre-limpiar ni se muestra ni se dibuja: su clear (89k) y el transform del frame siguiente (127k) se lanzan MIENTRAS el Blitter hace el fill del frame actual. El `update` baja de 344k a ~238k (< 284k = 2 vblanks) y el frame de 16.58 a **~20.7 fps** (343k). |
| 2026-09-14 | **¿El emulador infla el fill? NO: el fill del original coincide** | [✗] **Hipótesis descartada con evidencia.** El profiler del propio original (`system/profiler.c`, contador de líneas de raster) da **Fill 289 líneas = 131k ciclos** (a ~454 ciclos/línea PAL); nuestro port mide **fill 131.8k**. Coinciden: WinUAE cobra el mismo coste al área fill del original y al nuestro. El emulador es consistente; la brecha NO es un artefacto del modelo. La brecha real está en el **transform (127k vs 71k, 1.8x)** y en los **edges (114k vs 59k, 1.9x)**: el original los hace más baratos (codegen 68k a mano, `register ... asm("d3")`, macros `DRAWLINE` inline). Medir el original en vivo (render/s) quedó bloqueado porque el efecto del ADF sale al leer `LeftMouseButton()` (botón del ratón) y el `BRA *` final es un spin del framework tras salir. |
| 2026-09-14 | **`g_eng_prof` con la pipeline: las etiquetas v[0..4] cambian** | [✓] Al reestructurar `draw_edges_area_fill` en `draw_edges` + `area_fill_planes`, `v[0]` pasa a medir wait+swap+edges (ya no solo el clear), `v[1]` el transform (que corre durante el fill), `v[2]` los edges, `v[3]` el lanzamiento del fill. El frame (dCycles/dFrame) incluye la cola del Blitter que el `update` no espera (el wait se absorbe en el primer wait del update siguiente). |
| 2026-09-15 | **Port asm de flatshade-convex (`support/flatshade_asm.s`)** | [P] WIP. `fs_update_face_visibility`/`fs_update_edge_visibility_convex` completan la consola debugperiph (`FfEe`); `fs_transform_vertices` produce una **excepción** (PC en el vector del Kickstart) y la demo no llega a READY con `K_FLATSHADE_ASM=1`. **Dos bugs encontrados y corregidos**: (1) `movea.l 0(aN,dX.w),aM` carga el CONTENIDO de la dirección, no la dirección → hay que usar `lea 0(aN,dX.w),aM` (el C++ de GCC genera `lea (0,aN,dX.w)`); con `movea.l` la rutina leía basura y escribía con direcciones basura. (2) `moveq #511,d4` no cabe (moveq es -128..127) → `move.w`; `asr.l #12` no es inmediato válido (>8) → `moveq #12,dN; asr.l dN,dX`. **Pendiente**: el crash del transform no es la división (saltarla no lo evita). Diagnóstico con el periférico `0xB70000` (consola) y `monitor debugperiph console`: 'FfEeTp' + flood de 'g' del bucle de grupos. El `vertexGroups` de `pilka` es `[2,16,...,828, 0, 0, ...]` (termina en 0), así que el bucle debería parar. **Descartados por prueba**: (a) la división (saltarla no evita el crash); (b) el mask del `movem` (guardar d0-d7/a0-a6 tampoco); (c) el `lea` (ya corregido). La ruta C++ (`K_FLATSHADE_ASM=0`, por defecto) es la que funciona (verify-116 PASS). |
| 2026-09-15 | **Port asm de flatshade-convex: `fs_draw_edges` desfasa el contorno (ruta asm NO válida)** | [V] La ruta asm (`-DK_FLATSHADE_ASM=1`) **no** produce la imagen correcta. Diagnóstico aislado: con el `draw_edges` C++ sobre la visibilidad/transform asm el balón sale bien → el fallo está en **`fs_draw_edges`**. Medición del wireframe (`-DFLATSHADE_SKIP_FILL=1`, ASM vs C++): ~2900 px de contorno en cada uno pero **solo ~96 en común**; las líneas asm van **desfasadas ~1-2 px** (p. ej. y=52: ASM x=386,387/394,395 vs C++ x=388,389/392,393). Como el **area fill es XOR** (conmuta por píxel del contorno y propaga por paridad de scanline), el desfase rompe la paridad y el relleno se desmadra en bandas y triángulos. **Lección de proceso: `verify-116` da PASS con la imagen rota** (cobertura y nº de tonos —incluso 15, más que los 7-8 correctos— no detectan un desfase de contorno); el gate válido es **visual** (secuencia + Ollama pidiendo anomalías, o el diff de wireframe). Fixes válidos encontrados por el camino (aplicados, pero insuficientes para el desfase): (1) **cámara pisada** en `fs_update_face_visibility` (`d4` reusado como temporal) → recargar `cx/cy/cz` por cara; (2) **`.Lwait_blit` pisaba `d0`** (DMACONR cargado en `d0`, que `fs_draw_edges` escribe como BLTCON0 tras el wait → ningún blit pintaba; balón ausente sin crashear) → salvar/restaurar `d0`; (3) **`BLTAPT` sin extender** → `ext.l` de `derr` (acumulador de 32 bits), como `blitter_line_eor_draw`. El bucle de `fs_draw_edges` reutiliza `d5` como `x1`, así que no sirve de contador (se cuentan las métricas por slots de pila). Default de la demo: `K_FLATSHADE_ASM=0` (C++). |
| 2026-09-15 | **Sombreado por cara como punto de personalización (`light_ops`) + `swap` vs `>>16`** | [✓] El sombreado de `lib3d` pasa a `eng::math::light_ops` (núcleo) con backend 68000 (`cpu/m68k/light.hpp`: `mulu.w` + `swap`), igual patrón que `arith`/`pack3_ops`. Medido con `-S` (68000 `-O2`, `out/tmp/shade_cmp.s`): la formulación portable `(mulu16(...) >> 16)` emite `clr.w %d0; swap %d0` y la nativa sólo `swap %d0` → **18 vs 19 instrucciones** (~4 ciclos/cara menos; `clr.w Dn` = 4 ciclos). No medido en ciclos reales con el periférico (pendiente). Además: `arith::mulu` pasa a tomar la representación **sin signo** (antes el genérico sign-extendía y daba un valor distinto al `mulu.w` del 68000 para operandos con el bit alto). Aritmética de palabra (`div16`/`mul16`/`mulu16`) movida de `math2d` a `eng/core/word.hpp` (no tiene geometría; `math2d` la reexporta). Gate reproducible: `node tools/analyze/codegen-report.mjs` (0 libcalls en `c_shade`, `c_proj`, `dot`, `transform`, `matmul`). |
| 2026-09-15 | **La amortización de `x·y` en el empaquetado ya la hacía el CSE** | [✗] El `MULVERTEX` del original calcula `xy = x·y` **una vez por vértice** y lo comparte entre las tres filas de la proyección; en el port, `pack3_ops::eval` lo recalculaba por fila. Hipótesis: 9→7 `muls`/vértice. **Medido** (`-S`, 68000 `-O2`): `lib3d::transform_vertices` emite **10 `muls` + 2 `divs`, 0 `jsr`** ANTES y DESPUÉS de pasar `xy` explícitamente, con el cuerpo idéntico (163 líneas) → GCC ya eliminaba por **CSE** los dos `muls(x,y)` redundantes (el asm muestra `add.w`+`muls` empaquetado y ningún `muls` de `x*y` por fila). Se mantiene el paso explícito de `xy` (fidelidad al original y no depender del optimizador) pero **sin ganancia medible a `-O2`**; a `-O0` (config de depuración) sí evita los dos productos. Los 10 `muls` = 3 del caché `e0/e1/tz` + 7 de las tres filas. Lección: medir el asm antes de dar por hecha una optimización de este tipo. |
| 2026-09-15 | **`object3d::worldToObject` no es una inversa completa** | [!] `update_object_transformation` invierte la parte lineal (`S⁻¹Rᵀ`, con `load_reverse_rotate` + `1/s` por `div16`) pero deja la traslación en `−T`, no en `−S⁻¹Rᵀ·T`. Medido en host: `compose(objectToWorld, worldToObject).m ≈ I` (4094,-2,0 / 1,4093,-1 / 0,0,4090) pero `.t = (-9965,-800,-1924)` en vez de `(0,0,0)`. Es el comportamiento del port 1:1 (lo fija HOST-014) y de él depende la cámara en espacio objeto de 079/116, así que **no se cambia**. Se añade `math3d::inverse_rigid(Affine3)` (la inversa correcta para el caso ortonormal, `mᵀ` / `−mᵀ·t`, HOST-055) como utilidad reutilizable para componer. |
| 2026-09-20 | **Descomposición del frame de la demo 116 por secciones (builds con `FLATSHADE_SKIP_*`)** | [V] Medido con `tools/debug/measure-fps.mjs` (ciclos emulados, A500_release): **efecto completo = 350.596 c/f = 2,5 campos (20,23 fps)**; **sin `clear` = 282.334 c/f = exactamente 2 campos (25,13 fps)**; `edges`+transform (sin clear/fill) = 285.151 = 2,0 campos; `fill`+transform (sin clear/edges) = 142.102 = 1,0 campo; `clear`+transform (sin edges/fill) = 173.169 = 1,2 campos. Lectura: **el `clear` son ~68k expuestos y es exactamente lo que separa de los 25 fps**; el resto del efecto ya cabe en 2 campos. |
| 2026-09-20 | **El frame de 116 está limitado por el trabajo SERIAL del Blitter, no por el CPU** | [V] Con la pipeline de 3 buffers el transform (CPU) ya se solapa con el fill. Sumando el trabajo del Blitter (`edges` + `fill` + `clear`) sale ~358k ≥ el frame (350k): el Blitter es el cuello. Reordenar el `clear` (lanzarlo primero para taparlo bajo el transform, patrón de bobs3d y del original) **no cambia la suma** —el Blitter es serial— así que no cruza el umbral. El solape del `clear` ayudó en bobs3d porque allí el camino crítico era el **CPU**, no el Blitter; en 116 no aplica. |
| 2026-09-20 | **`clear` por CPU para sacarlo de la cola del Blitter** | [✗] Idea: pre-limpiar el buffer siguiente con escrituras de 32 bits del CPU durante el fill, para que el `clear` no ocupe la cola (serial) del Blitter. Medido (`-DFLATSHADE_CPU_CLEAR=1`, 32 KB en bucle `u32`): **6,30 fps / 1.125.597 c/f**, mucho peor. Con `cpu_memory_cycle_exact` la contención de bus del CPU con el DMA del Blitter es brutal (~34 ciclos por escritura). El `clear` debe quedarse en el Blitter. |
| 2026-09-20 | **`blitter_cycle_exact` no afecta a la medida en este build** | [V] Con el `clear` aislado (`FLATSHADE_SKIP_EDGES=1 FLATSHADE_SKIP_FILL=1`): `blitter_cycle_exact=true` y `=false` dan **idéntico** 173.169 c/f (40,96 fps). Desactivar además `cycle_exact`/`cpu_cycle_exact`/`cpu_memory_cycle_exact` *sube* el conteo a 211.746 (33,50 fps), no lo baja. Es decir: **no se puede usar esa clave para separar el coste del Blitter del modelo del emulador**; la cifra medida (350k) es la que cobra el emulador, y hay que reducir trabajo real, no reordenar. |
| 2026-09-20 | **El «25 fps del original de 116» es una cifra NO verificada** | [!] El objetivo de 25 fps sale del **profiler del propio original** (`system/profiler.c`: transform 71k + draw 59k + fill 131k = 261k < 284k). El oráculo del original de **bobs3d medido en-emulador dio 20,1 fps (2,49 campos)**, no los 2 campos que declaraba su profiler; bobs3d solo llegó a 25 recortando 4 BOBs. El oráculo de 116 no se pudo medir en este entorno (el MCP no lanza el `.adf` de forma fiable). Dado que el código de aristas es 1:1 con el original (`DRAWLINE` = 8 escrituras + `_WaitBlitter`, igual que `blitter_line_eor_draw`) y que sin `clear` damos 25 fps, es plausible que **el original de 116 también ronde los ~20 fps** y que el port esté ya a la par. Medirlo con el contador de ciclos es el paso que falta. |


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

## 9. Registros fijados y asm inline: cuándo sacar la rutina a `.s` (2026-09)

Lecciones del bucle de fuego de `fire-rgb` (demo 080), que en el original fija **los 7 registros de dirección** (`a0..a6`: 6 punteros + `dt`).

- **Síntoma**: con `m68k-amiga-elf-g++ 15.1.0` a `-O1` el bucle no compila: `unable to find a register to spill in class 'ADDR_REGS'`.
- **Causa 1 — GCC-15 ignora los pins**: `register T* p asm("a0")` **no** obliga a usar `a0`; el compilador asignó uno de esos punteros a `d0`. Con 6 punteros + `dt` no caben en los 7 registros de dirección.
- **Causa 2 — frame pointer en `a6`**: a `-O1` G++ usa `a6` como frame pointer, justo el que quiere `Eptr`. `#pragma GCC push_options` + `#pragma GCC optimize("O2","omit-frame-pointer")` por función libera `a6`, pero **sigue sin respetar los pins** → no basta.
- **Solución**: sacar el bucle a una **rutina `.s` aparte**. El build ensambla `support/*.s` con `m68k-amiga-elf-as` (gas) y `support/{audio_mixer,music}/*.asm` con VASM; así se **salta por completo la asignación de registros de G++**. Conservar además la versión C++ (flag `K_FIRE_ASM`) como ruta segura.
- **Truco de direccionamiento reutilizable**: `move.l (0,a5,d1.w),d2` — el modo indexado `(An,Dn.w)` suma el índice como **offset en BYTES**. Con una tabla de `u32`, `dt + idx == dt[idx/4]`: el original lo usa para indexar por la **media** (suma/4) sin `shift` ni `división`. Si el índice debe ir escalado por 4 (índice de elemento), hay que usar `lea`+`add` o `.w*4` (solo 68020+); en 68000 el byte-offset es la opción gratis.
- **Cómo inspeccionar lo que genera G++**: el build deja un `.s` con el **desensamblado** (`objdump`); para un objeto suelto, `m68k-amiga-elf-objdump -d obj/demos/<demo>/<cfg>/support_<x>.o`. Útil para confirmar instrucciones y asignación antes de culpar al compilador.
- **Regla práctica**: antes de pelear con el optimizador, **mirar el `.s`**. Si G++ no produce lo del original (o no compila), escribir la rutina en asm aparte y **mantener la de C++** como fallback.
- **[R] Resuelto**: `support/fire_loop.s` ensambla **idéntico** al original (verificado con `objdump -d`) y funcionaba pero no alcanzaba READY por una causa **ajena al asm**: `.cfi_startproc/.cfi_endproc` generaban una sección `.eh_frame` **no vacía** que el canal lateral enumeraba pero el `.map` del runner filtraba, desplazando los índices y resolviendo mal `g_eng_run_status`. Sin CFI en los `.s` (y con `.eh_frame` incluido en `findMapAllocSections`) queda resuelto; `K_FIRE_ASM=1` es el default.

### 9.1 Bucle caliente de `plasma` (copper chunky, demo 082): indirección por píxel

- **Síntoma**: el plasma iba a **12.7 fps** (573 338 ciclos/frame). Ni el modo IRQ ni la
  copperlist eran la causa (medido con `K_DIAG_NO_COPPER`): todo el coste estaba en `update`.
- **Causa**: `draw_into` llamaba `scene.set(row,col)` por **bloque** (2304/frame), y `set()` hacía
  **dos comprobaciones de rango + `m_slot[row*max_cols+col]` + multiplicación**; g++ además
  emitía `andi.l #255` redundante (por el `move.b` a registro de datos) y **no** generaba `dbra`.
  ~18 instrucciones/iteración. El original usa **asm con puntero incremental y paso de 2 words**
  (`movew cmap@(d0:w),ins@+ ; addql #2,ins`) = 6 instrucciones y **escribe en orden inverso**
  (x de `cols-1` a 0, así que el bloque 0 recibe `xbuf[cols-1]`).
- **Solución**: bucle gas aparte `support/plasma_chunky.s` (idéntico al del original, con
  `moveq #0,d0` para zero-extender y `dbra`), alimentado con **punteros de fila** del driver
  (`CopperChunkyScene::chunky_row`, análogo a `scene::Scene::plane()`). Sin indirección por píxel.
- **Resultado**: **36.5 fps** (194 185 ciclos/frame), ~3x. El resto es el 68000 sobre RAM lenta
  (~78 ciclos con las 6 instrucciones); no hay más margen sin fast RAM en A500.
- **Método de medida reutilizable**: contador de ciclos del periférico (`0xB7E928`) leído dentro
  del `update` y volcado temporalmente a `g_eng_run_status.detail`; se aísla cada parte con flags
  de diagnóstico (`K_DIAG_SKIP_DRAW`, `K_DIAG_FASTBUF`, `K_DIAG_NO_COPPER`) y se mide con
  `tools/debug/measure-fps.mjs <demo> <config>`. **Medir antes de atribuir**: las tres hipótesis iniciales
  (modo IRQ, copper, build -O0) se descartaron con datos.

## 10. Auditor de codegen: `tools/analyze/asm-audit.mjs` (2026-09)

Antes de bajar a asm "a mano" conviene saber **dónde** el compilador emite código caro. El auditor
desensambla un ELF (o un `.o`) y reporta, **por función**, cuántas llamadas a rutinas de soporte
emite:

```
node tools/analyze/asm-audit.mjs --demo demos/techniques/amiga/playfield/107_xlimited_corkscrew [--ext] [--top N] [--json] [--strict]
node tools/analyze/asm-audit.mjs out/demos/<demo>/<cfg>/<demo>.<cfg>.elf
```

Cuenta `__mulsi3`/`__umulsi3` (mul32), `__divsi3`/`__udivsi3` (div32), `__modsi3`/`__umodsi3`
(mod32), helpers soft-float y `__ashlsi3`/`__ashrsi3`/`__lshrsi3`, más el nº de `jsr`/`jbsr` como
contexto (y `--ext` añade `andi #255` redundantes). Ordena por peso y suma totales. **Caveat
importante**: no sabe la **frecuencia de llamada**; un `__mulsi3` en un `fill_screen` de init no
importa, uno por píxel/frame sí. Es un **filtro** para dirigir la medición, no un veredicto.

- **Control positivo**: un `.o` con `a*b`, `a/b`, `a%b` y `(float)a*2.5f` (compilado con el
  toolchain) reporta `mul32/div32/mod32/float` correctamente. `-r` en el `objdump` hace que
  también funcione con `.o` sin enlazar (relocaciones).
- **Hallazgo 2026-09**: `061_c2p` y `082_plasma` (los efectos puros) → **0 helpers caros**: el
  optimizador no es el problema ahí. En cambio `107_xlimited_corkscrew`, `201_ehb_map` y
  `104_tile_scroll_ring_dualpf` **sí** tienen mul32/div32/mod32 en `XLimitedPlayfield::add_draw`,
  `fill_screen`, `scroll_*` y `main` (vienen de config de geometría **runtime**, no NTTP: `a *
  m_cfg.tile_width`, `viewport_h / tile_height`). Son **candidatos** a verificar con medida (la 107
  corre a ~48 fps, así que los de init/ocasionales no son cuello); si alguno cae en el camino por
  bloque/frame, la vía es llevar la geometría a NTTP (como `ScrollConsts`).

**Lección de proceso**: el primer resultado del auditor fue "todo 0" y era **falso negativo**
(objdump Windows emite CRLF y el regex de cabecera anclaba en `$`). Se detectó con un **control
positivo**; sin él habríamos concluido lo contrario. Todo verificador necesita su caso que **debe
fallar/detectar**.

## 11. Divisiones y multiplicaciones en el engine: auditoría y qué se puede reducir (2026-09)

### 11.1 La realidad del 68000 (y por qué `-Os` no basta)

- **No hay multiplicación 32×32.** Para dividir/módulo de `u32` por una constante **no potencia de
  dos**, GCC necesitaría la "multiplicación mágica" con un producto de 64 bits; al no poder, emite
  un **libcall** `__udivsi3`/`__umodsi3` (~150 ciclos) **incluso a `-Os`** (donde además prefiere el
  libcall por tamaño). Verificado: `v % 768u` a `-Os` → `jsr __umodsi3`; `__attribute__((optimize("O2")))`
  **no** lo cambia.
- **Potencia de dos es gratis**: `v / 2^k` → `lsr`, `v % 2^k` → `and`. Verificado: `v % 256u` a `-Os`
  → `moveq #0,d0; move.b 7(sp),d0` (¡un solo byte!).
- **`-O0` (build `--debug`, el que usa la regresión/pipeline) convierte TODA división/módulo
  constante en libcall**, incluso `v / 8`. De ahí que el auditor sobre ELFs `--debug` infle los
  conteos: mide lo que *podría* ejecutarse, no lo que se ejecuta en release.
- **Conclusión operativa**: el lever real es (a) que la geometría sea **potencia de dos**, (b) que
  los denominadores lleguen como **constante de compilación (NTTP)**, o (c) **evitar el `%`/`/` en el
  bucle** (envolver de forma incremental). El truco de la constante mágica **no** está disponible
  como asumíamos.

### 11.1b `-Os` es MÁS LENTO que `-O2` en este engine: es un bug de codegen (2026-09-17)

La premisa "en 68000 el código compacto es más rápido (no hay i-cache útil)" es **falsa** aquí.
Medido sobre la misma fuente de la **086** (`A500_release`, contador de ciclos del Amiga,
`measure-fps.mjs`):

| Nivel | Ciclos/frame | Campos | Vs `-O2` |
|---|---:|---:|---:|
| `-O0` | 3.564.392 | 25,1 | 3,6× peor |
| `-O1` | 1.043.237 | 7,4 | +6 % |
| **`-O2`** | **994.714** | **7,0** | **— (mejor)** |
| `-Os` | 2.258.674 | 15,9 | **+127 % (2,3× peor)** |

**Causa**: a `-Os` gcc **no respeta `always_inline` de la cadena caliente** y **deshace la
abstracción** del engine. En el asm de `build_frame` de la demo a `-Os`:

- `ListBuilder::move` deja de estar inlineado y se llama con `jsr` **una vez por MOVE de copper**
  (push de argumentos + jsr + ret por cada par WAIT/MOVE);
- aparecen **llamadas reales a `memset`/`memcpy`** para inicializar/construir el `Scheduler` y
  copiar structs (a `-O2` son stores directos);
- `build_frame` pasa a tener **`link.w a5,#-568`** (568 B de marco de pila) frente a los ~20 B de
  `-O2`, con `movem` de 11 registros.

**Bisección por unidad** (`ENGINE_OPT`/`DEMO_OPT`/`C_OPT` del script de build), mismo resto a `-O2`:

| Unidad a `-Os` | Campos |
|---|---:|
| todo `-O2` (ref) | 7,0 |
| engine | 7,0 (sin efecto) |
| **demo (`main.cpp`)** | **11,2** |
| soporte C | 7,4 |
| todo `-Os` | 15,9 |

⇒ El coste lo introduce la **demo** compilada a `-Os` (instancia los inlines del engine con
semántica de `-Os`) y se **agrava al combinarla** con el resto (efecto no lineal).

**Arreglo**: el default release de `build-demo.sh` pasa a **`-O2`**. El `-Os` **no** es una opción
válida para el engine hasta que se resuelva el codegen; si se quiere reducir tamaño, hacerlo por
diseño (datos/políticas), no con `-Os`.

**Corrección adicional medida**: `-O2` no solo es más rápido, **también emite más BOBs** que `-O1`
en la 086 (`detail` 8/8 frente a 7/8), y la captura con `-O2` es visualmente correcta (degradado
continuo + 8 discos). `-O1` seguía siendo el "perfil verde" de la regresión por inercia, no por ser
el mejor.

**Validación del cambio a `-O2` (2026-09-17)**: `build-all-demos.sh --release` compila las **58
demos sin fallos** (`ok=58 fail=0`) ⇒ el aviso histórico de cuelgue con `-mtune=68020` **no aplica**
a `-O2` sin `-mtune`. Arranque y medida en release `-O2` de una muestra:

| Demo | Release `-O2` | Nota |
|---|---:|---|
| 055 copper_rainbow | 2,0 campos (25 fps) | igual que debug |
| 085 copper_plan_scene | 3,0 campos | ok |
| 086 bob_objects | 7,0 campos | bate a debug (7,2) |
| 101 ehb_tile_scroll | **1,0 campo (49,92 fps)** | objetivo de 50 fps cumplido |
| 116 flatshade_convex | 2,5 campos | ok |

Dos demos **no** son válidas en release, pero **tampoco en debug** (no es regresión de `-O2`, es deuda
preexistente): la **061** no avanza frame (`detail=0x0`, idéntico en debug y release) y la **107**
cuelga (`READY=2`, ~300 campos) también en debug. Ambas están en la lista de pendientes conocidos.

Conclusión: **`-O2` es el nivel correcto**; la escena ligera ya alcanza 1 campo, y las demos lentas lo
son por trabajo de CPU real por frame (en la 086, `update` = ~7 frames de cómputo: `emit` 37 %, `actors`
12,7 %, `blits` 10,6 %), no por el build.

### 11.2 Auditoría (inventario real, `asm-audit.mjs`)

`node tools/analyze/asm-audit.mjs --all --engine` agrega 90 ELFs. Antes de optimizar:
`mul32=981 div32=279 mod32=554` sitios estáticos, dominados por `eng::field` (`TileFieldController`,
`XLimitedPlayfield`, `ScrollEngine`). Se reparten en tres clases:

| Clase | Ejemplos | Coste real |
|---|---|---|
| **Init/una vez** | `TileFieldController::begin/valid_config`, `XlimitedScene::begin`, `main` (setup) | Irrelevante (una vez) |
| **Per-frame, denominador runtime** | `TileFieldController::update/enqueue_*` (`m_config.tile_width/size`), `XLimitedPlayfield::dmod*`/`add_draw` (`m_cfg.*`) | Unos pocos por frame → pequeño % |
| **Per-frame, denominador constante NTTP** | `ScrollEngine::r_dh/r_dph` (`fast_div<C.display_height>`) con 224/768 (no pow2) | libcall en el camino tomado |

**Medición de magnitud**: en `scroll_*` de 107 (release) hay ~1 `__umodsi3` por llamada, y se llama
≤ `max_step` veces/frame → <2 % del frame (141 876 ciclos). **No es el cuello** (las demos de tiles
van a 48-50 fps, *vblank-gated*); reducirlo es **margen** para hardware real, no un cambio de fps.

### 11.3 Qué se ha hecho

- **`eng/core/math/fast_div.hpp`**: se añaden utilidades **runtime** `is_pow2`, `ilog2`, `asr_floor`
  (shift aritmético = floor) — antes solo existían las `consteval` (`ct_*`), inservibles para
  geometría que llega por config. Test host **HOST-022**.
- **`TileFieldController` (field/tile_field.hpp)**: precálculo de `m_tw/th_shift`, `m_ts_mask` en
  `update_pow2()` (llamado en `begin`/`update`) y helpers `div_tw/div_th/mul_tw/mul_th/tile_mod/
  fb_cols/fb_rows`. Si `tile_width`/`tile_size`/`tileset_count` son potencia de dos (16/32, el caso
  real) el **camino ejecutado** usa shift/máscara; si no, cae en el camino general exacto (mismo
  resultado). Sustituye `floor_div(...)` + `/ tile_width` + `* tile_size` + `% tileset_count` en
  `update`/`enqueue_*`/`draw_pending`/`tile_job`. Precedente idéntico ya existente:
  `TileLayerMap::wrap_coordinate` (máscara si `period` es pow2).
  **MEDIDO (2026-09)**: en la demo 106 (`K_DIAG_FIELD_CYCLES`) los dos `update` de campo cuestan
  **46 310 vs 373 464 ciclos** forzando el camino runtime (`K_FIELD_FORCE_RUNTIME=1`) → **−87 %
  (8×)**; el frame global en ese caso pesado sube 12.2 → 13.2 fps. A `-O0` (build `--debug`) es
  donde más se nota, porque toda división constante es libcall.
- **`XLimitedPlayfield` (field/xlimited.hpp)**: accesores `ctw()/cth()/cplanes()` que devuelven la
  constante `SC` si se conoce (validada igual a `cfg` en `begin`) y si no el campo runtime; usados
  en el hot path (`draw_block_job`, `block_videoposy`, `planeline_for`, `hardware_view`,
  `fill_screen`, `tile_width()/tile_height()`, `map_*_blocks()`). Además el bloque de depuración
  `dbg_ink_visible` de `add_draw` usa `ctw/cth/cplanes` y `dmod1`.
  **Resultado modesto**: los divisores que dominan en el corkscrew (`display_height=288`,
  `display_planelines=1728`, `planes=6`, `bitmap_width=352`) son **todos no potencia de dos**, así
  que siguen siendo libcall. Verificado visualmente: 107 (secuencia), 201 y 202 OK.

### 11.4 Qué queda (ordenado por valor)

0. **OJO — `display_height` NO es negociable**: `display_height = viewport_h + 2*tile_height` es
   un **invariante del corkscrew** (ver AGENTS §checklist 201 y `201_ehb_map/README` §7): el anillo
   vertical se dimensiona para el viewport TOTAL, y `block_videoposy`/`mapy` colisionan si se
   cambia. **No se puede "encajar a potencia de dos"** sin romper la imagen. Para 320×256 → 288, y
   288 = 32·9 **no** es potencia de dos → `% 288` seguirá siendo libcall. La vía real para ese
   divisor es **evitar el módulo** (punto 4).
1. **Geometría potencia de dos donde SÍ es libre**: `tile_width/tile_size` (16/32, ya),
   `tileset_count` (64, ya), mapas (256×128, ya). Para tamaños nuevos que la IA elija, preferir
   potencia de dos. `bitmap_width` (352/384) y `planes` (3/6) son no-potencia-de-dos por diseño.
2. **[HECHO] `XLimitedPlayfield` usa `SC` (NTTP) en el hot path** (`ctw/cth/cplanes`,
   `block_videoposy`→`dmod1`, `planeline_for`, `hardware_view`, `fill_screen`, `draw_block_job`).
   Beneficio modesto por los divisores no-potencia-de-dos del punto 0.
3. **Metafunción `TileFieldController<Config>` (NTTP)**: llevar `tile_width/size/planes/count` a
   constante elimina el `if constexpr` runtime y el fallback del binario (auditor a 0).
4. **Evitar `%`/`/` en el bucle por envolvimiento incremental** (LA palanca real para 288/1728):
   mantener `mapposy % display_height` (y `videopos` por eje) como estado, actualizándolo con el
   delta (`if (m >= DH) m -= DH; if (m < 0) m += DH;`) en vez de recalcular `%` cada paso. Ya se
   hace análogamente en `draw_pending` (cursores) y en `TileFieldController` (bandas).
5. **`divu.w`/`divs.w` nativos (16 bits)** para denominadores no-potencia-de-dos cuando el cociente
   cabe en 16 bits; `runtime_div::qr` ya evita el doble libcall.
6. **`dbg_ink_visible`**: el bloque de depuración de `add_draw` corre **por bloque y frame** (varias
   divisiones) aunque solo lo use la 107. Candidato a flag de compilación por instancia
   (`bool InkDetect` como parámetro de plantilla) con default OFF y ON solo en 107.
7. **Relleno CPU de polígono (`field/playfield.hpp:195`)**: por **arista y scanline** hace
   `x0 + (x1-x0)*(y-y0)/(y1-y0)` → `__mulsi3` + `__divsi3` en el bucle O(alto). Es el *fallback*
   cuando no hay `PolygonFillSink` (HOST-046 y `mesh_renderer`). Vías: pendiente incremental
   (acumular `dx/dy` por fila con `div16` una vez por arista) o `mul16`+`div16` si los operandos
   caben en `s16`. No es el camino de las demos de producción (Blitter/sink), pero sí del raster
   software.

### 11.5 Regla de diseño (para que el compilador sí optimice)

- Toda **dimensión de geometría** que se pueda elegir libremente → **potencia de dos** (y como
  NTTP si es posible). Evita el libcall y el truco mágico imposible en 68000.
- Todo **divisor conocido a priori** → NTTP + `fast_div<N>` (no `u16` runtime).
- Todo **`%`/`/` por un valor runtime** dentro de un bucle → reducir a incrementos/compases.
- Antes de tocar: `asm-audit.mjs` para localizar, y **medir** el camino ejecutado (el conteo
  estático incluye ramas no tomadas).

---

## 12. Reglas obligatorias de rendimiento, comentarios y port a asm

Estas reglas son de obligado cumplimiento y `AGENTS.md` enruta aquí.

### 12.1 Regla permanente de rendimiento

- Todo código nuevo debe minimizar el trabajo total por frame y reutilizar datos, trabajos, buffers y estados siempre que sea posible.
- La CPU debe limitarse a decidir cambios y programar hardware; evitar que haga copias, divisiones, módulos, recorridos o reconstrucciones repetidas que puedan resolverse incrementalmente, por lotes o mediante el Blitter/Copper.
- Antes de aceptar una solución, buscar explícitamente algoritmos O(1) o O(n) frente a colas O(n²), fusionar operaciones compatibles y reducir el número real de accesos al Blitter y de esperas síncronas.
- Medir los picos con profiling y telemetría en el caso límite, no solo validar que el frame nominal funcione; cualquier optimización debe conservar la corrección visual y el presupuesto de Chip RAM.
- **Criterio retro del chipset (68000)**: preferir algoritmos **rápidos y exactos** a lentos y precisos. Un algoritmo que subestima ~6 en `isqrt` pero cuesta 10 ciclos gana a uno exacto que paga `__divsi3`/`__mulsi3`. Para el hot path, lo deseable es aritmética 16-bit nativa (`muls.w`/`divs.w`), bucles countdown que emiten `dbra`, cero divisiones runtime, cero floats y cero STL.
- **Comprobar el ensamblador generado**: al portar o escribir APIs, revisar con `-S`/`-fverbose-asm` que el código que emite el toolchain no sea peor que el original (o que el asm a mano del repo de origen). Un port 100 % «fiel pero lento» pierde contra el original 68k optimizado; si el original usaba una optimización en asm (p. ej. `swap` para rotar, `lsl.l #8; add` para `<<9`, `divs`/`divu` de 16 bits), verificar que nuestra versión C++ produce algo al menos igual de eficiente y anotarlo en la bitácora (§8).
- **Nivel de optimización obligatorio: `-O2`** (release) y `-O1` (regresión). **`-Os` está prohibido** mientras no se resuelva el bug de codegen de §11.1b: en este engine `-Os` deshace la abstracción (llamadas a `ListBuilder::move`/`memset`/`memcpy` por operación) y costaba **2,3×** más que `-O2` en la 086. Cualquier cambio de nivel (`ENGINE_OPT`/`DEMO_OPT`/`C_OPT`) debe medirse con `measure-fps.mjs` y anotarse; no se acepta "código más pequeño" como criterio de rendimiento sin medición.

### 12.2 Comentario de optimizaciones

- Toda optimización que deje **rastros no canónicos** en el código —algo que un lector no esperaría para ese algoritmo: un `static_cast` o tipo raro, una escritura de 32 bits donde «tocaría» dos de 16, asm inline, un orden de operaciones forzado, un flag/parámetro que desactiva una ruta «natural», desenrollados, contadores de perfilado en el bucle caliente, etc.— debe llevar **un comentario breve que explique POR QUÉ** se hace así en vez de la forma más legible/natural en C++ (p. ej. «evita `__mulsi3`», «una sola escritura al registro custom porque cada acceso cuesta ~57 ciclos con `cpu_cycle_exact`», «se fija 1×/frame en vez de por línea»).
- Aplica a **todo** el código susceptible, no solo al nuevo: si detectas una optimización sin justificar (propia o preexistente), documéntala.
- El comentario debe ser **corto** y citar el mecanismo/coste concreto, no una explicación larga.

### 12.3 Port de rutinas calientes a asm

- En los ports de efectos del demoscene, **conservar siempre la versión C++ canónica** en el código (fiel al original, legible) **y portar las rutinas más calientes a ASM m68k copiándolas del original** (caso de referencia: `support/fire_loop.s`, fire-rgb, demo 080). El asm vive en `support/<nombre>.s` (gas, sin `.cfi`), con argumentos por memoria en un global `extern "C"` y un flag `K_<DEMO>_ASM` (default el camino seguro) que elige asm o C++. Cuando la ruta asm ya pasa el gate visual y la regresión, se puede invertir el default para que la demo compile con el asm (documentándolo en el README de la demo y dejando la ruta C++ accesible con `-DK_<DEMO>_ASM=0`).
- Motivo: g++ 15 ignora los pins `register ... asm("aN")` del original y no alcanza el codegen apretado (ver §9). El original sí lo consigue con variables de registro y macros inline.
- **Toda ruta asm debe validarse visual/estructuralmente** (secuencia de frames + `verify-*`), no solo por compilación: un `movea.l`/`lea`, un índice o una división mal portados cuelgan o deforman sin fallar el build. Referencia de errores vistos: `docs/debugging/` y la bitácora de este documento.

### 12.4 Sondas de codegen del camino 3D (`math3d`/`lib3d`/`object3d`)

El informe `tools/analyze/codegen-report.mjs` fija el asm del camino 3D en 68000, además del gate de libcalls (nada de `__mulsi3`/`__divsi3` ni 68020):

| Sonda | Qué cubre | Coste medido (68000, `-O2`) |
|---|---|---|
| `c_math3d_load_rotate` | `math3d::load_rotate` (Rx·Ry·Rz) genérico | ~30 instr + 1 `jsr` (con `sincos3`) |
| `c_object3d_update` | `update_object_transformation` (directa + inversa + cámara) | 20 instr + 1 `jsr` |
| `c_lib3d_facevis` | `update_face_visibility` (luz por producto punto) | 111 instr, 7 `muls.w`, 0 libcalls |
| `c_lib3d_transform` | `transform_vertices` (transform + `div_wide` + bbox) | 87 instr, 0 libcalls |

- El producto q12×q12 va a `Fixed<s32,24>` (**8.24**, `wide<s16> = s32`) y normaliza **una vez** por `dot`/`mul_norm`; en 68000 es `muls.w`, nunca `__mulsi3`.
- **`sincos3` compartido**: `update_object_transformation` calcula `(sin, cos)` por eje una sola vez y los reutiliza en la matriz directa y la inversa (`sincos(-a) = (-sin a, cos a)`, exacto porque la tabla es impar/par), ahorrando 3 lecturas de tabla por frame. La exactitud la fijan HOST-050/051 y la tabla dorada HOST-053.

## 13. Relleno CPU de polígonos: dos cadenas + palabra por plano

El relleno CPU (`Playfield::fill_polygon`) trabajaba en **dos bucles anidados de coste alto**:

1. **Por scanline, recorría TODAS las aristas** recalculando el mínimo/máximo de `x` → O(lados·altura).
2. **Por píxel**, llamaba a `write_pixel` → un `write_planes` (RMW en cada plano) por píxel.

Ambos se han reducido con la forma amiga del raster (el relleno es de **polígonos convexos**):

- **`convex_spans`** (`eng/core/data/polygon.hpp`) recorre el polígono por **dos cadenas** (izquierda/derecha desde el vértice superior al inferior) y emite `(y, xl, xr)` con coste **O(altura)**. Evita reapuntar la arista por cada fila y está validado contra el barrido de referencia (HOST-013).
- **`Playfield::draw_span`** escribe un tramo con **una palabra por plano** (16 píxeles) y máscara solo en los extremos parciales; en vez de 16 `write_planes` por cada 16 píxeles hace **1**. `fill_polygon`, `Surface::fill_rect` y los tramos horizontales de `Surface::draw_line` lo usan.

Efecto esperado en el número de `write_planes` (trabajo, no ciclos): para un relleno de `W` píxeles de ancho y `H` de alto baja de `W·H` a `⌈W/16⌉·H` por plano (~**16×** menos RMW del bitmap cuando el ancho cubre palabras enteras).

> Medición en objetivo pendiente: requiere el emulador (perfil por secciones). **Banco correcto**: la demo 078 **no** usa `Playfield::fill_polygon` (tiene su propio `Canvas`); la ruta cambiada la consume `Surface` (HOST-046 o un demo de GUI). Medir ahí, no en la 078. El contador determinista de `write_planes` y la equivalencia de píxeles los fijan HOST-045 y HOST-046.




