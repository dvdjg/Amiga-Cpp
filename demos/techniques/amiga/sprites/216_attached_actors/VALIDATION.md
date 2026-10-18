# VALIDATION - 216_attached_actors

## Contenido (regla 25)

La demo ya era exigente: **fondo estructurado** (16 bandas + tablero de dos tonos) y **cuatro actores con x e y por toda la pantalla** (fases distintas, gemas animadas de 4 planos). Se añade **telemetría de memoria** por el periférico de depuración.

## Medidas (A500_debug, ventana de 6 s)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **499** (49,9 fps: 1 campo/frame) | contador 0 |
| `comp_ok` / `comp_bad` | **304 / 0** | contadores 1/2 |

**Self-check de memoria:** cada composición verifica que todos los actores están representados — `sprites_hw + degradados == 6` (2 gemas × 2 placements del par attached + 2 chispas) — sin depender del vídeo (lección DT-035). `comp_bad = 0` en toda la ventana; `fps10 = 499` confirma que la composición por frame (pares recocinados) cabe en un campo.

## Visión

`src/VISION.md` (prompt canónico). El render no cambia con esta mejora (solo telemetría); la evaluación vigente cubre el fondo, los cuatro actores con sus pares de color y la ausencia de artefactos.
