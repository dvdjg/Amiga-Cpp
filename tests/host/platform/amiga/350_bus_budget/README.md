# HOST-350: presupuesto de bus DMA del Amiga 500 (`eng/hw/bus_budget.hpp`)

Herramienta de **planificación** (no de medida) que acumula los recursos que compiten por el bus de
Chip RAM del A500 y devuelve el **margen restante**, el **cuello de botella** y **pistas** para
iterar. Ver [`BUS_BUDGET.md`](../../../../docs/engine/architecture/BUS_BUDGET.md).

## Qué comprueba

1. `total_slots` = 227 slots/línea × 312 líneas (PAL) × VBlanks del frame lógico.
2. Display = `(refresh + audio + sprites + bitplanes) × líneas`: 320×4 planos → 84 slots/línea.
3. 6 planos con `width > 288` dispara el hint clásico; a 288 no.
4. Hires 640 (40 palabras/plano) activa el hint de robo de *even*.
5. Audio (1 slot/canal) y sprites (2 slots/canal) suman por línea.
6. Blitter = `palabras × canales`.
7. CPU en Chip cuenta; con Fast RAM el coste de Chip es 0.
8. `fps=25` duplica el presupuesto del frame lógico.
9. Sobre-presupuesto → `remaining_slots < 0` y `kHintOverBudget`.
10. Multi-franja (hasta 4) acumula el display con modos independientes.
