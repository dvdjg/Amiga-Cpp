# 216_attached_actors - pares *attached* end-to-end por el camino de actores

**Objetivo:** demostrar la técnica *attached* (15 colores) **a través de la fachada de
actores**: el juego declara un `ActorDesc` con `visual.attached` (4 planos de 16 px) en
`eng::SpriteScene`; el engine reparte los **dos canales contiguos** del par, **cocina** las dos
estructuras DMA (canal par = planos 0-1; canal impar = planos 2-3 + `ATTACH`) con la DATA del
frame vigente y publica **dos placements**. La demo hermana [214](../214_attached_object/README.md)
ejecuta la misma técnica **a mano** con el helper; ésta es el camino de juego.

Técnica y mecanismo: [sprite-techniques-catalog.md](../../../../docs/reference/amiga/techniques/sprite-techniques-catalog.md)
(técnica 2), [sprite-layer.md](../../../../docs/reference/amiga/techniques/sprite-layer.md) §4
(AHRM 3.ª cap. 4, «Attached Sprites», Table 4-5) y
[SPRITE_CHANNEL_WINDOWS.md](../../../../docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md) §6–§8.

## Qué muestra

- **Dos gemas *attached*** (16×16, 4 planos → 15 colores) en los pares 0/1 y 2/3, con 2 frames
  de animación (bandas concéntricas que pulsan y brillo especular móvil) que **recorren toda la
  pantalla** en X e Y con fases propias.
- **Dos chispas** de 3 colores (rombo y cruz, canales 4/5) que cruzan la pantalla con fases
  propias: la comparación **15 tonos vs 3 tonos** es directa. Un par *attached* direcciona
  `COLOR17-31` completo (WinUAE `drawing.cpp` ~4239: `col = v + 16`), mientras que un sprite
  no-*attached* toma los 3 tonos de su par (`COLOR25-27` para 4/5).
- Los cuatro actores van **fijos a su canal** (`assign_rank = 1` + `preferred_channel` 0/2/4/5):
  pueden solaparse y recorrer la pantalla sin que el allocator les cambie el par de color
  (cada par tiene sus `COLORxx`). El **rearme vertical** para franjas disjuntas sigue soportado
  por el emisor (`emit_placements_into`, HOST-428).

## Implementación (el contrato que ilustra)

```text
ActorDesc { visual.attached, 4 planos }        ← el juego declara QUÉ
        │  SpriteScene::emit(plan, ctx)
        ▼
build_sprite_intents: DOS intents contiguos    ← canal par + impar con `attach`
        │  SpriteAllocator (pareja 0+1, 2+3, …)
        ▼
compose_sprites: cook_attached_pair en el pool  ← el engine cocina las estructuras DMA
        │  placements: {par, DATA planos 0-1}, {impar, DATA planos 2-3 + ATTACH}
        ▼
SpriteManager::emit_placements_into            ← armado temprano + rearmes por franja
        │  copper::DoubleBuffer: reemitir el bloque inactivo y `flip`+`install`
        ▼
        Copper (SPRxPT/POS/CTL)
```

- El **pool de cocinado** (`set_cooked_pool`) es Chip y lo posee la demo (sin heap): el engine
  escribe en él las estructuras `[POS, CTL, DAT/DATB…, 0,0]` con cabecera OFF
  (`sprite_limits.hpp`, antipatrón *Phantom Sprite*, `sprite-layer.md` §10). Sin pool, el par se
  rechaza de forma controlada (`result.ok == false`).
- La animación sale de `Visual::frame_stride` + `Animation`: la DATA publicada es la del frame
  vigente (`SPRITE_CHANNEL_WINDOWS.md` §7), sin copias por frame fuera de la cocina.
- **Doble buffer de copperlist** (`copper::DoubleBuffer`): reescribir en ejecución una lista
  única puede partir un `WAIT` y perderse los armados de media pantalla; el swap de `COP1LC` se
  estrena en el VBlank.
- Los sprites van **delante del playfield** (`BPLCON2=0x0024` de `emit_planes_display`; AHRM
  cap. 7 Table 7-2).

## Estado: validada

- **HOST-072** `test_compose_attached_pair`: dos placements por par, `ATTACH` solo en el impar,
  DATA cocinada del frame vigente, rechazo sin pool y degradado a BOB una sola vez.
- **HOST-428** `emit_placements_into`: armado temprano compartido y rearme vertical del canal
  reutilizado.
- **Rendimiento**: `measure-fps` da **49,92 fps** emulados y 142 102 ciclos/frame
  (`fieldsPerFrame` 1,002; un frame por VBlank PAL). **Perfil por frame** (`.amigaprofile`):
  `profileCycles` idéntico en los 8 frames (sin frames de 2 VBlanks ni picos de procesamiento),
  DMA estable (±120 ciclos) y el código propio de la demo es una fracción mínima de las
  muestras de CPU (el resto es la espera de VBlank del engine y tareas de AmigaDOS).
- **Secuencia** (8 frames, 150 ms): los 4 actores presentes en todos los frames (las fusiones
  de clúster del análisis por píxel son solapes de objetos, no desapariciones), colores
  estables, posiciones cambiando **por toda la pantalla** y `frame-diff` con cambios confinados
  a los objetos en movimiento (SSIM ≈0,98; fondo estable).
- **Visión local (Ollama, qwen3-vl)**: por frame, gemas multicolor (sin pérdida de tonos) y
  chispas de 3 colores, sin cortes, parpadeos, huecos ni basura. Su afirmación de «no hay
  movimiento» en stills la refuta el gate determinista (bboxes de los 4 objetos cambian en cada
  frame, cubriendo la pantalla).

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/216_attached_actors --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/216_attached_actors
```

## Referencias

- AHRM 3.ª cap. 4 («Attached Sprites», Table 4-5), cap. 7 (Table 7-2, prioridad).
- `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md` §6–§8 (pares, animación y reparto).
- `docs/reference/emulators/winuae/sprite-dma.md` (estructura con cabecera).
- `../WinUAE-DBG/drawing.cpp` (~4239, `col = v + 16` en *attached*).
- `tests/host/scene/072_actor` y `tests/host/graphics/428_sprite_object_arm`.
