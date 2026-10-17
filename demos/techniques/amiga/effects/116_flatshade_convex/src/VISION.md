# EVALUACIÓN DE VISIÓN — 116_flatshade_convex

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-17T11:20:32.118Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png (5)
- Secuencia: `out/run/116_flatshade_convex/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Elemento central:** Un poliedro esférico compuesto por múltiples caras planas de forma irregular, coloreado en distintos tonos de azul. Las caras más cercanas a la fuente de luz (superior) son de un azul claro, mientras que las caras más alejadas (inferiores y laterales) son de un azul más oscuro. El poliedro tiene un aspecto pixelado.
- **Texto en la parte superior:** "FLATSHADE v2.0.4255 F 0".
- **Fondo:** Negro.

**Imagen 2:**
- **Elemento central:** El mismo poliedro, ahora con un cambio en la iluminación. Las caras que antes eran azules claras ahora son de un azul más intenso, y las caras oscuras se han vuelto aún más oscuras, creando un contraste mayor. La iluminación parece haberse movido hacia arriba.
- **Texto en la parte superior:** "FLATSHADE v2.0.4255 F 128".
- **Fondo:** Negro.

**Imagen 3:**
- **Elemento central:** El poliedro se ha girado ligeramente hacia arriba y hacia la izquierda. La cara central (hexagonal) ahora está más orientada hacia el espectador y es la más brillante. Las caras laterales y superiores están más iluminadas, mientras que las inferiores son más oscuras.
- **Texto en la parte superior:** "FLATSHADE v2.0.4255 F 128".
- **Fondo:** Negro.

**Cambios observables:**
- El valor "F" en la parte superior cambia de "0" a "128" entre la primera y la segunda imagen.
- El poliedro gira entre la segunda y la tercera imagen.
- La iluminación del poliedro cambia de forma perceptible entre las imágenes, lo que sugiere que el programa está simulando un cambio de ángulo de luz o una rotación del objeto.
