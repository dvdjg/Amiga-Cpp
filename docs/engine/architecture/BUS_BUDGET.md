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

## Reparto vertical: la ventana de VBlank (medido)

El bus **no es uniforme a lo largo del frame**: el DMA de bitplanes solo existe en la ventana
visible. Medido con el grid DMA del frame profiler (227×313 celdas, una por slot) en la demo 218
contra su referencia:

| Zona (PAL, display en línea 44) | Líneas | Slots libres/línea | Slots libres |
|---|---|---|---|
| Borde superior (VBlank) | 0-43 | ~215 | ~9,4k |
| Ventana visible (4 planos + Copper denso) | 44-267 | ~41 | ~9,2k |
| Borde inferior (VBlank) | 268-312 | ~215 | ~9,7k |

Los dos bordes forman **un hueco continuo de ~89 líneas (~19k slots libres)** donde el Blitter
corre a ~1 slot/2 ciclos; en la ventana compite con la CPU por los ~41 slots libres de cada línea
y baja a ~1 slot/4-8 ciclos. Medido con el mismo job de BOB: **3,9 ciclos/slot en el borde vs 8,3
en la ventana (2×)**.

**Técnica de planificación**: empaquetar los blits pesados en el hueco del VBlank y dejar la
ventana visible para el trabajo de CPU. La referencia de la 218 mete 17,5k de sus 23k slots de
Blitter en los bordes (densidad de bus total 98,1 %); un port con los mismos blits repartidos por
la ventana los ejecuta a la mitad de velocidad y no cabe en un campo. En A500 sin Fast RAM el
**código también compite**: cada fetch de instrucción desde Chip es un slot (medido: 9,9k
slots/campo de CPU frente a 4,7k de la referencia, que además pasa menos tiempo en esperas).

**Implicación para el modelo**: el presupuesto por frame es una **cota inferior** de coste — dos
escenas con los mismos slots totales pueden diferir 2× en tiempo de Blitter según dónde caigan.
La extensión natural de `BusBudgetInput` es declarar las palabras de Blitter que corren en el
blanco y las que caen en la ventana (con sus capacidades y velocidades respectivas) y avisar
cuando el trabajo pesado no quepa en el hueco. Datos y método completos:
`docs/debugging/investigaciones/218-campana-rendimiento.md`.

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

**Preflight de `App::start()`**: si el juego declara `GameDisplay::bus` (franjas + Blitter + Copper +
CPU), `App::start()` calcula el presupuesto y **falla rápido** con `StartError::BusOverBudget` si la
escena no cabe, antes de componer. Con `bands_count == 0` se usa una franja derivada del display
(ancho/alto/planos) para no aceptar a ciegas un modo que ya satura el bus. Si el juego no declara
Blitter/Copper/CPU, el preflight es una **cota inferior** (solo display), no una validación completa.

## Uso iterativo

1. Rellena lo que ya conozcas (resolución, planos, longitud de copperlist, palabras de Blitter…).
2. Llama a `amiga500_bus_budget` y mira `remaining_pct10`, `bottleneck` y `hints`.
3. Ajusta **una** variable (baja `width` a 288, acorta el Copper, sube `fps` de 25 a 50, activa Fast
   RAM…) y vuelve a calcular hasta dejar el margen en 0–8 % (máquina al 100 %).

El modelo captura la contención entre franjas y entre recursos; afinar los coeficientes de Copper y
Blitter con medidas concretas de cada lista/blit es el siguiente paso natural.
