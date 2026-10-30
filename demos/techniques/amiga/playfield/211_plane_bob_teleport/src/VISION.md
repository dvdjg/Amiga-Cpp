# EVALUACIÓN DE VISIÓN — 211_plane_bob_teleport

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-30T23:06:28.506Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_008.png, frame_016.png, frame_023.png, frame_031.png, frame_039.png (6)
- Secuencia: `out/run/211_plane_bob_teleport/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes en orden temporal:**

**Imagen 1:**
- **Fondo:** Pantalla de color azul oscuro uniforme.
- **Objetos visibles:**
  - **Dos puntos rojos:** Uno en la esquina superior izquierda, otro en la parte superior derecha.
  - **Dos puntos amarillos:** Uno en la mitad izquierda, otro en la mitad derecha, más abajo.
  - **Dos puntos azules:** Uno en la mitad derecha, más abajo, y otro en la parte inferior derecha.
- **Estado:** Todos los objetos están estacionarios en esta imagen.

**Imagen 2:**
- **Fondo:** Igual que la imagen 1.
- **Cambios respecto a la imagen 1:**
  - **Objetos amarillos:** El punto amarillo de la mitad izquierda ha desaparecido. El punto amarillo de la mitad derecha ha bajado ligeramente.
  - **Objetos azules:** El punto azul de la parte inferior derecha ha desaparecido. El punto azul de la mitad derecha ha subido ligeramente.
  - **Objetos rojos:** Los dos puntos rojos permanecen en la misma posición que en la imagen 1.
- **Estado:** Los objetos se mueven de forma independiente, pero no hay superposición ni artefactos visibles.

**Imagen 3:**
- **Fondo:** Igual que las imágenes anteriores.
- **Cambios respecto a la imagen 2:**
  - **Objetos rojos:** El punto rojo de la esquina superior izquierda ha desaparecido. El punto rojo de la parte superior derecha ha bajado ligeramente.
  - **Objetos amarillos:** El punto amarillo de la mitad derecha ha bajado más. El punto amarillo de la mitad izquierda ha reaparecido en una posición más baja.
  - **Objetos azules:** El punto azul de la mitad derecha ha bajado ligeramente. El punto azul de la parte inferior derecha ha reaparecido en una posición más baja.
- **Estado:** Los objetos continúan moviéndose de forma independiente, sin superposición ni artefactos visibles.

**Observaciones:**
- **Contorno de los objetos:** Los objetos tienen un contorno limpio, sin halos ni artefactos. No hay píxeles cortados ni colgando en los bordes.
- **Fondo:** El fondo permanece uniforme y sin cambios, y los objetos no tapan el fondo con un rectángulo de color uniforme.
- **Posiciones:** Las posiciones de los objetos cambian de forma coherente entre imágenes, lo que sugiere un movimiento continuo.
- **Elementos extraños:** No se observan elementos extraños, defectuosos o incoherentes. La secuencia parece ser una captura de pantalla de un juego o simulación en tiempo real.
