# EVALUACIÓN DE VISIÓN — 095_collision3d

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T08:51:41.872Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_003.png, frame_004.png, frame_005.png (5)
- Secuencia: `out/run/095_collision3d/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Elementos gráficos:** Un rectángulo grande de color rosa y un rectángulo más pequeño de color amarillo, situado dentro del rosa.
- **Posición y estado:** El rectángulo rosa está centrado en la pantalla. El rectángulo amarillo está posicionado en el lado derecho del rosa, parcialmente superpuesto a él.
- **Fondo:** Negro uniforme.
- **Texto en la parte superior:** `695 COLLISION3D v1.0.6331 | F40 16.4FPS | INI L1 D2`.
- **Observaciones:** El contorno del rectángulo amarillo es limpio, sin halos ni artefactos. El fondo negro está intacto alrededor de los objetos.

**Imagen 2:**
- **Elementos gráficos:** Un rectángulo grande de color azul y un rectángulo más pequeño de color amarillo, situado a la derecha del azul.
- **Posición y estado:** El rectángulo azul está centrado en la pantalla. El rectángulo amarillo está posicionado a la derecha del azul, parcialmente superpuesto a él.
- **Fondo:** Negro uniforme.
- **Texto en la parte superior:** `695 COLLISION3D v1.0.6331 | F47 16.4FPS | OUT L1 D2`.
- **Observaciones:** El contorno del rectángulo amarillo es limpio. El fondo negro está intacto alrededor de los objetos.

**Imagen 3:**
- **Elementos gráficos:** Un rectángulo grande de color azul y un rectángulo más pequeño de color amarillo, situado a la derecha del azul.
- **Posición y estado:** El rectángulo azul está centrado en la pantalla. El rectángulo amarillo está posicionado a la derecha del azul, parcialmente superpuesto a él.
- **Fondo:** Negro uniforme.
- **Texto en la parte superior:** `695 COLLISION3D v1.0.6331 | F54 16.2FPS | OUT L1 D2`.
- **Observaciones:** El contorno del rectángulo amarillo es limpio. El fondo negro está intacto alrededor de los objetos.

**Cambios entre imágenes:**
- El rectángulo rosa de la primera imagen se ha cambiado por un rectángulo azul en las siguientes imágenes.
- El texto en la parte superior cambia, indicando un cambio en el estado del programa (de `INI` a `OUT`).
- La posición relativa entre el rectángulo azul y el amarillo es la misma en las imágenes 2 y 3, pero el rectángulo amarillo parece haberse desplazado ligeramente hacia la derecha en la imagen 3 respecto a la 2, aunque la diferencia es mínima y no se puede confirmar con certeza.
- El FPS (frames per second) disminuye ligeramente de 16.4 a 16.2 entre la imagen 2 y la 3.
