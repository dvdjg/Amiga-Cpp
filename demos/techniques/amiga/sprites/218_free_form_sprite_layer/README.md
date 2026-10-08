# 218_free_form_sprite_layer — «SPR Layer» (Jeroen Knoester, 2018), recreación fiel

Port **funcional 1:1** de la demo de referencia **«SPR Layer»** (`../spr_layer/Sprite_Layer`,
Jeroen Knoester, 2018): una **capa de fondo free-form de sprites** que cubre toda la pantalla
(304 px, sin patrón repetitivo) **detrás** de un playfield de 4 planos (15 colores) que hace un
scroll horizontal de 1 px/frame, con **9 BOBs** de 32x32 rebotando encima y un **sub-buffer** de
3 planos con su propia paleta en las 16 líneas inferiores.

No es una variante: se transcriben el mismo reparto de trabajo por frames, las mismas
copperlists, los mismos blits y los mismos datos (tiles de Turrican II, BOBs, fuente 8x8 y
paletas de la referencia). La demo sustituye a la antigua `207_sprite_layer` (una versión
simplificada de 128 px que no reproducía el efecto).

## El efecto (qué se ve)

1. **Pantalla de título**: la capa de sprites + el texto de la referencia en el playfield. La demo
   **arranca el efecto inmediatamente** al cargar (la referencia original espera al botón izquierdo
   del ratón; aquí no se espera ninguna entrada, por petición explícita).
2. **Efecto en marcha** (1 frame por VBlank):
   - el **playfield** (texto y patrón de puntos) se desplaza **1 px/frame** hacia la izquierda;
   - la **capa de sprites** avanza **0.5 px/frame de forma uniforme** (offset base de posición
     +1 = 2 px cada 4 frames con vuelta a 0 tras 7, y el toggle de `SPRxCTL` cada 2 frames que
     pone el píxel impar), y **cada columna entra con su propia imagen** (no repetitiva);
   - los **9 BOBs** (4 planos, cookie-cut) van en dos filas (`y` y `224-y`) que rebotan entre
     `y=16` e `y=224`, con jitter horizontal de 15 px y separación de 24 px;
   - la **barra del sub-buffer** (3 planos) queda estática abajo.

## La técnica (por qué funciona)

Los 8 canales de sprite cubren, sin trucos, solo 128 px. La capa cubre **304 px** con **19
columnas de 16 px**: 8 columnas las dibuja el **DMA** (una estructura de sprite por canal, 224
líneas) y 11 el **Copper**, que **rearma** cada canal dentro de la línea reescribiendo
`SPRxPOS`+`SPRxDATB`+`SPRxDATA` (**sin** `SPRxCTL`: escribirlo desactiva el comparador;
`SPRxDATA` es quien arma) y, al final de la línea, **reposiciona los 8 canales a la izquierda en
orden inverso** para el renglón siguiente. Claves y citas en
[`docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`](../../../../docs/reference/amiga/techniques/sprite-horizontal-multiplex.md)
y [`sprite-layer.md`](../../../../docs/reference/amiga/techniques/sprite-layer.md).

La parte cara (la DATA de las columnas) se **reparte en frames** para no agotar el bus:

| Pieza | Reparto (transcrito de `GFX/layer.asm`) |
|---|---|
| `UpdateSprCtl` | `SPRxCTL` alterna `$0c03`/`$0c02` cada **2 frames** (píxel impar) |
| `UpdateLayerPos` | las 19 posiciones en **4 frames** (cuartos de columna, 223/224 líneas) |
| `UpdateLayerData` | la DATA en **32 frames**: canales 0-7 por DMA (frames 0-7), columnas Copper 1-11 (8-18) y la segunda lista (19-29); el frame 30 retrocede 18 columnas el offset del tilemap |
| `UpdateFGTiles` | un cuarto de tile de 32x32 por frame (28 frames), en los **3 buffers** de FG |

Cuatro copperlists (`clist1..4`) con **dos pares** que se alternan cada 32 frames y **doble
buffer de estructuras de sprite** (`spr0..7`/`spr0b..7b`): la CPU escribe siempre la lista/el
juego que **no** se muestra, y `COP1LC` se publica por frame sin `COPJMP` (el Copper recarga en
el VBlank). El playfield tiene **3 buffers** (`fg_buf1..3`, 352x338 px, 4 planos intercalados):
1/2 se alternan cada 256 frames y el 3.º es la copia limpia de la que el Blitter **restaura** el
fondo bajo los BOBs antes de redibujarlos.

Cláusulas de hardware que sostienen el efecto (verificadas en la fuente del emulador,
`../WinUAE-DBG/`, citadas en `src/main.cpp`):

- VSTART/VSTOP de sprite son de 8 bits con **wrap** (`custom.cpp:4479`): las estructuras usan
  VSTART=44 y VSTOP=12 → 224 líneas; el estado de DMA se arma/desarma por **igualdad** de línea
  (`custom.cpp:10065-10077`).
- Escribir `SPRxPOS` a media línea **no desarma** el sprite (`sprstartstop`,
  `custom.cpp:4018-4030`): el rearmado puede usar `VSTART=$7f` y seguir pintando.
