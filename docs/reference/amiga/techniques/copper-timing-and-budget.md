# Copper, timing y presupuesto de bus en el A500

Referencia local de optimización para Amiga (OCS/A500), sintetizada de `amiga-bootcamp/`
(`17_demoscene/timing_optimization.md`, `17_demoscene/copper_effects.md`,
`01_hardware/ocs_a500/copper.md`) y **corregida** donde el original confunde unidades, más lo
**medido** en este repo. Es la fuente única para optimizar efectos dependientes de raster.

## 1. Presupuesto de tiempo (PAL)

- Reloj de CPU 68000: **7.09379 MHz**. Campo PAL ≈ 20 ms → **141 876 ciclos CPU por campo**
  (constante del engine). El original del bootcamp cita «19 968 ciclos/frame», pero eso son
  **ranuras de bus** (312 líneas × 64), no ciclos de CPU; usar 141 876.
- El bus es **compartido** entre CPU, Copper, Blitter, bitplane/sprite/audio DMA. Con 4 planos
  LoRes el DMA roba ~40 % del bus; con **6 planos**, ~60 % → la CPU dispone de menos ranuras.
- **`BLTPRI` (Blitter Nasty)**: prioridad total al Blitter → la CPU se queda sin ranuras; usar
  solo si no hay trabajo de CPU útil.

## 2. La Copper

- Tres instrucciones, cada una **32 bits** (2 palabras): `WAIT`, `MOVE`, `SKIP`. Solo escribe
  registros `$DFF0xx`; **no** lee ni accede a memoria.
- La copperlist vive **en Chip RAM** y termina en `$FFFF,$FFFE`.
- **Presupuesto por línea** (6 planos LoRes): ~52 pares `WAIT+MOVE` (≈210 ranuras libres tras
  bitplane/sprite/audio). **Una `COLORxx` por banda cada 8 líneas está muy por debajo**: la
  Copper no es el cuello.

## 3. Coste de instrucciones 68000 (base, sin contención)

| Instrucción | Ciclos | Uso típico |
|---|---|---|
| `MOVE.W Dn,Dn` | 4 | registrar |
| `MOVE.W (An),Dn` / `Dn,(An)` | 8 | lectura/escritura a memoria |
| `MOVE.L (An)+,(An)+` | 12 | copia |
| `MULS.W`/`MULU.W` | 28 | 16×16→32 |
| `DIVS.W` | 44–140 | evitar en bucles |
| `DBRA` (taken/salir) | 10 / 6 | bucles |
| `MOVEM.L D0-D7,(An)` | ~ 8×8 rápido | ráfaga (4× vs `MOVE.W` en bucle) |

**Contención**: con DMA activo, `coste efectivo ≈ base + stalls` (accesos a Chip RAM pagados
varias veces). `Fast`/`Slow` RAM (no DMA) ~30 % más rápida para código/tablas.

## 4. Técnicas de optimización (ordenadas por impacto)

1. **Parchear la copperlist (SMC)**, no reconstruirla: plantilla con *placeholders* y por frame
   escribir solo las palabras que cambian (patrón estándar demoscene). Reduce escrituras de
   **estructura** (waits, registros), **no** las palabras de **dato** que cambian.
2. **Mover cómputo pesado a VBlank** (sin bitplane DMA → menos contención).
3. **Código y tablas en Fast/Slow RAM** si existen.
4. **`MOVEM`** para copias; accesos secuenciales (aprovechar el prefetch).
5. **Evitar `DIVS`** (tabla de recíprocos → `MULS`).

## 5. Lo medido en este repo (demo 125, `layers_dualpf`)

Dual playfield 6 planos con gradientes por banda. Aislado con `kBands` (perfilado):

| Caso | fps | ciclos/frame |
|---|---|---|
| Lista fija (sin reconstruir) | **49.92** | 142 102 (1.0 campo) |
| Reconstruir estructura (waits), **sin** color por banda | **50.09** | 141 631 (1.0 campo) |
| Efecto completo (con color por banda) | **32.5** | ~218 000 (1.5 campos) |

