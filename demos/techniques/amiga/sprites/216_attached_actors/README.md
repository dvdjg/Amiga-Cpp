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
  de animación (bandas concéntricas que pulsan y brillo especular móvil) y rebote triangular en X.
- **Dos chispas** de 3 colores (rombo y cruz, canales 4/5) que conviven en el mismo reparto: un
  par *attached* direcciona `COLOR17-31` completo (WinUAE `drawing.cpp` ~4239: `col = v + 16`),
  mientras que un sprite no-*attached* toma los 3 tonos de su par (`COLOR25-27` para 4/5).
- Los cuatro actores comparten una **banda vertical con intervalos solapados**: el allocator no
  reutiliza canales y cada objeto conserva su par de color (sin *color bleed*). El **rearme
  vertical** para franjas disjuntas sí está soportado por el emisor (`emit_placements_into`,
  cubierto en HOST-428) y lo usará un juego con objetos repartidos en Y.

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
- **Rendimiento**: `measure-fps` da **49,97 fps** emulados y 141 952 ciclos/frame
  (`fieldsPerFrame` 1,001; un frame por VBlank PAL). La demo marca el frame con
  `debug::mark_frame` para la telemetría.
- **Secuencia** (8 frames, 150 ms): los 4 actores presentes en todos los frames (las fusiones
  de clúster del análisis por píxel son solapes de objetos, no desapariciones), colores
  estables, `frame-diff` con cambios confinados a la banda de sprites (SSIM ≈0.99; fondo
  estable) y `analyze-demo` OK.
- **Visión local (Ollama, qwen3-vl)**: por elementos y por frame, sin sprites cortados,
  incompletos, parpadeando ni huecos negros/costuras; la variación de brillo señalada es el
  pulso de animación de las gemas (verificado con el frame-diff determinista).

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
