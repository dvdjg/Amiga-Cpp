# VALIDATION - 053_sprite_multiplex

## Ruta validada (2026-10-24)

La demo usa `scene::SpriteDirector<2, 1, 1, 1, 8, 8>`: registra un `Visual` de 16x24, una entidad con `HwSpriteTemplate` de 6 segmentos y publica con `emit` + `present`. Ya no llama a `SpriteManager` directamente.

El template necesita 6 intents; `SpriteScene<2>` reserva 8 (4 por actor), por eso la capacidad no es 1. El switch de paleta se corrigio a `first = 17` y un color por franja.

## Build y ejecucion

```bash
tools/build/build-demo.sh demos/techniques/amiga/sprites/053_sprite_multiplex --debug
tools/run/run-demo.sh demos/techniques/amiga/sprites/053_sprite_multiplex --warp
```

Resultado: `READY`; `detail = 246` palabras de copperlist en la ultima captura. La composicion publica 6 placements HW (un segmento por franja), 0 BOB y 0 degradados en el frame inicial.

## Vision (Ollama local)

Prompt: "Describe si esta captura Amiga contiene seis barras de colores multiplexadas en una columna o si esta completamente negra."

Respuesta: se ven seis bloques/barras de colores en columna (gris oscuro, rojo, verde, azul, amarillo y cian); no esta negra ni corrupta. La animacion horizontal se comprobo en capturas sucesivas de la demo.

## Notas

- La demo no expone aun un contador `fps10`; la validacion es READY + reporte de composicion + vision. Anadir el contador queda pendiente si se quiere comparar con las demas demos.
- El cambio de rango horizontal a 40..166 px de pantalla mantiene el bloque visible durante todo el rebote; antes el rango heredado lo sacaba por la izquierda.