**Conclusión**: montar la copperlist (estructura/waits) **no** es el cuello; lo son las
**~700 palabras de color por frame** (11 colores × ~64 bandas) escritas en Chip RAM durante el
display. Es un coste de **dato**, que el parcheo **no** elimina (los colores cambian cada banda).
Palancas reales: (a) menos bandas/colores (coste de fidelidad), (b) escribir el dato más barato
(p. ej. construir en RAM sin DMA y copiar, si hubiera), o (c) asumir 1.5 campos aquí y 1 campo
en hardware real (el modelo de contención del emulador puede ser conservador).

**El runner SÍ es ciclo-exacto** (`out/run/<demo>/<cfg>/runner.uae`: `cpu_cycle_exact=true`,
`cpu_memory_cycle_exact=true`, `blitter_cycle_exact=true`, `cycle_exact=true`). El contador
`$B7E928` es libre y **fiable**: calibrado con un bucle trivial de 10 000 iteraciones (RMW
volátil) da **956 336 ciclos ≈ 95 ciclos/iteración** — imposible en HW ideal (~10), pero normal
en un **A500 (código en Chip RAM) con 6 planos de DMA**: cada instrucción y cada acceso compiten
con el bus y el CPU queda hambreado (~5–10×).

Eso reencuadra el problema: **no es "el volumen de 1 KB"**. Desglosado con el conmutador de
perfilado de la demo (`kBands`, y aislando el bucle):

| Sección de `build_copper` | ciclos | fps resultante |
|---|---|---|
| Solo base (modo display + punteros + paleta base, sin bucle) | ~28 600 | 50.09 |
| + estructura (bucle de bandas: waits) | ~138 000 | 50.09 |
| + datos de color por banda (efecto completo) | ~230 000 | 32.5 |

1 KB de copperlist "debería" ser trivial (~512 palabras × ~20 ciclos ≈ 10 k en HW ideal). En la
práctica, con el CPU hambreado, **cada acceso ronda ~100 ciclos** y el build hace **miles** de
accesos (bucle de 256 líneas + ~64 waits + ~700 escrituras) → ~1.5 campos. La técnica demoscene
ataca justo esto: **asm apretado (menos instrucciones) + parchear (menos accesos) + hacerlo en
VBlank + código/tablas en Fast/Slow RAM**. Nuestro build está en C++ y con muchos accesos, así que
paga el peaje de Chip RAM ×2.

**El Scheduler tenía además un coste fijo** (construir la timeline por frame): merece revisarse
(la base sola ya cuesta ~28 k).

**Corrección aplicada (2026):** ese coste fijo era la `Timeline` del `Scheduler`, que se
**inicializaba a cero (2×256 B) en cada construcción** — y como el `Scheduler` se crea por frame
en la pila (Chip RAM), costaba **~25 k ciclos/frame solo en borrar memoria**. Arreglado: la
`Timeline` ahora **no borra sus contadores** en el constructor (bitset de líneas tocadas de 32 B;
`finish()` ignora las no tocadas). Medido en la demo 125: **32.5 → 42.6 fps** con `Scheduler`
(y 44.5 con `ListBuilder` directo). Lección general: **no construir/limpiar objetos grandes en el
hot path**; usar inicialización perezosa o almacenamiento del llamador. Se añadió además una
librería de punteros no propietarios sin heap (`eng/core/ptr.hpp`: `Ref`/`NonNull`/`Opt`).

**MOVEM/ráfaga**: en la copperlist las palabras van intercaladas `[reg, dato, …]` → los datos
**no** son contiguos (stride 4 B); un `memcpy`/`movem` reescribiría también los registros (más
palabras). No es la palanca mientras el cuello sea "muchos accesos × ~100 ciclos".

**VBlank**: en PAL ~56 líneas fuera del área visible; ahí el bitplane DMA está apagado y los
accesos son baratos. Construir ahí (o lo antes posible tras el WAIT de VBlank) reduce el peaje;
pero el build completo (~230 k) no cabe en el hueco de VBlank (~26 k) tal cual → hay que bajarlo
(parcheo/tight loop) para que quepa.



## 6. Referencias

- `amiga-bootcamp/17_demoscene/timing_optimization.md`, `copper_effects.md`
- `amiga-bootcamp/01_hardware/ocs_a500/copper.md`
- AHRM 3.ª, cap. 6 (Copper); `docs/reference/ahrm/`
- `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` §13 (relleno CPU)
- `demos/amiga/125_layers_dualpf/` (caso medido)
