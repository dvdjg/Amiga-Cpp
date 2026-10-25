# VALIDATION — 206_tiled_beginning_fields_baked

## Alcance

- Mapa `Beginning Fields` (40×40, 16×16) compuesto en host con `tools/tiled/compose-map.mjs` y cocido a EHB con `tools/tiled/cook-map.mjs` (perfil `ehb`, 1183 tiles, banco interleaved de 225 KB).
- Scroll **solo horizontal** (`AxisPolicy::Off` en Y), mapa toroidal en X, cámara 2 px/frame con inversión cada 400 frames.
- Verificado en `A500_debug` con WinUAE. **No** cubre overlays, animación de tiles en runtime, objetos como actores ni mapa vertical; eso queda para 207/208.

## Medidas

`node tools/debug/measure-fps.mjs 206_tiled_beginning_fields_baked A500_debug --json`:

```json
{"demo":"206_tiled_beginning_fields_baked","config":"A500_debug","date":"2026-10-25","commit":"b9692b26","emulatedFps":49.97,"hostFps":49.9,"cyclesPerFrame":141974,"fieldsPerFrame":1.00069,"frames":1009,"cycles":143251612,"measurementMs":20222,"detail":"0x20600000","watchdog":null,"stable":true,"quantizedFields":1,"minFps":49.54,"maxFps":50.32}
```

Regresión con flicker:

```text
== 206_tiled_beginning_fields_baked: flicker ==
Regression OK.
```

El banco Chip medido es 230 400 B (225 KB) y el display EHB 320×256×6 = 61 440 B; la arena configurada es 176 KB. El render entra en 1 campo/frame y no se observaron frames de 2 campos en la ventana medida.

## Visión (prompt canónico)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

La respuesta cruda completa está en [`src/VISION.md`](src/VISION.md). Resumen: el modelo describe una escena pixel-art rural con casa, entorno, árboles, carro y objetos; detecta movimiento gradual del carro/escena entre frames, contornos limpios, fondo intacto y HUD con FPS que pasa de 0.0 a 49.9. No reporta parpadeos, recortes ni rectángulos de color alrededor de objetos. Etiqueta la perspectiva como isométrica, pero la demo es ortogonal top-down; es una imprecisión descriptiva del modelo, no un defecto del render.

## Conclusión

En el alcance probado, el bake Tiled se ve coherente, el scroll horizontal es continuo y el coste es 49,97 fps con 1,0007 campos/frame. Sin verificar: overlays/animación runtime, Y, oclusión de actores e isométrico. La selección EHB está justificada por la capa de sombras y el informe `proposal.md`; el usuario puede imponer otra paleta/profundidad con `cook-map --palette/--planes`.
