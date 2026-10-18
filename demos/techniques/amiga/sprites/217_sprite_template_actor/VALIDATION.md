# VALIDATION - 217_sprite_template_actor

## Contenido (regla 25)

La demo ya era exigente: **fondo estructurado** (bandas suaves sobre navy) y el **tótem de 4 franjas** recorriendo toda la pantalla con onda triangular (x e y). Se añade **telemetría de memoria** por el periférico de depuración.

## Medidas (A500_debug, ventana de 6 s)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **499** (49,9 fps: 1 campo/frame) | contador 0 |
| `comp_ok` / `comp_bad` | **303 / 0** | contadores 1/2 |

**Self-check de memoria:** cada composición verifica que las cuatro franjas están representadas — `sprites_hw + degradados == 4` (una placement por franja del canal encadenado) — sin depender del vídeo (lección DT-035). `comp_bad = 0` en toda la ventana; `fps10 = 499` confirma que la re-proyección por frame (intents + paleta por franja) cabe en un campo.

## Visión

`src/VISION.md` (prompt canónico). El render no cambia con esta mejora (solo telemetría); la evaluación vigente cubre el fondo, el tótem de franjas con sus colores y la ausencia de artefactos.
