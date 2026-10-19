# VALIDATION - 303_hud_icons

## Medidas (A500_debug, bucle por **sondeo**)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **400-419** (40,0-41,9 fps; reloj de **pared** del periférico, subestima — ver nota) | contador 0 |
| `vbl` / `frame` | **635-844 / 634-843** → ratio **0,998-0,999** (la app consume ~todos los latidos) | contadores 2/3 |
| Trabajo por frame (secciones) | update ≤9k, draw_world ≤3,2k, present ≤2,2k, consume ≤1,3k ciclos | instrumentación temporal DT-036 (retirada) |

**DT-036 RESUELTA para la 303 (2026-10-19).** La pérdida de ~11% de latidos era la **familia 212/DT-011** (la interacción de las IRQ de nivel 3 con exec bajo el SO vivo; ver `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`). Evidencia del cierre, medida con el ratio `frame`/`vbl` contra el reloj de VBlank del mini-SO (50 Hz exactos):

- **Espera por señal** (la anterior): 659 frames / 747 latidos = **88,2%** (11,8% perdido).
- **Espera activa** (`VblankWait::Active`): 209 frames / 234 latidos = **89,3%** y la app **moría** a los ~4,7 s (valores idénticos en dos corridas: parón determinista; el latido IRQ muere con ella).
- **Sondeo** (`VblankWait::Polling`, `App::run_polling`): **99,8-99,9%** y la app sobrevive la ventana — es la única variante estable de la investigación 212 (clase 119/400/303).

El `fps10` de pared subestima (40-42) porque el reloj del periférico no es de instrucciones; el ratio `frame`/`vbl` es la medida honesta. La franja sigue mostrando `FPS` calculado con el reloj de pared (cosmético).

## Evidencia de memoria

El gate `analyze-sequence.sh` exige `vbl >= 500` (la app viva) y ratio `frame`/`vbl >= 950/1000` (sin pérdida de campos), además de la verificación visual por secuencia por intervalos (DT-035: la captura por paso puede servir vídeo congelado).

## Visión (prompt canónico; `qwen3-vl:8b-instruct-q8_0`)

Respuesta cruda en `src/VISION.md`. Observado: rejilla de iconos con celdas resaltadas (rojo/amarillo), etiqueta `ITEMS`, línea de estado `SEL 05 FOC 03` → `SEL 01 FOC 03` (la selección avanza con el guion), franja de depuración `HUD ICONS v1.0.4861 ... FPS 49.9` → `41.9`, sin artefactos ("no hay bandas, halos, píxeles cortados ni restos"). La salida corresponde a la intención (inventario interactivo con foco y selección).

