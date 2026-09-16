# Demo 061: c2p_1x1_4 (chunky 4bpp → planar) + rotozoom por CPU

> **ESTADO: NO FINALIZADA** (regla de demos atractivas de `AGENTS.md`). Es correcta,
> colorida y **verificada** (asm c2p y rotozoom byte-idénticos a la C++), pero la
> animación va a **~3 fps** a pantalla completa: el techo con 20.480 píxeles/frame es
> ~4-5 fps incluso con un bucle idealizado (ver «Rendimiento»). Para cumplir la regla
> hay que reducir área o pasar a píxeles gordos 2×2.

Demuestra el valor del **modo chunky**: en un Amiga OCS, un efecto por píxel escrito
en planar (un bit por plano) sería impagable. Aquí la CPU genera cada píxel en un
framebuffer **lineal de 1 byte/píxel** y `c2p_1x1_4` lo transpone a los 4 bitplanes
que lee el DMA de Agnus. Es la pieza que habilita en OCS todo el repertorio de
efectos type "PC" (rotozoom, plasma, túnel, fuego) sin pagar el coste planar.

## Qué se ve

Un **rotozoom** animado sobre una textura indexada 64×64, con paleta cíclica de 16
colores (negro → azul → cian → verde → amarillo → rojo → magenta → violeta). Rota,
late el zoom y la textura panea en diagonal; llena la pantalla (320×256) gracias al
`row_repeat=4` del driver: se generan 320×64 píxeles y el Copper repite cada fila 4
veces.

El efecto se genera a 320×64 (20.480 px, la mitad de una pantalla completa) porque el
`row_repeat` es gratis en vertical: es el mismo truco de la demo 080.

## Arquitectura

```
  update()                                    render() (tras VBlank)
  +--------------------------------+          +---------------------+
  | rotozoom_into<64,64>()         |          | install(copperlist  |
  |   textura -> chunky[back]      |          |   del buffer nuevo) |
  | c2p_1x1_4_asm()                |          +---------------------+
  |   chunky[back] -> planos[back] |
  +--------------------------------+          (swap de COP1LC, sin
             |                                 COPJMP1: el Copper
             v                                 recarga en VBlank)
      active ^= 1  -> doble buffer
```

- **Generador**: `eng/graphics/effects/rotozoom.hpp` (utilidad de engine, genérica y
  sin tamaños fijos). El bucle caliente es incremental: una vez por frame se calculan
  los pasos por píxel (`du/dv`) y por fila, y dentro del bucle solo hay sumas e
  indexado. Ver test host `tests/host/022_rotozoom`.
- **C2P**: `support/c2p_1x1_4.s`, port a GAS del c2p de Kalms/Scout (1999). Corre en
  **asm 68000** con la ABI del original (d0/d1/d5 + a0/a1).
- **Display**: `HamScene` paramétrico (4 planos, `bplcon0=0x4200`, `row_repeat=4`) con
  **doble buffer**: dos instancias del driver, una por buffer, y swap de copperlist
  tras VBlank (patrón anti-tearing de la 080).
- **Paleta**: 16 colores RGB444 (`kColors`); los planos 5/6 no existen (4 planos).

## Gate de equivalencia asm vs C++ (en `init`)

La demo convierte el mismo chunky con la **asm de Kalms** (la que se ve) y con la
**referencia portable C++** (`c2p_1x1_4`), y publica en `g_eng_run_status.detail` el
**nº de bytes distintos** (0 = idénticos):

```
node tools/debug/measure-fps.mjs 061_c2p_chunky_4bpl
  ... detail=0x0   ->  asm byte-identica a la C++
```

Dos fallos que este gate cazó y que el análisis por color **no** detecta (una
permutación de planos sigue dando índices/grises válidos): una escritura
`move.w d7,(a4)+` que faltaba en la cola de `.pix16` (el plano 1 salía basura) y la
ABI del port (por pila en vez de registros).

## Rendimiento — el bucle asm ya está, la fluidez sigue pendiente

El bucle del rotozoom vive en dos rutas: la **C++ canónica**
(`rotozoom_into`, camino seguro, `-DK_061_ASM=0`) y el **asm**
(`support/rotozoom_loop.s`, `K_061_ASM=1`, default). Ambas parten de los mismos
`RotozoomSteps` y el `init` de la demo comprueba que producen el **mismo buffer**
(`0x00006105` si difieren).

Medido con `measure-fps.mjs` (WinUAE-DBG, A500, `-O1`, ciclo-exacto):

| Tramo | Ciclos/frame | Notas |
|---|---|---|
| C2P asm (320×64) | ~60 k | 1.280 iteraciones de 16 px |
| Rotozoom C++ | ~3,12 M | **~152 ciclos/píxel** |
| Rotozoom asm | ~1,93 M | **~94 ciclos/píxel** (−38 % vs C++) |
| frame total (asm) | ~2,38 M | ~16,7 campos → **~3,0 fps** |

El asm va en dos pasos: primero (~117 c/px) solo movía el bucle a registros; después
(~94 c/px) mantiene la coordenada `u` **pre-escalada** (`u<<6`) para que extraer el
texel sea un `and` en vez de `and`+`lsl.w #6` (18 ciclos/píxel), con re-máscara a media
fila para no desbordar. El coste lo dominan las dos extracciones (`swap`+`and`), el
`move.b` indexado y los dos `add.l` del 68000.

**Techo**: incluso bajando a ~60 ciclos/píxel (un bucle idealizado), 320×64 seguiría en
~4 fps: con 20.480 píxeles/frame el presupuesto de 20 ms no da para más. Es decir,
**optimizar el bucle no basta para que sea fluida** a esta área; sirve para confirmarlo
con números.

**Sigue sin ser fluida.** Opciones, por orden de coste/beneficio:
1. **Ampliar con el display, no con la CPU**: generar 160×128 y mostrar 320×256 es
   imposible gratis en horizontal (el Copper solo repite filas), así que la vía real
   es **píxeles gordos** (2×2) o aceptar media anchura.
2. **Bajar el área/resolución**: 160×64 (media anchura) da ~5 fps; 160×32, ~8-9 fps.
3. **Optimizar el bucle asm**: mantener los acumuladores ensanchados para que la
   extracción del índice sea un `and` (sin `swap`+`lsl`), o precalc con tablas por
   fila. Techo estimado ~50-60 ciclos/píxel → ~5-6 fps a pantalla completa.
4. **Cambiar de efecto** a uno por celda/bloque con menos píxeles por unidad de
   trabajo, manteniendo el C2P como protagonista.

Mientras no se decida, la demo **no cumple** la regla de demos atractivas (animación
fluida) y así queda documentado.

## Build & run & analyze

```bash
tools/build/build-demo.sh demos/amiga/061_c2p_chunky_4bpl --clean
tools/run/run-demo.sh       demos/amiga/061_c2p_chunky_4bpl
tools/analyze/analyze-demo.sh demos/amiga/061_c2p_chunky_4bpl
```

## Criterio de aceptación (automático)

- Compila y llega a `Ready` por canal lateral.
- `g_eng_run_status.detail = 0` (asm byte-idéntica a la referencia C++).
- La captura es **colorida y llena la pantalla**: `verify_c2p_color.js` exige ≥12
  colores distintos, ≥55 % de píxeles no negros y ≥30 % de píxeles con croma real
  (`OK c2p: colores=15 croma=71300/71300 cobertura=71300/108864`).

## Relación con la ingesta

Primer entregable de la Oleada 1 (`libgfx`) sobre la nueva estructura del engine.
Ver `docs/demos/effects/OLEADA1_LIBGFX_INVENTARIO.md`.