- El HSTART del sprite es `(pos&0xff)*2 + (ctl&1)` (`drawing.cpp:2706`): el bit 0 de `SPRxCTL`
  da el píxel impar del scroll.

## Estructura de la copperlist (una de las 4)

```
cabecera (AGA, DMACON, DIW, DDF, BPLCON0=$4200, BPLCON2=0)
SPR0..7PT (16 palabras)                     ← doble buffer (spr0..7 / spr0b..7b)
WAIT línea 42
COLOR00..31 (32 colores)                    ← paleta de la referencia
BPL1..4PT + BPL1MOD/BPL2MOD=138 + BPLCON1   ← playfield 4 planos (intercalado, 44 B/plano)
por cada una de las 224 líneas:
  WAIT (H=72 px lo-res)
  11 × [SPRxPOS, SPRxDATB, SPRxDATA]        ← columnas 8..18 (canales k%8), 6 palabras por columna
  8  × [SPRxPOS]                            ← reposición a la izquierda (orden inverso 7..0)
línea 268: DMACON off (BPLEN) → WAIT hpos $c5 → DMACON on
DDF $40/$c8, MOD=72, BPLCON0=$3200 (3 planos), BPLCON1=0
BPL1..3PT del sub-buffer + COLOR01..07 (subpal) + BPLCON2 + END
```

Las palabras que se parchean por frame (las 19 `SPRxPOS`, la `DATA` de las 11 columnas Copper,
los 4 `BPLxPT` del FG, el `BPLCON1` del scroll fino) se localizan al construir la lista y se
reescriben con **Blitter** (`blitter_fill_words_strided` para las posiciones, paso 84 palabras)
o con la CPU (los punteros). La lista **no se re-emite** por frame.

## Assets

Los tiles (Turrican II), los BOBs, la máscara y la fuente 8x8 de la referencia viven en
[`assets/amiga/sprites/spr_layer/`](../../../../assets/amiga/sprites/spr_layer/README.md). Los
que lee el **Blitter** se copian a Chip RAM en `init` (el Blitter no ve Slow/Fast); la fuente
solo la lee la CPU.

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/218_free_form_sprite_layer --debug
# efecto en marcha + secuencia:
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/218_free_form_sprite_layer \
  --sequence-frames 6 --sequence-interval-ms 250
# 24 updates consecutivos (paso por update, determinista) para visión:
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/218_free_form_sprite_layer \
  --sequence-step-frames 24 --sequence-interval-ms 30
```

## Estado: VALIDADA (capa de sprites) / cadencia en línea con el original

- **Capa de sprites correcta**: el fondo (columnas Copper + DMA) cubre toda la pantalla sin zonas
  negras ni cortadas; pantalla de título **pixel-idéntica** al original (0 de 281 880 píxeles
  distintos, captura del runner, mismo encuadre 756x576) y efecto en movimiento verificado con
  visión («exactamente 9 bobs completos en cada frame, ningún frame roto»).
- **Dinámica**: scroll del texto 1 px/update (medido por correlación); **capa de sprites
  0.5 px/update uniforme** (medido por correlación enmascarada solo-fondo: −1 px cada 2 updates);
  BOBs con la trayectoria de rebote del original.
- **Cadencia**: el update cabe en **2 campos exactos** (284 204 ciclos) tras desactivar el servicio
  de blit de fondo (no-op con la cola vacía, ~130k ciclos/update) y pasar los lotes a escrituras
  de 32 bits: la tasa es ≈ **25 updates/s** en emulación ciclo-exacta, en línea con las ≈ 26 del
  original. Medido con los contadores del periférico de depuración; detalle en
  [`VALIDATION.md`](VALIDATION.md) §4.
- **Selector de buffer FG fiel al original**: el original selecciona el buffer con
  `btst #0,c32frame_cn_o+1` (`SPR_Layer.asm:378-398`) — el byte alto de un contador 0..31, siempre
  0: usa siempre `fg_buf2`. El port lo replica tal cual; un doble buffer por update produce un
  salto periódico de 8 px del playfield cada 16 updates (verificado contra la referencia con el
  watchpoint de `COP1LC`; §3.6 de la validación).

Nota: la demo **no vuelve a AmigaDOS** (el `takeover_display` del engine congela el SO) y **no
procesa entrada**: el efecto arranca solo al cargar. La captura/cierre los hace el runner.

## Referencias

- `../spr_layer/Sprite_Layer` — «SPR Layer», Jeroen Knoester (2018): `SPR_Layer.asm`,
  `GFX/layer.asm`, `GFX/tilemap.asm`, `GFX/blitter.asm`, `Data/copperlists.asm`.
- [`sprite-horizontal-multiplex.md`](../../../../docs/reference/amiga/techniques/sprite-horizontal-multiplex.md)
  — la técnica free-form (mecanismo, coste, límites).
- [`sprite-layer.md`](../../../../docs/reference/amiga/techniques/sprite-layer.md) — subsistema de
  capas de sprite del engine.
- [`docs/reference/emulators/winuae/sprite-dma.md`](../../../../docs/reference/emulators/winuae/sprite-dma.md)
  — la estructura DMA por canal y la columna fantasma sin terminador.
