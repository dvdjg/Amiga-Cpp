# 214_attached_object - sprite *attached* de 15 colores

**Objetivo:** un **objeto de 15 colores** hecho con **dos canales de sprite** del
mismo par unidos por el bit `ATTACH` (AHRM 3.ª cap. 4, «Attached Sprites»,
Table 4-5): el canal par aporta los bits 0-1 del índice de color y el impar los
bits 2-3 (índice 0 = transparente, 1..15 = `COLOR17..31`). Ambos canales deben ir
siempre a la **misma X/Y**; separarlos cambia el color de los píxeles. La técnica
está en `docs/reference/amiga/techniques/sprite-layer.md` §4.

## Qué muestra

- **Núcleo (par 0/1, 15 colores)**: gema de 16x24 con una banda de color distinta
  por línea y brillo especular; dos frames de DATA que alternan (pulso) y rebote
  X/Y. Un canal solo daría 3 colores; el par da 15.
- **Contraste (canales 2..7, 3 colores)**: seis chispas de 16x16 en sus pares
  (2/3, 4/5, 6/7: verde, cian, magenta), con rebotes propios.

## Implementación

- Helper del engine **`eng/graphics/sprite_attached.hpp`** (`cook_attached_pair`):
  cocina el arte planar de **4 planos** en las **dos estructuras DMA**
  `[POS, CTL, DAT/DATB…, 0,0]` (par = planos 0-1; impar = planos 2-3 + `ATTACH`),
  con terminador. Salidas `ChipView<SpriteTag>` (DMA tipada por banco). Cubierto
  por **HOST-427**.
- Copperlist reconstruida por frame: display, `DMACON` con `SPREN`, paleta y, tras
  un `WAIT` temprano (línea 32, antes del primer `VSTART`), el armado de los 8
  canales con **`SpriteManager::arm_object`** (`SPRxPT` a la DATA, `SPRxPOS`,
  `SPRxCTL`; `VSTOP` exclusivo y `ATTACH` solo en el impar; HOST-428). Estructuras
  con cabecera válida (antipatrón *Phantom Sprite*, `sprite-layer.md` §10).
- Los sprites van **delante del playfield**: `BPLCON2=0x0024` del
  `emit_planes_display` (AHRM cap. 7, Table 7-2 y ejemplo `MOVE.W #$0024,BPLCON2`).

## Estado: validada

- **Captura única**: gema de 15 tonos + 6 chispas (3 por par), sin recortes.
- **Secuencia** (8 frames, 150 ms): conteos de color estables por frame y **MD5
  distintos** (animación), sin decaimiento.
- **Visión local (Ollama)**: análisis por elementos (forma, tamaño, color, cambio
  de posición entre frames, coherencia de trayectoria, glitches/parpadeo):
  «todos los elementos bien renderizados sin glitches, parpadeo ni sprites
  cortados».
- **Regresión** de 207/208/054/101 en verde tras el cambio de `BPLCON2` del
  scheduler; 053 y 206 revalidadas después (ahora sus sprites también van delante
  del playfield).

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/214_attached_object --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/214_attached_object
```

## Referencias

- AHRM 3.ª, cap. 4 («Attached Sprites», Table 4-5), cap. 7 (Table 7-2, prioridad).
- `docs/reference/amiga/techniques/sprite-layer.md` §4 (attached), §5 (prioridad), §10 (antipatrones).
- `docs/reference/emulators/winuae/sprite-dma.md` (estructura con cabecera).
- `docs/reference/emulators/winuae/sprite-color-priority.md` (prioridad, `BPLCON2`).
- `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md` §6.
- `tests/host/graphics/427_sprite_attached` (helper).
