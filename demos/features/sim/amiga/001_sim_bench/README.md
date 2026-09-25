# Demo 001 (features/sim/amiga): benchmark del ecosistema `eng::sim`

Mide en Amiga (real o WinUAE) el coste del **ecosistema** (`SimWorld` con criaturas) y de su
**planificación** — GOAP con presupuesto *anytime* y una descomposición HTN — usando el
contador TOD de la CIA-A (50 Hz PAL), sin depender de que quepa en un frame.

Publica en `g_eng_run_status.detail`:

```
bits 31..16  frames de simulación por segundo
bits 15..0   expansiones de GOAP por frame (media)
```

y lo dibuja en pantalla junto con el número de criaturas y los pasos del HTN.

## Por qué importa

`SimWorld<>` (~24 KB) y el planificador van en **memoria estática**: en la pila del 68000 no
caben. El demo comprueba que el ecosistema completo compila y corre en el target m68k (no
solo en host: HOST-313/317 lo prueban allí) y da una cifra real de frames/s y de trabajo de
planificación por frame en el 68000.

## Ejecutar

```bash
bash tools/build/build-demo.sh demos/features/sim/amiga/001_sim_bench --release --clean
bash tools/run/run-demo.sh demos/features/sim/amiga/001_sim_bench --warp
```

`analyze` en la salida del runner da el `detail` (frames/s y expansiones/frame).

## Verificación

`build -> run -> analyze` con READY OK (ver `run-report.json`). Cambiar `TARGET_MACHINE`
(A500 vs. A1200) compararía CPU.
