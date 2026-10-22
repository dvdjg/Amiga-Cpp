# EVALUACIÓN DE VISIÓN — 090_polyhedra_orbit

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-22T05:49:09.382Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/090_polyhedra_orbit/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Objetos visibles:**
    *   Un poliedro complejo, compuesto por múltiples caras de colores (rojo, naranja, cian, amarillo, magenta), que parece estar girando. Está ubicado en el centro superior de la pantalla.
    *   Un cubo más pequeño, con caras de colores (verde, marrón, cian, amarillo), situado a la izquierda y ligeramente abajo del poliedro.
*   **Texto en la parte inferior:**
    *   `090 POLYHEDRA ORBIT v1.0.5900`
    *   `FRAME 96 FPS 010.91417`
*   **Fondo:** Un fondo azul oscuro uniforme.
*   **Estado de los objetos:**
    *   El poliedro y el cubo están en movimiento, pero su contorno es limpio, sin halos ni artefactos visibles.
    *   El fondo alrededor de los objetos es uniforme y no muestra restos de posiciones anteriores.

**Imagen 2:**

*   **Objetos visibles:**
    *   El mismo poliedro complejo, ahora girado a una posición diferente, mostrando caras de colores distintas (rojo, naranja, cian, amarillo, verde).
    *   El mismo cubo pequeño, también girado, mostrando caras de colores distintas (rojo, cian, amarillo).
*   **Texto en la parte inferior:**
    *   `090 POLYHEDRA ORBIT v1.0.5900`
    *   `FRAME 128 FPS 15.6 V17`
*   **Fondo:** El fondo azul oscuro permanece uniforme.
*   **Estado de los objetos:**
    *   Los objetos han cambiado de posición y orientación, lo que indica que están en movimiento continuo.
    *   El contorno de los objetos es limpio y no hay artefactos visibles.
    *   El fondo alrededor de los objetos es uniforme y no muestra restos de posiciones anteriores.

**Cambios entre imágenes:**

*   **Posición y orientación:** Ambos objetos (poliedro y cubo) han girado a nuevas posiciones y orientaciones.
*   **Texto:** El texto de la segunda imagen muestra un cambio en el número de marco (`FRAME 128` en lugar de `FRAME 96`) y en los valores de FPS (`15.6 V17` en lugar de `010.91417`). El texto de la primera imagen parece tener un error de formato en el valor de FPS, que muestra `010.91417` en lugar de un valor numérico coherente como `10.91417`.
*   **Artefactos:** No se observan artefactos visibles en ninguna de las imágenes. Los contornos de los objetos son limpios y no hay halos ni píxeles cortados.
