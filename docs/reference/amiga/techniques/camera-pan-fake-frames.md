# Fotogramas intermedios por paneo de cámara (ventana de display sobre framebuffer sobredimensionado)

**Qué resuelve**: que un juego 3D poligonal cuya pasada completa (transformación + orden + relleno por plano) no cabe en un VBlank se perciba a **50 Hz**. Mientras se genera el siguiente fotograma completo en el búfer trasero, se muestra el fotograma completo anterior **paneado por hardware** —solo se mueve la ventana de display, no se redibuja nada— de modo que el giro de cámara (yaw/pitch) sea continuo. Es la técnica de *Superhot* y de los simuladores 90s (*F29*, *Castle Master*): el mundo está casi quieto y la cámara rota o apunta; el movimiento aparente es un **pan de imagen** que el scroll del playfield reproduce casi gratis.

## Mecanismo

El framebuffer de la escena (bitplanes de PF1) es **más grande que el viewport**: lleva márgenes de `M` px por cada lado, y la pasada completa renderiza **todo el framebuffer** (viewport + márgenes), no solo lo visible.

```text
framebuffer PF1 (uno por cada bloque del doble buffer)
┌──────────────────────────────────────────────┐
│ margen superior                              │
│   ┌──────────────────────────────────────┐   │
│   │   VIEWPORT 320×256 (ventana visible) │   │  ← lo que muestra el display
│   └──────────────────────────────────────┘   │
│ margen inferior                              │
└──────────────────────────────────────────────┘
     margen izq                        margen der
```

- **Full**: `clear` del área de trabajo + malla completa renderizada en el framebuffer entero; la ventana de display se sitúa en su **origen** (centro del framebuffer) y se guarda la cámara de referencia (`cam_full`).
- **Fake**: no se toca el framebuffer; solo se **desplaza la ventana** dentro de él según `(cam_now − cam_full)`:
  - horizontal: puntero de bitplanes (coarse) + `BPLCON1` (fine 0..15) — píxel a píxel;
  - vertical: el puntero de bitplanes avanza en **líneas completas** (`y · bytes_por_fila`); OCS no tiene scroll fino vertical, así que el paneo de pitch se cuantiza a 1 línea (~1 px), que a estos deltas es imperceptible.

La ventana se mueve **hacia el margen que contiene el contenido que entra en pantalla**. Al girar la cámara a la derecha, el mundo «fluye» a la izquierda y la ventana avanza hacia el margen derecho del framebuffer (que ya fue renderizado en la pasada completa). El mapeo exacto ángulo→píxeles se calibra con `focal`/FOV y se **fija con un contrato de píxel** (fotograma fake = fotograma anterior desplazado por la ventana calculada), no a ojo.

## Condiciones para que sea válido

```text
permitir Fake solo si:
  movimiento de cámara ≈ pan (rotación con Δyaw/Δpitch pequeños; sin traslación)
  && |Δpx| acumulado dentro de los márgenes (X e Y)
  && los dinámicos relevantes no cambian de pose respecto al Full
  && frames_since_full < K (error de paralaje acotado)
“si no” → Full obligatorio
```

| Cuándo | Por qué falla |
|---|---|
| **Traslación de cámara** (caminar, strafe) | Paralaje fuerte: lo cercano debe moverse más que el fondo; un scroll global miente. |
| **Objetos que se mueven solos** (enemigos, puertas) | El framebuffer anterior los congela en su pose vieja. En *Superhot* el tiempo congelado lo hace correcto por diseño; si el juego los mueve, hay que forzar Full o moverlos como sprites. |
| **Δángulo grande** (giro rápido) | El pan uniforme deja de ser un yaw 3D (error de perspectiva) y se agota el margen. |
| **Cambio de iluminación / color de cara / patrones** | El píxel anterior queda; ningún scroll lo corrige. |
| **Primer frame, teletransporte, corte de escena** | No hay imagen coherente previa. |
| **Ventana ya pegada al borde del margen** | No queda contenido renderizado hacia el que panea. |
| **Fakes consecutivos > K** | El error de paralaje se acumula; mejor 1 Full cada 1–2 Fakes. |

Cielo/tierra y demás fondos viven en el plano de horizonte (plano 4 en la variante RoboCod; PF2 en la variante DPF — ver [`copper-horizon.md`](copper-horizon.md)) y se actualizan **de verdad** cada frame con la cámara real: el lag de 1 frame completo solo afecta a la geometría 3D y el fondo correcto lo disimula.

## Integración en el engine

