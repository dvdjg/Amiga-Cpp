# EVALUACIÓN DE VISIÓN — 086_bob_objects

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:27:43.683Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png, frame_009.png (6)
- Secuencia: `out/run/086_bob_objects/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Descripción de las imágenes en orden temporal:**

**Imagen 1:**
- **Fondo:** Un cielo con bandas horizontales de color que van del azul oscuro en la parte superior al rojo naranja en la base. En la parte inferior, una franja naranja representa el suelo.
- **Elementos fijos:**
  - Una franja verde oscura en la parte inferior central, que parece un terreno o una pared.
  - Sobre esta franja, tres montañas triangulares de color marrón, dispuestas en fila.
  - Cuatro postes verticales marrones, dos entre las montañas y uno en cada extremo, que parecen sostener la franja verde.
- **Elementos móviles (en el cielo):**
  - Un círculo rojo a la izquierda.
  - Un círculo verde y un círculo cian (azul verdoso) en el centro, superpuestos.
  - Un círculo amarillo con una semicircunferencia roja en su lado derecho, a la derecha del centro.
  - Un círculo naranja en la esquina superior derecha.
- **Estado de los objetos móviles:** Todos los círculos están completos y sin artefactos. El fondo alrededor de cada círculo es el cielo con bandas, sin restos de su posición anterior ni halos.

**Imagen 2:**
- **Fondo y elementos fijos:** Igual que en la imagen 1.
- **Cambio en los objetos móviles:**
  - El círculo rojo a la izquierda se ha desplazado ligeramente hacia la derecha.
  - El círculo cian (azul verdoso) ha desaparecido.
  - El círculo verde ha desaparecido.
  - El círculo amarillo ha desaparecido.
  - El círculo naranja en la esquina superior derecha permanece.
  - Aparece un nuevo círculo blanco, de tamaño similar al amarillo, a la derecha del círculo naranja.
- **Estado de los objetos móviles:** El círculo rojo tiene un contorno limpio. El círculo blanco es nuevo y tiene un contorno limpio. El fondo alrededor de los círculos nuevos es el cielo con bandas, sin artefactos.

**Imagen 3:**
- **Fondo y elementos fijos:** Igual que en la imagen 1.
- **Cambio en los objetos móviles:**
  - El círculo rojo a la izquierda se ha desplazado ligeramente hacia la derecha.
  - El círculo verde ha reaparecido, ahora parcialmente superpuesto sobre el círculo cian (azul verdoso), que ha desaparecido.
  - El círculo amarillo ha reaparecido, ahora parcialmente superpuesto sobre el círculo blanco, que ha desaparecido.
  - El círculo naranja en la esquina superior derecha permanece.
- **Estado de los objetos móviles:** Los círculos tienen contornos limpios. El fondo alrededor de los círculos es el cielo con bandas, sin artefactos.

**Observaciones:**
- Los objetos móviles cambian de posición y de presencia entre imágenes, pero siempre mantienen un contorno limpio y no dejan artefactos en el fondo.
- No hay elementos superpuestos de forma incoherente ni recortes.
- El fondo permanece constante en todas las imágenes.
