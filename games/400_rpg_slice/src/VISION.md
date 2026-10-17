# EVALUACIÓN DE VISIÓN — 400_rpg_slice

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-17T13:32:42.954Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_007.png, frame_009.png, frame_011.png (6)
- Secuencia: `out/run/400_rpg_slice/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Escena:** Vista isométrica de un mapa de juego con terreno verde, caminos marrones y árboles verdes dispersos.
- **Personaje:** Un personaje (cuadrado negro con un cuadrado verde en el centro) está en el centro del mapa, sobre un camino marrón.
- **Objetos:** Hay varios objetos (cuadrados negros con un cuadrado amarillo en el centro) y un objeto azul (parecido a una puerta o cofre) en la parte superior derecha.
- **Texto en la parte inferior:**
  - `RPG SLICE v1.0.4319 F 299HP3 ATK7`
  - `G0 PELT0 POT0 OBJ:HUNT 3 WOLVES`
- **Observaciones:** El personaje está en un punto de interacción. El texto indica que el objetivo es cazar 3 lobos.

**Imagen 2:**
- **Escena:** Vista isométrica de un mapa de juego con terreno verde, caminos marrones y árboles verdes dispersos.
- **Personaje:** El personaje (cuadrado negro con un cuadrado verde en el centro) está en el centro del mapa, sobre un camino marrón.
- **Objetos:** Hay varios objetos (cuadrados negros con un cuadrado amarillo en el centro) y un objeto azul (parecido a una puerta o cofre) en la parte superior derecha.
- **Texto en la parte inferior:**
  - `RPG SLICE v1.0.4319 F 512 HP3 ATK7`
  - `G0 PELT0 POT0 OBJ:SELL PELLS GET KEY`
- **Observaciones:** El personaje está en un punto de interacción. El texto indica que el objetivo es vender pelotas y obtener una llave.

**Imagen 3:**
- **Escena:** Vista isométrica de un mapa de juego con terreno negro, caminos marrones y una cuadrícula blanca.
- **Personaje:** El personaje (cuadrado negro con un cuadrado verde en el centro) está en el centro del mapa, sobre un camino marrón.
- **Objetos:** Hay varios objetos (cuadrados negros con un cuadrado amarillo en el centro) y un objeto azul (parecido a una puerta o cofre) en la parte superior derecha.
- **Texto en la parte inferior:**
  - `RPG SLICE v1.0.4319 F 1459 HP3 ATK7`
  - `G10 PELT0 POT8 OBJ:RECTORY IN THE TOWER`
- **Observaciones:** El personaje está en un punto de interacción. El texto indica que el objetivo es ir a la capilla en la torre.

**Cambios entre imágenes:**
- El personaje se mueve de un punto a otro en el mapa.
- El texto en la parte inferior cambia, indicando diferentes objetivos.
- El mapa cambia de una vista isométrica a una vista de cuadrícula.
