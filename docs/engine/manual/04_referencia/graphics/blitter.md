# Referencia — BOBs y sprites

El chipset ofrece dos formas de poner objetos en pantalla: **BOBs** (bitmaps copiados por el Blitter) y **sprites de hardware** (8 canales DMA). La regla del repo: **BOB ≠ polígono** — un BOB se dibuja copiando un bitmap pre-renderizado; el Blitter poligonal (line-draw + area-fill) es para relleno vectorial/3D.

## `Bob` — `bob.hpp`

`Bob` (`bob.hpp:93`) es un objeto de bitmap (**copia** de planos + máscara opcional) con desplazamiento fino por *barrel shifter*, parametrizable por profundidad, layout y algoritmo de dibujo/borrado. **No posee memoria**: describe una hoja de frames y se materializa como `BlitJob`s en un `FramePlan`. Con planos **intercalados** el objeto es **un blit** (altura = alto × planos); en **planar** son N (uno por plano).

| Tipo | Qué es |
|---|---|
| `BobDraw` (`:51`) | Algoritmo de dibujo: aditivo (`D = A \| D`, `$FC`, sin máscara), cookies/opaco. |
| `BobErase` (`:65`) | Borrado: `ClearRect` (blit de borrado), `RestoreUnder` (save-under: la capa de actor/intención emite `RestoreRect` + `CopyRect` con `bob_save_box`/`bob_restore_box`) o ninguno (objetos aditivos). |
| `BobLayout` (`:77`) | Alias de `PlaneLayout`. `BobMaskPack` (`:80`). |
| `BobTarget` (`:113`) | Alias `BitmapView<PlaneTag, Chip>`; `make_bob_target(...)` (`:125`). |
| `save-under` (`bob_erase_box`/`restore`) | La caja procesa `base + (shift != 0)` palabras — las **mismas** que el dibujo: borrar solo `base` deja hasta 15 px por fila sin limpiar (residuo en el borde derecho). |

Contrato de la **hoja**: por cada fila de cada plano, `base + 1` palabras (`base = (width+15)/16`); la última es **guarda** (absorbe la lectura desplazada). `frame_stride` separa frames (0 = denso). Verificación: test host `072_actor` + demo `086_bob_objects` (gate visual).

## Sprites de hardware — `sprite.hpp`, `sprite_manager.hpp`

`SpriteManager` gestiona los 8 sprites DMA a nivel de escena: guarda la config, reserva su Chip RAM y emite los MOVEs (`SPRxPT`/`SPRxPOS`/`SPRxCTL`) en la copperlist. `SpriteConfig` declara `data`/`width_words`/`height`/`hpos`/`vstart`/`vstop`/`attach`. El camino canónico de emisión de objetos es `emit_placements_into` (placements del compositor con rearme vertical); `emit_armed_into`/`emit_template_into` son drivers de demos y `emit_into` es legado (ver `ROADMAP_JUEGO_SPRITES_BOBS.md` §4.1).

Los **pares attached** (0+1, 2+3, 4+5, 6+7) comparten sus 3 `COLORxx` (cambiar el color de un canal afecta a su par: *Color Bleed*) y dan 4 bits/píxel (15 colores) sobre `COLOR16-31`; reducen los canales útiles de 8 a 4. La prioridad frente a los playfields la fija `BPLCON2` (`PF1P`/`PF2P`); entre sprites, el orden de canal.

`sprite.hpp` aporta las **plantillas portables** (sin registros) que traducen una imagen a segmentos reutilizables (`HwSpriteSegment`, `:37`, reuso vertical/multiplexado), cambios de paleta por franja (`HwSpritePaletteSwitch`, `:47`, *color multiplexing*) y attached pairs (`HwSpriteTemplate`, `:60`). `SpriteAllocator` (`sprite_allocator.hpp:42`) procesa las plantillas y decide canales/multiplexado (`SpriteSlot`, `:26`). `SpriteCollisionConfig`/`encode_clxcon`/`decode_clxdat` (`sprite_collision.hpp:26`, `:38`, `:74`) configuran y leen la colisión de hardware (`CLXCON`/`CLXDAT`, AHRM cap. 7).

## Assets y animación

`sprite_asset.hpp` define `Sprite` (`:39`): un BOB con nombre de dominio (geometría + máscara + hoja). `anim.hpp` define `Anim` (`:17`): una secuencia de frames de la hoja; `animation.hpp` la reproducción.

## Intención de objeto — `raster_intent.hpp`

`Visual` es la **intención** portable de un objeto (no un `Bob`/`Sprite`: no son duplicados); `SpriteIntent`/`SpriteIntentSet` (`sprite.hpp:104`) agrupan las intenciones de sprite. `RasterIntent` y `safe_blitter_window` (`raster_intent.hpp:144`) relacionan el trabajo de Copper con el de CPU (el Blitter es único).

Volver al [índice de `graphics/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
