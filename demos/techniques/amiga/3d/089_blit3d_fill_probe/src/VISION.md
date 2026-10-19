# EVALUACIÓN DE VISIÓN — 089_blit3d_fill_probe

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T18:43:45.010Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png (2)
- Secuencia: `out/run/089_blit3d_fill_probe/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

- **Fondo**: negro uniforme.
- **Elementos gráficos**:
  - **Cuadrado blanco**: situado en la mitad izquierda de la pantalla. Contorno limpio, sin halos ni píxeles cortados. Fondo negro inmediatamente alrededor, sin artefactos.
  - **Rombo blanco**: situado en la mitad derecha de la pantalla, rotado 45 grados. Contorno limpio, sin halos ni píxeles cortados. Fondo negro inmediatamente alrededor, sin artefactos.
  - **Texto en la parte inferior**: “089 FILL PROBE” en letras blancas, tipo pixel art. Posición fija en la parte inferior central.
  - **Línea horizontal blanca**: ubicada en la parte superior izquierda, cerca del borde superior. Contorno limpio, sin artefactos.
- **Estado**: Todos los elementos están estables, sin movimiento ni cambio entre imágenes. No hay superposiciones ni recortes visibles. La composición es estática.
