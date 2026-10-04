# 208_risky_woods — fondo por sprites *Risky Woods* (en verificación)

Demo del **reparto de canales de sprite por ventanas de reprogramación**
(`docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`): un fondo por sprites usa **6 canales**
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
- **Reparto**: el fondo reserva 2..7 en el `SpriteChannelLedger` con `plan_sprite_windows`;
  `SpriteAllocator::assign` da a los objetos los canales libres (0 y 1).

## Estado

- `build` y `run` OK; el reparto es correcto (`detail = 0x0300`: libre-mask 0b11,
  canal de objeto 0).
- **Renderizado**: el fondo de **ladrillos** cubre 320 px **sin huecos** y hace **scroll**
  (1 px cada 2 frames, con envolvente del patrón). Un **objeto con forma** (rombo) se
  dibuja delante y se mueve. Verificado con captura y visión (Ollama): sin columnas
  negras internas.
- Copper emitido (decodificado y test host HOST-417): `WAIT` por período + 6 MOVEs de
  `SPRxPOS`, canales ciclando 2→…→7→2 en pasos de 16 px, sin reescribir `SPRxCTL`.
- **Pendiente**: el **segundo objeto** (canal 1) no llega a verse: los dos comparten
  `COLOR17..19` (par 0/1) y su forma debe salir de un plano distinto (DATB) para tener
  color propio; queda por confirmar el armado del canal 1 con la sonda de registros en
  caliente (la última lectura se hizo contra una instancia sin READY).

## Siguiente

- Segundo objeto visible (color propio del par) y más objetos degradando a BOB.
- Intervalo inferior con **todos los canales libres** (otra técnica), como pide el modelo
  por ventanas.
- Animación HW del bitmap (Jim Power): inyectar `SPRxDATA/DATB` en caliente.

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --sequence-frames 4
```
