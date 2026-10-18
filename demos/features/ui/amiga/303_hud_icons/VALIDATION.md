# VALIDATION - 303_hud_icons

## Medidas (A500_debug)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **419-426** (41,9-42,6 fps; reloj del periférico) | contador 0 |
| fps real (reloj de VBlank, `os::frame_count`) | **~44,4 fps** (659 frames / 747 latidos en 15 s) | instrumentación temporal DT-036 (retirada) |
| Trabajo por frame (secciones) | update ≤9k, draw_world ≤3,2k, present ≤2,2k, consume ≤1,3k ciclos | instrumentación temporal DT-036 (retirada) |

**Nota DT-036 (abierta):** la demo pierde ~11% de latidos con la CPU muy holgada. Evidencia acumulada **excluye**: coste del HUD/demo (secciones medidas), camino de espera de VBlank (en modo polling la cadencia es igual o peor), **preempción del SO** (`vb_foreign = 0`: ningún VBlank interrumpe otra tarea), IRQs perdidos (`vb_seq` 1:1 con los campos reales) y el Blitter (`hw_calls = 0`). Pendiente: perfil por frame (el perfilador ya funciona: ID de demo + `WINUAE_USE_LEGACY_LAUNCH=1` + WinUAE de la extensión + puertos propios; falta arreglar el parseo de muestras de `profile-samples.mjs`). El fps queda **por debajo del objetivo de 50** y por encima del suelo de 25; la demo se cierra con esta nota.

## Evidencia de memoria

`fps10` por contador (sin depender del vídeo) ✓. La captura por paso puede servir vídeo congelado (DT-035): el gate visual usa **captura por intervalos** (`--sequence-frames`, CPU viva).

## Visión (prompt canónico; `qwen3-vl:8b-instruct-q8_0`)

Respuesta cruda en `src/VISION.md`. Observado: rejilla de iconos con celdas resaltadas (rojo/amarillo), etiqueta `ITEMS`, línea de estado `SEL 05 FOC 03` → `SEL 01 FOC 03` (la selección avanza con el guion), franja de depuración `HUD ICONS v1.0.4861 ... FPS 49.9` → `41.9`, sin artefactos ("no hay bandas, halos, píxeles cortados ni restos"). La salida corresponde a la intención (inventario interactivo con foco y selección).
