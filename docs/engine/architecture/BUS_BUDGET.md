# Presupuesto de bus DMA del Amiga 500

Herramienta de **planificación** (`eng/hw/bus_budget.hpp`) que estima si un juego cabe en el bus de
Chip RAM de un A500 (OCS) y cuánto margen deja para cada recurso. **No es una medida**: es un modelo
de ingeniería para dimensionar antes de programar. Los números reales dependen de `DDFSTRT`/`DDFSTOP`,
de la contención exacta del Blitter y del modo de fetch; este modelo los aproxima con los valores
del AHRM y la práctica de la época.

## Qué compite por el bus

El A500 reparte un número fijo de **slots de bus por línea** de raster (~227) entre los consumidores
de Chip RAM. Los slots **impares (odd)** están reservados a DMA crítico de tiempo real; los **pares
(even)** los comparten **Copper > Blitter > CPU** por prioridad.

```
                    odd (reservado DMA)              even (compartido)
   ┌───────────────────────────────────┬────────────────────────────────────┐
   │ refresh(4) │ disk(3) │ audio │ spr  │ Copper(2/MOVE,3/WAIT) │ Blitter │ CPU │
   │            │         │ 4×1  │ 8×2  │        (prioridad alta)          │     │
   └───────────────────────────────────┴────────────────────────────────────┘
              bitplanes: 20 palabras/plano (lores 320) ... y con >4 planos (o hires) roban even
```

| Recurso | Slots típicos / línea | Notas |
|---|---|---|
| Refresh | 4 | fijo, odd |
| Disco (Paula) | 3 | streaming de floppy, odd |
| Audio (Paula) | 4 (1/canal) | odd |
| Sprites (8 canales) | 16 (2/canal) | odd; reutilizables por línea |
| Bitplanes | `ceil(width/16)` (lores) o 40 (hires) por plano | odd; >4 planos o ≥4 hires roban even |
| Copper | 2 por MOVE, 3 por WAIT | solo even |
| Blitter | 1–4 por palabra (lecturas de canal) | solo even; puede monopolizar la CPU |
| CPU 68000 | el resto de even | solo Chip RAM; Fast RAM alivia el bus |

Totales orientativos por frame: PAL ≈ 227×312 ≈ 70 800 slots; NTSC ≈ 227×262 ≈ 59 500. Con **6
planos lores** o **4 hires** el bitplane DMA roba ~50 % de los slots even, y por eso los juegos
clásicos bajaban a **288×240**, reducían a **25 fps** o recortaban la copperlist.

## API

```cpp
#include <eng/hw/bus_budget.hpp>

eng::hw::BusBudgetInput in {};
in.pal = true;
in.fps = 50u;
in.blitter_words = 8000u;      // BOBs + fills + copias por frame
in.blitter_channels = 4u;      // cookie-cut
in.cpu_chip_cycles = 15000u;   // ciclos 68000 que tocan Chip (0 si Fast RAM)
in.bands_count = 1u;
in.bands[0] = eng::hw::BusBand {.height = 256u, .width = 320u, .bitplanes = 4u};

const eng::hw::BusBudgetResult r = eng::hw::amiga500_bus_budget(in);
// r.used_slots / r.remaining_slots / r.remaining_pct10 (décimas de %)
// r.bottleneck (BusResource) / r.hints (BusBudgetHint)
```

Soporta **hasta 4 franjas** horizontales con modos independientes (`BusBand`: altura, ancho, planos,
hires, sprites, Copper local, actualización cada frame o cada dos). El cálculo **acumula** el coste
de todas y devuelve qué recurso queda más ahogado (`bottleneck`) y qué variables siguen libres
(`remaining_pct10` alto + `hints`).

`BusBudgetHint` resume los trucos de la época como bits: `kHintSixPlanesWide` (6 planos >288 px),
`kHintHiresHeavy` (≥4 hires), `kHintCpuNeedsFast` (la CPU en Chip se come >50 %), `kHintLowerFps`,
`kHintTight` (<8 % libre), `kHintOverBudget`.

## Uso iterativo

1. Rellena lo que ya conozcas (resolución, planos, longitud de copperlist, palabras de Blitter…).
2. Llama a `amiga500_bus_budget` y mira `remaining_pct10`, `bottleneck` y `hints`.
3. Ajusta **una** variable (baja `width` a 288, acorta el Copper, sube `fps` de 25 a 50, activa Fast
   RAM…) y vuelve a calcular hasta dejar el margen en 0–8 % (máquina al 100 %).

El modelo captura la contención entre franjas y entre recursos; afinar los coeficientes de Copper y
Blitter con medidas concretas de cada lista/blit es el siguiente paso natural.
