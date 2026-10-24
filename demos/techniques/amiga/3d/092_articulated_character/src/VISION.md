# EVALUACIÓN DE VISIÓN — 092_articulated_character

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T11:31:40.552Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png, frame_004.png (5)
- Secuencia: `out/run/092_articulated_character/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Descripción de las imágenes en orden temporal:**

**Imagen 1:**
- **Elementos gráficos:** Un conjunto de tres formas geométricas sobre fondo negro.
  - Una forma rectangular naranja, en la parte inferior.
  - Una forma rectangular azul claro, encima de la naranja, ligeramente inclinada hacia la derecha.
  - Una forma rectangular azul claro más grande, encima de la anterior, también inclinada hacia la derecha.
- **Posición:** Todas las formas están centradas horizontalmente en la imagen.
- **Estado:** Las formas están en una posición estable, sin movimiento aparente.
- **Fondo:** Negro uniforme, sin artefactos.

**Imagen 2:**
- **Elementos gráficos:** Las mismas tres formas geométricas.
- **Cambio observable:**
  - La forma rectangular azul claro superior ha girado ligeramente hacia la izquierda, mostrando un ángulo diferente respecto a la imagen 1.
  - La forma azul más pequeña permanece en la misma posición relativa.
- **Estado:** El conjunto parece estar en movimiento, con una rotación en curso.
- **Fondo:** Negro uniforme, sin artefactos.

**Imagen 3:**
- **Elementos gráficos:** Las mismas tres formas geométricas.
- **Cambio observable:**
  - La forma rectangular azul claro superior ha girado ligeramente hacia la derecha, regresando a una posición similar a la de la imagen 1, pero no exactamente igual.
  - La forma azul más pequeña permanece en la misma posición relativa.
- **Estado:** El conjunto parece estar en movimiento, con una rotación en curso.
- **Fondo:** Negro uniforme, sin artefactos.

**Observaciones:**
- **Contornos:** Los contornos de las formas son limpios, sin halos, bandas ni píxeles cortados. No hay restos de posiciones anteriores.
- **Fondo:** El fondo negro permanece uniforme y sin artefactos en todas las imágenes.
- **Coherencia:** Las formas se mueven de forma coherente, manteniendo su relación espacial entre sí.
- **Elementos extraños:** No se observan elementos superpuestos, recortes, artefactos o partes incompletas. Las formas parecen completas y bien definidas en cada imagen.
