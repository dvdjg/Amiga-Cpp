# 116 - flatshade-convex: informe de validación

Informe de validación de la demo (procedimiento F0-F5). Pasada de modernización 2026-10-17. La demo queda **ABIERTA** por presupuesto (19,7 fps < suelo 25 del efecto de relleno de polígonos): detalle y plan en la deuda **DT-026** (`docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md`).

## Medidas (`A500_debug`, `tools/debug/measure-fps.mjs`, 15-20 s)

| Build | fps emulado | campos/frame | ciclos/frame |
|---|---|---|---|
| HEAD (antes) | 19,20 | 2,60 | 369.564 |
| Modernizada | **19,70** | 2,54 | 360.056 |

Cambios de esta pasada: `run_frames` (IRQ + `Wait()`, sin sondeo de VPOSR), `set_blit_service_enabled(false)` (la demo no usa la cola de fondo) y **franja de depuración de coste cero** (zona de 1 bpp activada por Copper en una etapa de `scene::compose`, texto estático + contador de 24 escrituras/segundo; el render por frame no cambia).

## Experimentos acotados y descartados (medidos)

- **Fill XOR acotado a la caja del objeto** (soporte `bbox` nuevo en `flat_shade_xor`): 19,2 → **17,3 fps**. El fill acotado son 4 jobs (uno por plano) cuyo `wait_blitter` interno serializa la CPU con el Blitter dentro del update; el barrido completo es **un solo job** lanzado sin esperar. El recorte (~56k menos de fill DMA) solo paga con el referee async (DT-026).
- **Clear acotado por bbox del buffer** (ring de cajas por buffer): misma serialización por planos (4 jobs) → descartado por el mismo motivo y no medido por separado (el build que lo contenía es el de 17,3 fps junto al fill).
- Se conserva el **barrido completo** de fill y clear (single-job, `wait=false`), con comentarios de coste en `src/main.cpp`.

## Validación visual (secuencia + visión)

- Secuencia: `run-demo.sh --sequence-frames 8 --sequence-interval-ms 700`; evaluación canónica en `src/VISION.md` (`qwen3-vl:8b-instruct-q8_0`).
- Respuesta (resumen fiel de la cruda): poliedro con **caras planas azules** de tonos variables (sombreado plano) girando sobre fondo oscuro, texto blanco `FLATSHADE v2.0.4243 F <n>` en la parte inferior; **sin artefactos, recortes ni superposiciones**; secuencia coherente de rotación.
- Comprobación propia: la captura por `run-demo` (una vez) muestra el balón sólido con contorno y el HUD legible; el render del balón no se ha tocado en esta pasada (misma llamada `flat_shade_xor` a barrido completo, mismos transforms).

## Fidelidad

- La pasada no modifica el render del sólido (la técnica de contorno EOR + area fill XOR, los transforms y la composición quedan igual); el único cambio de imagen es la **franja de depuración** nueva (zona de 1 bpp abajo, filas ≈216..224).
- El gate de fase congelada (`FLATSHADE_FREEZE_ANGLE` + `tools/analyze/freeze-diff.mjs`) sigue siendo el contrato determinista: aplica si se toca el dibujo (p. ej. al portar el fill al plan async).

## Límites del alcance (SIN VERIFICAR / abierto)

- **Presupuesto**: 19,7 fps < 25 → demo **ABIERTA** (DT-026). La deuda no es de corrección visual sino de camino de ejecución (referee async del `FramePlan`).
- La medida cubre `A500_debug`; no se midió `A500_release`.
- No se declaró `flicker-baseline.json` en esta pasada.
- La ruta asm (`K_FLATSHADE_ASM=1`) sigue sin ser válida (desfase de contorno, README): no se ha tocado.
