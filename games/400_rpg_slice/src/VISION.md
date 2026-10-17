# EVALUACIÓN DE VISIÓN — 400_rpg_slice

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-17T16:17:58.443Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_003.png, frame_005.png, frame_008.png, frame_010.png, frame_013.png (6)
- Secuencia: `out/run/400_rpg_slice/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Escena:** Mapa de mundo en vista isométrica, estilo pixel art retro. El terreno es verde con árboles (cuadrados verdes con troncos marrones) y caminos (rectángulos marrones). Hay un cuerpo de agua azul a la derecha.
- **Personaje:** Un personaje (cuadrado rojo con un cuadrado amarillo encima) está en el centro del mapa, sobre un camino.
- **Elementos:** Un objeto negro (posiblemente una puerta o cofre) está en la parte superior derecha. Un objeto negro más pequeño está en el centro del mapa, sobre un camino.
- **Texto en la parte inferior:**
  - `RPG SLICE v1.0.4377 F 273 HP9 ATK7`
  - `G0 PELT1 POT2 OBJ:HUNT 3 WOLVES`

**Imagen 2:**
- **Escena:** Mapa de mundo similar, pero con menos árboles y más caminos. El personaje está en el centro, sobre un camino.
- **Personaje:** El personaje (rojo y amarillo) está en la misma posición que en la imagen 1, pero ahora hay un objeto negro (posiblemente una puerta o cofre) a la izquierda del personaje.
- **Texto en la parte inferior:**
  - `RPG SLICE v1.0.4377 F 563 HP3 ATK7`
  - `G0 PELT3 POT3 OBJ:SELL PELTS GET KEY`

**Imagen 3:**
- **Escena:** Mapa de mundo en vista isométrica, pero con un fondo negro y una cuadrícula blanca. El terreno es ahora un patrón de cuadrados negros con líneas blancas.
- **Personaje:** El personaje (rojo y amarillo) está en el centro del mapa, sobre un camino marrón.
- **Elementos:** Un objeto azul (posiblemente una puerta o cofre) está en la parte superior derecha del mapa.
- **Texto en la parte inferior:**
  - `RPG SLICE v1.0.4377 F 1925 HP3 ATK7`
  - `G10 PELT0 POT8 OBJ:RECTORY IN THE TOWER`

**Cambios entre imágenes:**
- El personaje se mueve de la imagen 1 a la 2 y luego a la 3.
- El texto en la parte inferior cambia, indicando cambios en el estado del juego (HP, F, OBJ).
- La escena cambia de un mapa con árboles y caminos a un mapa con una cuadrícula y un fondo negro.
- En la imagen 3, el personaje está en una posición diferente y hay un objeto azul en la parte superior derecha.