| Pieza | Dónde | Cómo entra el paneo |
|---|---|---|
| Ventana sobre bitmap mayor | `eng/field/xlimited.hpp` (`AxisMode::Finite`) | Ya implementa «cámara sobre bitmap más grande que el display» en X con puntero directo (`set_camera`); el paneo fake es esa misma aritmética movida por Δcámara. **Y finito está pendiente** (ver roadmap). |
| Geometría de la ventana | `eng/scene/display.hpp` (`Band::update_from_view`, `scroll_off`) | La banda toma `planeaddx/planeaddy` del `PlayfieldHardwareView`; el offset de la ventana (coarse + fine) es exactamente eso. |
| Convenciones de scroll | `engine/include/eng/graphics/playfield_scroll.hpp:20-34` | `fine_scroll_coarse(scroll_x)` para el puntero (con `DDFSTRT=$30`) y `fine_delay(scroll_x)` para el nibble de `BPLCON1`; continuo en 0..15. |
| Parche por frame | `TileScrollScene` (`engine/include/eng/graphics/drivers/tile_scroll.hpp:748-810`) | Patrón «emitir una vez con `move_at` y parchear solo los `MoveSlot`»; el paneo añade el delta al valor parcheado. |
| Doble buffer de copperlist | `copper::DoubleBuffer` / `copper::Plan` (`PlanConfig::copper_bytes`, `copper/plan.hpp:57`) | La lista del bloque **inactivo** es la única que se parchea (nunca la que está siendo leída). |
| Overlay de balas | `eng/graphics/sprite_manager.hpp` + [`ROADMAP_JUEGO_SPRITES_BOBS.md`](../../../guides/roadmap/ROADMAP_JUEGO_SPRITES_BOBS.md) | Sprites hardware: el DMA los recarga cada frame, no hay que escribir en el playfield visible (cero tearing). |
| Tick de 50 Hz | mini-SO (`eng::os::init` + `Engine::run_frames`, ver `AGENTS.md`) | El bucle despierta por señal de VBlank; la pasada completa debe ser **reanudable en frontera de VBlank** para emitir el Fake del frame intermedio. |

La pasada completa suele necesitar **2 VBlanks** (objetivo de relleno poligonal: 25 fps de render real). La secuencia de display queda `Full · Fake · Full · …` = render a 25 Hz, percepción a 50 Hz. Si el juego no tiene tiempo por frame para rebanar la pasada completa, el patrón degenera en frame doblado y **no** se cumple el contrato de cadencia (`AGENTS.md` §3): medir con `.amigaprofile`.

## Coste

- La pasada completa paga los márgenes (`(320+2M)×(256+2B)` px en vez de 320×256): con `M=8..16` (y `B` la mitad) el sobrecoste de área es del orden del 8–17 %; con fill por plano el coste extra está en el `clear` y en pocas caras de borde.
- El Fake es casi gratis: mover la ventana cuesta unos pocos `patch_data` (2 words por plano para el puntero + 1 para `BPLCON1`), más el overlay de sprites. No hay trabajo de CPU por píxel.
- Memoria: cada bloque del doble buffer crece a `(320+2M)×(256+2B)` px × planos. Con `M=16, B=8` el área extra son ~13 800 px ≈ 1,7 kB por plano (≈ 5 kB por buffer de 3 planos, y por duplicado en el segundo bloque); medir el footprint por modo antes de fijar márgenes.

## Trampas

- **Dirección y signo**: el mapeo Δcámara→offset de ventana tiene convención de ejes; se fija con un test de contrato (fake == desplazamiento exacto del framebuffer anterior), no por inspección visual.
- **Escribir en el búfer visible**: prohibido salvo sprites/HUD por DMA; cualquier escritura de playfield en el búfer mostrado se ve como tearing.
- **Márgenes sin renderizar**: si la pasada completa no pinta el framebuffer entero, el Fake revela basura en el borde.
- **Márgenes solo en un bloque**: los dos bloques del doble buffer necesitan sus márgenes y su origen; olvidarlo produce un salto al primer swap.
- **Acumulación**: el offset se calcula siempre respecto a `cam_full` del último Full (no incremental), y se clampea al margen; al llegar al límite se fuerza Full.
- **Dinámicos con pose vieja**: balas/decales pueden ir en sprites; enemigos que cambian de silueta invalidan el Fake (regla de la tabla anterior).

## Enlaces

- Plan por fases: [`ROADMAP_3D_PRESENTACION.md`](../../../guides/roadmap/ROADMAP_3D_PRESENTACION.md).
- Arquitectura 3D objetivo (cámara, clipping, perfiles): [`3D_GAME_ARCHITECTURE.md`](../../../engine/architecture/3D_GAME_ARCHITECTURE.md).
- Estado del render 3D actual: [`3D_RENDER_VS_PHYSICS.md`](../../../engine/architecture/3D_RENDER_VS_PHYSICS.md).
- Horizonte cielo/tierra que acompaña al paneo: [`copper-horizon.md`](copper-horizon.md).
- Zona de display e interacción con el scroll: [`playfield_scroll.hpp`](../../../../engine/include/eng/graphics/playfield_scroll.hpp), `SCROLL_VARIANTS.md` y `PLAYFIELD_SCROLL_ARCHITECTURE.md` (en `docs/engine/architecture/`).
