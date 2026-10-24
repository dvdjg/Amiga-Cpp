# Vertical slice 2D: «Caverna» (A500)

Estado: **V0 hecho y V1/V2/V3/V4 parciales** en `games/500_caverna`: mapa fijo `FixedTileLayerMap<256>`, cámara, jugador con colisión contra el tilemap, salto/gravedad, DPF 3+3, cuatro enemigos con patrulla/animación y HUD a ~50 fps; faltan plataformas ricas, ocho actores, audio (diferido) y la medición automática de latencia. Este documento fija el primer juego que debe cerrar la API de alto nivel y el presupuesto contra el que se medirá. No es una demo de técnica: es el consumidor real que obliga a que la fachada, el planner, los assets y el presupuesto funcionen juntos.

## 1. Qué se construye

Un **side-scroller de plataformas** para A500, de una sola pantalla de juego y tres minutos de recorrido:

- DPF **3+3** a **320×208**, 50 Hz PAL nominal, con el fondo en PF2 y las plataformas/objetos en PF1.
- Mapa de **256×20 tiles de 16×16** (4096×320 px) usando `FixedTileLayerMap<256>`: 256 es potencia de dos y permite resolver el índice X con desplazamiento.
- Un jugador de 16×24 px, salto, gravedad y colisión contra el tilemap; cámara con dead-zone horizontal y clamp al mundo.
- Ocho enemigos de un tipo, animación de dos frames y reacción simple; cuatro BOBs/sprite hardware como máximo simultáneo.
- HUD de una línea en la franja inferior (8 px): nombre, versión, frames y fps; sin robar ancho al área de juego.
- Audio de una música de fondo y cuatro SFX cortos, disparados por eventos; sin mezclador de más de cuatro voces.
- Carga de assets desde manifiesto generado; ni `INCBIN` ni memcpy en el código de juego.

El objetivo no es la cantidad de contenido: es que el juego se escriba **sin `configure_memory`, `FramePlan`, `Block`, `CopperIntent`, planos ni backend**, y que el frame quepa con margen.

## 2. Presupuesto de la slice

Estos números son el contrato de aceptación; si un cambio los supera, se recorta contenido o se optimiza, no se sube el umbral.

| Recurso | Presupuesto | Medición |
|---|---|---|
| Cadencia | 49,9–50,0 fps PAL, 12 s, `stable=true`, sin frames de 2 campos | `measure-fps` |
| CPU activa del frame | **≤ 55k ciclos** para scroll + juego + sprites + audio + HUD | `prof_clock`/`run-headroom-gate` |
| Coste de pared | ≤ 1 campo por frame; p95 ≤ 1,01 campos | `measure-fps` |
| Memoria de escena | ≤ 160 KiB Chip, con headroom explícito para OS/audio | `remaining_chip()` y matriz de memoria |
| Blitter | ≤ 8k palabras y ≤ 48 jobs por frame | `FramePlan::blit_budget()` |
| Sprites | ≤ 5 canales/placements simultáneos (jugador + cuatro enemigos) | reporte del `SpriteDirector` |
| Audio | 1 música + 4 SFX, ≤ 4 voces DMA; sin underrun en 12 s | `AudioPlan`/`AudioFeeder` |
| Latencia de entrada | cambio visible en el siguiente campo | prueba de secuencia |
| Memoria dinámica | 0 reservas/heap por frame | inspección y contadores |

La base de la 202 ya mide **≈32,6k ciclos** de CPU activa con DPF 3+3 y scroll; la slice deja por tanto **≈22k** para lógica, colisión, cuatro sprites y audio. Si no cabe, se reduce el número de enemigos o el alcance del mapa antes de tocar el DPF o el framebuffer.

## 3. Fases de implementación

| Fase | Entregable | Gate |
|---|---|---|
| **V0 - contrato** | `MiniGame` de alto nivel con `App`/`Screen`/`input`/`present`; sin plomería | HOST-494 compila; revisión de imports |
| **V1 - mundo** | cámara, mapa fijo 256×20, scroll DPF y colisión de cajas | V1 parcial: cámara/mapa/DPF 3+3 y colisión 16×16 contra tiles sólidos; faltan plataformas de mapa ricas |
| **V2 - jugador** | movimiento, salto, gravedad, suelo/plataformas, cámara dead-zone | V2 parcial: movimiento, salto/gravedad y suelo; faltan dead-zone y plataformas |
| **V3 - enemigos y sprites** | ocho actores, animación, prioridad y reacción; cinco canales | V3 parcial: cuatro enemigos con patrulla y animación; faltan ocho actores y reacción completa |
| **V4 - audio y HUD** | música + cuatro SFX, HUD 1 línea, eventos de sonido | V4 parcial: HUD 1 línea con número y fps; audio **diferido por decisión del usuario** |
| **V5 - cierre** | carga por manifiesto, sin memoria dinámica, presupuesto congelado | `run-headroom-gate`, suite host y visión |

Cada fase conserva el juego jugable; no se aplaza la integración al final.

## 4. Dependencias que deben estar cerradas antes de V1

1. La fachada de juego debe permitir el contrato de **30 líneas** (HOST-494) y no obligar a `FramePlan`/`Block`.
2. El planner de intención debe entregar un `FramePlan`/`AudioPlan` y completar por `IntentDone` sin bloquear el bucle.
3. Los assets deben cargarse por nombre desde manifiesto (sprite, tilemap, música, SFX) sin `INCBIN` en el juego.
4. El mapa debe poder declararse con `FixedTileLayerMap<256>` y el pipeline debe generar ese ancho.

## 5. Qué queda fuera

- Multijugador, scroll vertical libre, parallax de más de dos capas y efectos de raster por línea.
- 3D, RPG, aventura, GUI de ventanas y HUD avanzado.
- AGA/A1200; la slice se valida primero en A500 OCS/ECS.
- Contenido artístico definitivo; los placeholders deben ser deterministas y medibles.

La regla es simple: **una slice jugable y medida vale más que diez demos técnicas**. Hasta que V5 pase, no se abre un frente nuevo de contenido.
