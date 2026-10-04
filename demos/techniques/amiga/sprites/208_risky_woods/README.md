# 208_risky_woods — fondo por sprites *Risky Woods* (en verificación)

Demo del **reparto híbrido de canales de sprite por franjas**
(`docs/engine/architecture/SPRITE_BANDS.md`): un fondo por sprites usa **6 canales**
(2..7) y deja **2 libres** (0..1) para objetos tradicionales, todo por encima de un
playfield de color 0.

## La técnica

- **Fondo** (`effects::RiskyWoodsLayer`): 6 canales contiguos forman un patrón de
  columnas de 16 px (período 96 px). El canal se **arma una vez** por DMA — su
  estructura en Chip RAM lleva cabecera `POS`+`CTL` y las 96 líneas de DATA — y el
  Copper **solo reposiciona `SPRxPOS`** (`emit_sprite_horizontal_reposition`) para
  redibujar el patrón a lo ancho. No reescribe `SPRxCTL` (desarmaría el sprite). Ver
  [sprite-horizontal-multiplex.md](../../../../docs/reference/amiga/techniques/sprite-horizontal-multiplex.md).
- **Objetos** (canales 0/1): sprites hardware que rebotan; su `SPRxPOS` de la cabecera
  DMA se parchea cada frame (el DMA la relee al armar el sprite).
- **Reparto**: el fondo reserva 2..7 en el `SpriteChannelLedger` con `plan_sprite_bands`;
  `SpriteAllocator::assign` da a los objetos los canales libres (0 y 1).

## Estado

- `build` y `run` OK; el reparto es correcto (`detail = 0x0300`: libre-mask 0b11,
  canal de objeto 0).
- **Renderizado**: el fondo **cubre 320 px sin huecos** (Un `WAIT` al inicio + una
  reposición de `SPRxPOS` por período con los canales ciclando 2→…→7→2, paso 16 px y
  `head_start` de 24 px). Los **dos objetos** (canales 0/1) se dibujan delante y se
  mueven. Verificado con captura y con visión (Ollama): la banda no tiene columnas
  negras internas.
- El copper emitido es: `WAIT` por período + 6 MOVEs de `SPRxPOS`; un canal nunca se
  reutiliza a menos de 96 px (muy por encima del mínimo ≈24 px).

Evidencia: `out/run/208_risky_woods/A500_debug/screenshot.png` y `sequence/`; registros
vivos `SPR0/SPR1` armados (`hstart=40/240, vstart=120, vstop=136`).

## Siguiente

- Añadir **scroll** (`set_scroll`) con el wraparound del patrón.
- Contenido gráfico real (no solo columnas planas de color).
- Demostrar en la franja inferior una banda con **todos los canales libres** (otra
  técnica), como pide el modelo híbrido.

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --sequence-frames 4
```
