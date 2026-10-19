# EVALUACIÓN DE VISIÓN — 002_adv_verbs

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T14:45:04.591Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_005.png, frame_007.png, frame_009.png (5)
- Secuencia: `out/run/adventure_amiga_002_adv_verbs/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Escena:** Vista isométrica de un interior con suelo de madera y paredes de color azul oscuro. Hay un objeto rectangular de color marrón claro en el centro, y a la derecha, una puerta cerrada con un marco de madera y una textura de rejilla oscura.
- **Texto en pantalla:** En la parte inferior, un menú de verbos en español. El verbo "COGE" está resaltado. El texto de estado dice: "COGE UN VERBO. ESTA CERRADA. NECESITA UNA LLAVE.".
- **Estado de la puerta:** La puerta está cerrada, con una textura de rejilla visible.
- **Elementos gráficos:** El contorno de la puerta es limpio, sin halos ni artefactos. El fondo inmediato alrededor de la puerta es uniforme y no muestra restos de su posición anterior.

**Imagen 2:**
- **Escena:** La misma escena, pero la puerta ahora está abierta, mostrando un fondo azul oscuro con una textura de rayas diagonales.
- **Texto en pantalla:** El verbo "USA" está resaltado. El texto de estado dice: "USA LA LLAVE GIRA. LA PUERTA SE ABRE.".
- **Cambio observable:** La puerta ha cambiado de estado: de cerrada a abierta. El contorno de la puerta es limpio, sin halos ni artefactos. El fondo inmediato alrededor de la puerta es uniforme y no muestra restos de su posición anterior.

**Imagen 3:**
- **Escena:** La misma escena que la imagen 2, con la puerta abierta.
- **Texto en pantalla:** El verbo "USA" está resaltado. El texto de estado dice: "USA LA LLAVE GIRA. LA PUERTA SE ABRE.".
- **Cambio observable:** No hay cambio en la escena respecto a la imagen 2. La puerta sigue abierta. El contorno de la puerta es limpio, sin halos ni artefactos. El fondo inmediato alrededor de la puerta es uniforme y no muestra restos de su posición anterior.

**Resumen de cambios:**
- La puerta cambia de estado de cerrada (imagen 1) a abierta (imagen 2 y 3).
- El texto de estado cambia para reflejar la acción de abrir la puerta.
- El menú de verbos cambia de "COGE" a "USA" para indicar la acción realizada.
- No hay artefactos, halos, píxeles cortados ni elementos superpuestos. Todos los elementos gráficos tienen contornos limpios y el fondo inmediato alrededor de los objetos es uniforme.
