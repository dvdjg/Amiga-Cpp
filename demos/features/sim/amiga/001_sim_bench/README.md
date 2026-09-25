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

`analyze` en la salida del runner da el `detail` (ticks solo/s en los bits altos y frames/s con
planificación en los bajos). En pantalla se ven además los pases de planificación/s, las
expansiones GOAP/frame y los pasos del HTN.

## Medición (A500, 12 criaturas, presupuesto 24)

Tres ventanas separadas: tick solo, búsqueda de planificación **sin caché** (vaciándola cada
pase) y tick + planificación **realista** (con caché e intervalo de replan).

```
tick solo/s                     : 131
frames/s (tick + planificacion) : 113
```

Con `--warp` la cifra **absoluta depende de la carga del host** (la misma build puede dar 131
o 40 según lo ocupada que esté la máquina): lo robusto es el **cociente** dentro de la misma
ejecución. La planificación realista cuesta ~1.2x el tick (~14% de sobrecarga), no el ~18x del
peor caso sin caché. Dos razones, y las dos son de uso normal:

1. **La caché de planes se usa de verdad** (`PlannerDriver::replan` llama a `plan_cached`):
   muchas criaturas replantean el mismo objetivo desde el mismo estado y no vuelven a buscar.
2. **No todas planifican cada frame**: el intervalo de replan (la histéresis) reparte las
   consultas.

El peor caso sigue medido en la ventana de búsqueda (todas las criaturas, sin caché) para no
esconder el coste real de A*.

## Verificación

`build -> run -> analyze` con READY OK (ver `run-report.json`). Cambiar `TARGET_MACHINE`
(A500 vs. A1200) compararía CPU.
