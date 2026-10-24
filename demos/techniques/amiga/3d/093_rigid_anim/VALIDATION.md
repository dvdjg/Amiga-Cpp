# Validación - 093_rigid_anim

## Medidas

| Configuración | fps emulado | ciclos/frame | campos/frame | HUD visual (1,8 s) |
|---|---:|---:|---:|---|
| `A500_release` | **28,90** | 245 437 | 1,73 | `F64 28,9FPS` |
| `A500_debug` | **26,54** | 267 268 | 1,88 | `F59 26,7FPS` |

La firma de aristas del brazo solo cambia cuando cambian las coordenadas proyectadas; `F` y el `FPS` del HUD son, por tanto, visuales, no vueltas del bucle. La medida de bucle y la visual están alineadas.

Secuencia release de 20 capturas cada 40 ms:

```text
[3d-seq] OK: 20 frames 756x576; visual=17/19 cambios; maxEstatico=1
```

El `maxEstatico=1` es una captura que cae dos veces en el mismo frame de emulación; el HUD visual (28,9 fps) no cuenta esos duplicados. El gate `3d-demo-gate` exige `--require-distinct --max-static 1`.

## Visión (Ollama)

Evaluación canónica en `src/VISION.md` (`vision-final.mjs`, `qwen3-vl:8b-instruct-q8_0`, 5 frames). Respuesta cruda resumida:

> Modelo de rígido compuesto por segmentos rectangulares blancos... segmento central resaltado en azul... sobre dos líneas horizontales... El segmento azul gira de izquierda a derecha y viceversa... contorno limpio, sin bandas, halos, píxeles cortados ni restos... El fondo es negro uniforme... animación con un segmento central resaltado en azul que gira de forma cíclica. No hay artefactos visibles.

El modelo describe un modelo articulado con un segmento azul que gira y no detecta artefactos, parpadeos, recortes ni superposiciones. No usa la etiqueta “robot”, pero sí ve la animación de una cadena rígida en cada imagen.

## Alcance

- Medido en WinUAE-DBG A500, release y debug. No se ha medido A1200 ni hardware real.
- La firma del HUD usa coordenadas de aristas proyectadas; el gate exacto usa un hash RGB del área 3D sin HUD.
- El cuerpo/cámara estáticos permiten hornear el plano 0. Un cuerpo móvil necesitaría otra ruta (G5).
- Culling por bounds: hoy descarta nodos cuyo extremo trasero no alcanza `near_z`; el culling lateral completo queda para G5.
- No se declara flicker formal; la secuencia no muestra parpadeos ni recortes según visión y diff de frames.
