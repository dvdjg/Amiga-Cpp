# C2P por Blitter (chunky → planar)

La CPU escribe mucho más rápido en **chunky** (1 byte/índice por píxel, contiguo) que en **planar** (el formato que dibuja el chipset Amiga, 1 bit/píxel por plano). El **C2P** convierte chunky→planar con el **Blitter**, dejando a la CPU el trabajo "fácil" (escribir el buffer chunky) y al hardware el "caro" (repartir los bits en planos). Este documento cubre el C2P 4bpp del efecto `fire-rgb` (demo 080) y la API del engine que lo porta.

```
 chunky (CPU escribe)            planar (Denise dibuja)
 ┌───┬───┬───┬───┐              plano0 │b7 b6 b5 b4 b3 b2 b1 b0│ plano1 │...│
 │ 3 │ 1 │ 0 │ 2 │  (1 byte)     plano2 │...                     plano3 │...│
 └───┴───┴───┴───┘              (el píxel = (bit0,bit1,bit2,bit3) de los 4 planos)
```

## 1. Idea del algoritmo (4 bpp)

Se parte de un buffer con 4 bits por píxel empaquetados como **nibbles** y se generan **4 bitplanes**. El Blitter no sabe de píxeles: trabaja con **palabras de 16 bits**. Así que el C2P hace **dos pasadas de "byte swap"** (reordena los bytes/nibbles para juntar los bits de un mismo plano) y luego **reparte** a cada plano. Todo con `ASHIFT` (barril) + minterms + modulos.

Detalle clave: la CPU escribe **palabras de color** (no nibbles); el `scramble` de `dualtab` ya reparte los bits de R/G/B de cada color en 4 píxeles HAM, y el C2P los empaqueta en planos.

## 2. Las 13 fases (`ChunkyToPlanar`)

`fire-rgb` divide la conversión en 13 pasos encadenados por **interrupción de blit** (cada blit terminado dispara el siguiente). Parámetros: `src = chunky`, `dst = chunky + BLTSIZE` (la segunda mitad del mismo buffer), `bpl[]` = los 4 punteros de plano; `BLTSIZE = 10240` bytes.

| fase | qué hace | bltcon0 | bltcon1 | mods / datos | punteros |
|---|---|---|---|---|---|
| 0 | swap 8×4, pasada 1 (adelante) | `(A+B+D)|0xE4|ASHIFT(8)` | 0 | `amod=bmod=dmod=4`, `cdat=0x00FF` | A=`src+4`, B=`src`, D=`dst` |
| 1 | dispara 2.º blit (mismo) | — | — | — | — |
| 2 | swap 8×4, pasada 2 (atrás) | `(A+B+D)|0xD8|ASHIFT(8)` | `BLITREVERSE` | — | A=`src+N-6`, B=`src+N-2`, D=`dst+N-2` |
| 3 | 2.º blit | — | — | — | — |
| 4 | separa nibbles → plano 0 | `(A+B+D)|0xE4|ASHIFT(4)` | 0 | `amod=bmod=6`,`dmod=0`,`cdat=0x0F0F` | A=`dst+2`, B=`dst`, D=`bpl[0]` |
| 5 | 2.º blit | — | — | — | — |
| 6 | → plano 2 | idem 4 | 0 | idem | A=`dst+6`, B=`dst+4`, D=`bpl[2]` |
| 7 | 2.º blit | — | — | — | — |
| 8 | → plano 1 (atrás) | idem 2 (`ASHIFT(4)`) | `BLITREVERSE` | idem | A=`dst+N-8`, B=`dst+N-6`, D=`bpl[1]+BPLSIZE-2` |
| 9 | 2.º blit | — | — | — | — |
| 10 | → plano 3 (atrás) | idem 8 | `BLITREVERSE` | idem | A=`dst+N-4`, B=`dst+N-2`, D=`bpl[3]+BPLSIZE-2` |
| 11 | 2.º blit | — | — | — | — |
| 12 | parchea `BPLxPT` de la copper | — | — | — | `bpl[3],bpl[2],bpl[1],bpl[0]` |

Cada par (n, n+1) es UN blit: la fase par configura y arranca (`bltsize`), la impar vuelve a escribir `bltsize` (el mismo) para "re-disparar" tras el `ClearIRQ(INTF_BLIT)` de la IRQ. La fase 12 no blitea: reordena los punteros de plano en la copper (el C2P escribe los planos al revés).

## 3. Los minterms: la convención que causó el bug

El minterm es una función de **A/B/C** (8 combinaciones); cada bit dice si D=1. La nomenclatura del repo es `ABC`=A&B&C, `NABNC`=~A&B&~C, etc. **Ojo**: cada letra es un canal (A/B/C) y el `N` delante la niega.

