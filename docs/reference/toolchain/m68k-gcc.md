# Toolchain m68k-amiga-elf (GCC) — defectos conocidos y verificación

Defectos **reproducidos** del compilador del toolchain (`m68k-amiga-elf-g++`) que afectan al engine, con
el **caso mínimo** y cómo **re-verificar en cada actualización de versión**. Ver `AGENTS.md` §1.11
(la implementación es la referencia de facto) aplicado al compilador.

> **Versión auditada**: GCC **15.1.0** (`vscode-amiga-debug/bin/win32/opt/bin/m68k-amiga-elf-g++`,
> ver `--version`). **Al subir de versión, ejecutar la sección «Re-verificación»** y actualizar el
> resultado aquí (y los `#pragma`/workarounds que estos defectos obligan a mantener en el engine).

## 1. ICE de CFI con `constexpr`/lambdas a `-O0`

**Síntoma**:
```
internal compiler error: in dwarf2out_frame_debug_adjust_cfa, at dwarf2cfi.cc:1348
```

**Cuándo**: al compilar a **`-O0`** (el modo **debug** de las demos, `tools/build/build-demo.sh`) código
con **lambdas locales** o **bucles `constexpr`** no triviales. A `-O1`/`-O2` **no** ocurre.

**Casos mínimos reproducidos**:
- Un `world_gen.hpp` con **lambdas locales** en un cuerpo grande (`dwarf2cfi.cc:1348`).
- Un **bucle de resta binaria** (`udiv_u32`, `div_norm<Fixed<s32>>`) → ICE también en `arith.hpp`/`fixed.hpp`.

**Workarounds usados**:
- **No usar lambdas locales** en código que se compile a `-O0`; recorrer vecinos con
  `eng::util::Graph::neighbor_count`/`neighbor_at` (sin callback).
- **`#pragma GCC optimize("O1")`** a nivel de función **no sirve para `constexpr`** (el compilador lo
  ignora con esa combinación; el ICE persiste). Solo funciona en funciones no-`constexpr` (p. ej. la
  variante `udiv_u32` no-`constexpr`).

**Impacto en el engine**: obliga a escribir el código de algoritmo **sin lambdas locales** y a no
apoyarse en `constexpr` para rutinas con bucles `-O0`-hostiles (p. ej. división larga).

## 2. Rutinas de soporte (`libgcc`) en `-nostdlib`

**Síntoma**: el enlazado falla con **símbolos indefinidos** (`__mulsi3`, `__udivsi3`, `__modsi3`,
`__divsf3`, `__floatsisf`, `__muldi3`, …). El engine compila con **`-nostdlib`**, así que **no hay
libgcc**: toda operación que el compilador resuelva con una *libcall* **rompe el build**.

**Cuándo**: `u32`/`s32` `*`, `/`, `%`; `float` (soft-float); `u64 *`; `Fixed<s32>` con `div_norm`.
**`u16` `*`/`/`** son nativos (`mulu.w`/`divu.w`) y **no** generan libcall.

**Cómo detectarlo**:
```bash
<toolchain>/opt/bin/m68k-amiga-elf-g++ -std=gnu++23 -m68000 -nostdlib -O0 \
  -I engine/include -I <sys-include> -c tu.cpp -o tu.o
nm tu.o | grep -E '__mul|__div|__mod|__float|__fix'
```

**Regla del engine**: nada de `float`/`u32 *`/`u32 %` en la ruta que corre en Amiga; `u16` para
aritmética y tablas; `Fixed<s16,E>` (E ≤ 15) en vez de `Fixed<s32,E>`; ver `AGENTS.md` §1.10 y
`tools/analyze/asm-audit.mjs`.

## Re-verificación (al actualizar GCC)

Script reproducible (adaptar rutas). Falla si **alguno** de los dos defectos ya no se reproduce (¡buena
noticia: quitar el workaround!) o si **aparece** otro:

```bash
GX=<toolchain>/opt/bin/m68k-amiga-elf-g++
SDK=<toolchain>/opt/m68k-amiga-elf/sys-include
# (a) ICE de CFI: un lambda local + bucle a -O0
cat > /tmp/ice.cpp <<'EOF'
int f(int n) { int s = 0; auto add = [&](int x) { s += x; }; for (int i = 0; i < n; ++i) add(i); return s; }
EOF
"$GX" -std=gnu++23 -m68000 -nostdlib -O0 -c /tmp/ice.cpp -o /tmp/ice.o 2>&1 | grep -i 'internal compiler error' && echo "DEFECTO 1 PRESENTE" || echo "defecto 1 resuelto"
# (b) libcalls prohibidas
cat > /tmp/lc.cpp <<'EOF'
unsigned f(unsigned a, unsigned b) { return a * b + a / b + a % b; }
EOF
"$GX" -std=gnu++23 -m68000 -nostdlib -O0 -c /tmp/lc.cpp -o /tmp/lc.o && nm /tmp/lc.o | grep -E '__mul|__div|__mod' && echo "DEFECTO 2 PRESENTE (esperado)" || echo "defecto 2 resuelto"
```

## 3. Riesgo: las optimizaciones propias pueden OCULTAR defectos

Las libcalls (`__mulsi3`, `__divsf3`…) son una **señal barata**: si aparecen, sabes que algo es caro o
incorrecto para `-nostdlib`. Al sustituirlas por rutinas propias (p. ej. `div_norm<Fixed<s32>>` sin
libgcc) **esa señal desaparece**, y con ella la detección pasiva de fallos. Un defecto puede colarse
sin que el build falle ni se vea una libcall en el asm.

**Estrategia por capas (ninguna basta sola):**

1. **Corrección → tests host + referencia.** Toda rutina propia (división larga, RNG, ruido) se valida
   contra una **referencia independiente** (la operación nativa o el algoritmo clásico), con casos
   límite; idealmente equivalencia **byte a byte** (patrón del repo: `zx0` vs compresor de referencia,
   `c2p` vs `c2p_1x1_naive`). Un número "parecido" no vale.
2. **Instrucciones inválidas → `asm-audit` (columna `bad020`).** Detecta **68020+/FPU** en el binario
   (`muls.l`, `fmove`, `extb.l`, `bf*`, `cas.l`…) que **cuelga en 68000/OCS**. Falla siempre (`exit 1`),
   aunque no haya libcalls. Es la salvaguarda que no depende de la señal antigua.
3. **Ineficiencia residual → `asm-audit --strict`.** Sigue contando libcalls/soft-float/shifts; si una
   rutina propia es **más lenta** que el libgcc que sustituye, el conteo de `calls` y el coste medido
   (`codegen-report.mjs`, `Timeline`) lo delatan.
4. **Criterio de diseño**: una optimización propia **debe** venir con su test de equivalencia y (si es
   caliente) con la medición A/B; sin eso no se acepta (AGENTS §1.5/§1.10, `OPTIMIZACION_GPP_68000` §12).

> Regla: **nunca** sustituir una libcall por una rutina propia sin (a) test de equivalencia contra una
> referencia y (b) comprobar que el binario sigue en 68000 (`asm-audit`). El silencio de las libcalls
> no es prueba de corrección.

## Referencias

- `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` (reglas de codegen y libcalls).
- `tools/analyze/asm-audit.mjs` (libcalls + `bad020` por función), `tools/analyze/codegen-report.mjs`.
- `docs/reference/emulators/` (mismo patrón, para el emulador).
