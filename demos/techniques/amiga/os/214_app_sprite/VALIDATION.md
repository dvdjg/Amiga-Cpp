# VALIDATION - 214_app_sprite

## Contenido (regla 25)

La demo ya era exigente: **fondo estructurado** pre-renderizado a planar (640×256: cresta de horizonte y tablero de marcadores, colores 1/2/3 sobre navy) que **scrollea 2 px/frame**, y **dos objetos móviles con x e y** (el sprite de `screen.sprite` y el actor del mundo retenido, a velocidades distintas) con cookie-cut de disco real (máscara + anillo). Se añade **telemetría de memoria** por el periférico.

## Medidas (A500_debug, ventana de 6 s)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **338** (33,8 fps; el blit de fondo a pantalla completa por frame es el coste dominante, DT-030) | contador 0 |
| `copy_bad` | **0** | contador 1 |
| `scroll` | **94** (la fase avanza) | contador 2 |

`copy_bad = 0` (todos los blits de la ventana del fondo se materializaron) + `scroll` avanzando = progreso del render por contadores, sin depender del vídeo (lección DT-035). El auto-chequeo por píxel del sprite no se hace en esta demo: el doble buffer hace ambiguo qué buffer leer en el instante del contador (declarado); la presencia de los objetos se verifica por visión (VISION.md) y por el contador de blits.

## Visión

`src/VISION.md` (prompt canónico): el render no cambia con esta mejora (solo se añadió telemetría); la evaluación vigente cubre el scroll, los dos objetos y la ausencia de artefactos.