- **`0xE4` = ABC|ANBC|ABNC|NABNC = (A&C)|(B&~C)** → "toma de A los bits donde está la máscara C, y de B donde no". Es el swap de la fase 0/4.
- **`0xD8` = ABNC|ANBNC|ABC|NABC = (A&~C)|(B&C)** → el inverso (fase 2/8).
- **`0xCA`** = cookie-cut `D=(A&B)|(~A&C)` (el C2P no lo usa; sí otros blits del engine).

**El bug que costó horas**: se portó `0xE4` como `0xE2` (se escribió `NANBC`—bit 1— en lugar de `NABNC`—bit 2—). Con `0xE2` el minterm pasa a `~A&~B&C`, que **escribe la máscara `bltcdat` donde la fuente es 0** → la pantalla salía "ya rellena" de rayas (0x00FF/0x0F0F) incluso con el fuego a cero. Moraleja: al portar minterms, **recalcular el valor con la tabla de la convención**, no copiarlo a ojo.

## 4. Las máscaras `BLTCDAT` y `ASHIFT`

- `bltcdat` alimenta el canal C (que no se lee de memoria; `SRCC` está apagado). Funciona como **máscara de selección** de bytes (`0x00FF`) o de nibbles (`0x0F0F`).
- `ASHIFT(8)` / `ASHIFT(4)` activan el **barril** del Blitter: desplaza A para alinear el byte/nibble que se quiere intercalar. `(Val<<12)` en `bltcon0`.

## 5. Síncrono vs interrupción de blit

- **Original**: la IRQ de blit llama a `ChunkyToPlanar`; cada llamada ejecuta la siguiente fase → el C2P avanza **en background** mientras la CPU calcula el fuego del siguiente frame (solape → más fps).
- **Port (actual)**: `MinimalBackend::c2p_4bpp_step(state)` ejecuta **una fase y espera** al Blitter. La demo llama las 13 fases en bucle → **síncrono** (misma imagen, sin solape). El hook de IRQ queda pendiente.

## 6. API del engine

```
struct MinimalBackend::C2p4State {
    u8   phase;             // 0..12
    u8*  chunky;           // buffer (mitad src, mitad dst)
    u8*  planes[4];        // punteros de bitplane
    u16  bytes;            // BLTSIZE (10240 en fire-rgb)
};
bool MinimalBackend::c2p_4bpp_step(C2p4State& s);   // una fase + wait
void MinimalBackend::set_bitplane_dat(u8 plane, u16 v); // BLTxDAT (bits HAM fijos)
```

El buffer `chunky` se usa como **origen y destino** (la segunda mitad es el intermedio planar), igual que en el original; por eso su tamaño es `2 * BLTSIZE`.

## 7. Rendimiento

- El C2P son **12 blits** de 2 palabras × 640 líneas cada uno (`bltsize = 2|(640<<6)`), más la fase de parcheo. El Blitter los hace en paralelo a la CPU, pero el port síncrono los **espera**.
- **Hallazgo (medido)**: los pares (fase par = configura+arranca, impar = re-escribe `bltsize`) **NO son redundantes**. Saltarse las impares rompe el C2P (salida con 4 colores, sin fuego): muy probablemente el Blitter deja sus registros de puntero **avanzados** y la fase impar procesa el **bloque siguiente** con el mismo `bltsize`. Por eso hay 12 blits, no 6.
- **Coste medido del C2P (fire-rgb, emulador)**: con C2P activo la demo avanza ~22 frames en el tramo medido; **saltándolo, ~26** → el C2P pesa **~15%** del frame. Un solape perfecto por IRQ recuperaría como máximo ese ~15%; el resto es la simulación del fuego (y el techo del emulador, ~10-11 fps).
- Optimizaciones pendientes: (a) **encadenar por IRQ de blit** (solape con el fuego, como el original) → ~15%; (b) **recuperar el bucle de fuego en asm** (mayor palanca que el C2P); (c) `c2p_1x1_4.s` (Kalms/Scout, en `support/`) para el caso genérico 4bpp/1byte.

## 8. Referencias

- Origen: `demoscene-repo-orig/effects/fire-rgb/fire-rgb.c` (`ChunkyToPlanar`).
- Port ASM genérico: `support/c2p_1x1_4.s` (`demoscene-repo-orig/lib/libgfx/c2p_1x1_4.asm`).
- Demo que lo valida: `demos/amiga/080_fire_rgb` (+ plan `docs/demos/effects/FIRE_RGB_PORT_PLAN.md`).
