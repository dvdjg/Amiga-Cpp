# Sprites hardware (OCS/ECS/AGA)

Referencia del subsistema de sprites: formato, colores, **attached (15 colores)**,
prioridad, multiplexado (**vertical** y **horizontal**), **sprite-as-playfield**, colisión
de hardware y antipatrones. Sintetizada de `amiga-bootcamp/08_graphics/sprites.md` y del
AHRM (cap. 4), más el estado real del engine.

## 1. Qué son

Denise/Lisa compone hasta **8 canales DMA** sobre los playfields con **coste de CPU cero**:
Agnus lee la DATA de Chip RAM, Denise la dibuja y el Copper recarga los punteros cada
frame. Cada canal mide **16 px** (32/64 en AGA), **3 colores + transparencia** (15 en
*attached*). Los sprites consumen **16 de los ~226 slots** de bus por línea (~7 %).

## 2. Formato de datos (Chip RAM)

```
┌──────────────────────────────────────────┐
│ Header: SPRxPOS (VSTART/HSTART)          │
│ Header: SPRxCTL (VSTOP/control/ATTACH)   │
├──────────────────────────────────────────┤
│ DATA word  (bit 0 de cada píxel)         │  ← 16 px/línea
│ DATB word  (bit 1 de cada píxel)         │
├──────────────────────────────────────────┤
│ … repetir por línea …                     │
├──────────────────────────────────────────┤
│ Terminador: 0x0000, 0x0000               │  ← obligatorio
└──────────────────────────────────────────┘
```

- **Color del píxel** = `(DATB<<1) | DATA`: `00` transparente, `01`/`10`/`11` colores 1–3.
- **SPRxPOS**: `VSTART[7:0]` (bits 15-8), `HSTART[8:1]` (bits 7-0).
- **SPRxCTL** (AHRM cap. 4): `VSTOP[7:0]` (bits 15-8), **`ATTACH` (bit 7)**, bits 6-3 sin usar,
  **`VSTART[8]` (bit 2)**, **`VSTOP[8]` (bit 1)**, **`HSTART[0]` (bit 0)**. *Cuidado: el
  bootcamp `sprites.md` lista ATTACH en el bit 0 — es un error; el AHRM dice «bit 7 in sprite
  control word 2».*
- **X solo en píxeles lores pares** (HSTART va ÷2): en hires la posición es más gruesa que
  el playfield.

## 3. Colores por par

Cada **par** de canales comparte 3 registros (el color 0 es transparente):

| Par | Registros | Direcciones |
|---|---|---|
| 0–1 | `COLOR17`–`COLOR19` | `$DFF1A2`–`$DFF1A6` |
| 2–3 | `COLOR21`–`COLOR23` | `$DFF1AA`–`$DFF1AE` |
| 4–5 | `COLOR25`–`COLOR27` | `$DFF1B2`–`$DFF1B6` |
| 6–7 | `COLOR29`–`COLOR31` | `$DFF1BA`–`$DFF1BE` |

Cambiar el color de un canal afecta a **su par** (antipatrón *Color Bleed*).

## 4. Attached: 15 colores

Dos sprites del **mismo par** se unen poniendo **`ATTACH` en el CTL del impar**. El par
aporta bits 0–1 (par) y 2–3 (impar) → índice de 4 bits sobre `COLOR16–31`:

```
   independientes:  [S0: 3 col][S1: 3 col]          → 2 objetos de 3 colores
   attached:        [S0+S1: 15 col + transparencia]  → 1 objeto ancho/multicolor
```

- Reduce los canales útiles de **8 a 4**.
- **Se puede conmutar por zona**: como `ATTACH` vive en el CTL y el Copper puede reescribir
  `SPRxCTL` por línea (rearmado), una banda puede ir *attached* (15 colores) y otra
  *detached* (dos sprites de 3 colores) dentro del mismo frame.

## 5. Prioridad (BPLCON2)

Orden canónico (frente → fondo): `sprites 0-1 > 2-3 > 4-5 > 6-7 > playfield(s)`. Lo fija
`BPLCON2`: el ejemplo del AHRM (`MOVE.W #$0024,BPLCON2`, «sprites have priority over
playfields», AHRM 3.ª cap. 7) coloca `PF1P`/`PF2P = 100` (playfields **detrás** de los
cuatro grupos de sprites). **En modo single-playfield el campo que coloca el playfield es
`PF2P` (bits 5-3)**, no `PF1P` (AHRM, nota de Table 7-2: *«PF2P2-PF2P0 … are the priority
bits for normal (non-dual) playfields»*; confirmado con Grok, ver
[`consulta-bplcon2-single-playfield-priority-en.md`](../../debugging/investigaciones/consulta-bplcon2-single-playfield-priority-en.md)).
Ojo con el valor `000`: **invierte** la relación y pone el playfield **delante de todos los
sprites** (AHRM Table 7-2; los sprites solo se ven sobre los píxeles de color 0 del bitmap);
el `emit_planes_display` del engine usaba `000` y se corrigió a `$24` — ver
[`winuae/sprite-color-priority.md`](../../emulators/winuae/sprite-color-priority.md).
`BPLCON2` **no tiene valor de reset documentado** (indefinido): escríbelo siempre. Con
`PF1P`/`PF2P` intermedios los sprites pueden quedar **entre** playfields (p. ej.
personaje tras árboles de primer plano y delante del cielo).

## 6. Multiplexado

| Tipo | Qué reutiliza | Para qué | Detalle |
|---|---|---|---|
| **Vertical** | un canal en **otra Y** | >8 objetos en pantalla (separados ≥1 línea) | `SpriteRearm`; `SpriteAllocator` (first-fit) |
| **Horizontal** | un canal en **otra X**, misma línea | objeto >16/64 px, tramos, **sprite-as-playfield** | [sprite-horizontal-multiplex.md](sprite-horizontal-multiplex.md) |

## 7. Sprite-as-playfield

Los **8 canales rearmados horizontalmente** forman una capa ancha con scroll propio y
**coste cero de bitplanes** (3 o 15 colores, limitado por el presupuesto de Copper). Es la
técnica de *Jim Power*: el fondo sale de sprites y los bitplanes quedan para el primer
plano. Requiere mucha carrera Copper↔haz y memoria de copperlists.

## 8. Colisión de hardware (CLXCON/CLXDAT)

`CLXCON` (`$DFF098`) habilita qué pares sprite/bitplane colisionan; `CLXDAT` (`$DFF00E`)
devuelve el resultado y **se autolimpia al leer** (leer una vez por frame). Es
*pixel-perfect* pero **sin posición**: la lógica de juego suele usar cajas software y
dejar la colisión de hardware para efectos (bala-terreno).

**Colisión por software que ya tiene el engine** (buscar `collide`): `core/util/collision.hpp`
(geométrica/SAT: polígono, segmento, círculo; `tests/host/core/095_collision`, `125_convex_sat`),
`field::collide_cpu` (máscara **pixel-perfect** por CPU) y `Backend::blitter_collide` (la misma
comprobación por **Blitter**, minterm `B = A & D`; `field/raster.hpp` la documenta como
referencia CPU). La colisión de **hardware de sprites** (`CLXCON`/`CLXDAT`) está en
`graphics/sprite_collision.hpp` (`encode_clxcon`/`decode_clxdat`, AHRM Table 7-3/7-4), sin
necesidad de máscara en RAM.

## 9. Límites

- **Robo de slots con 5+ planos**: el fetch ancho puede comerse los slots de los canales
  **6 y 7** (y a veces 4-5) → usar canales bajos para lo crítico.
- **Datos en Chip RAM** siempre (el DMA no ve Fast RAM).
- X solo en píxeles pares lores.

## 10. Antipatrones

| Antipatrón | Por qué falla | Solución |
|---|---|---|
| **Phantom Sprite** | no recargar `SPRxPT` cada frame → el puntero queda tras el terminador | el Copper recarga `SPRxPTH/L` cada frame |
| **Missing Terminator** | sin `0x0000,0x0000` el DMA lee basura | terminador obligatorio |
| **Color Bleed** | el par comparte `COLORxx` | recordar que 2 y 3 usan los mismos registros |
| **Fast RAM Trap** | DATA en Fast RAM es invisible al DMA | `MEMF_CHIP` |
| **Stolen Pointer** | pisar el sprite 0 (ratón de Intuition) | usar canales 1-7 |

## 11. Estado en el engine

| Capacidad | Estado | Dónde |
|---|---|---|
| 8 canales, POS/CTL/PT | **sí** | `SpriteManager::emit_into`/`emit_config` |
| **Armado de objeto** (PT/POS/CTL temprano, `ATTACH` en el impar) | **sí** | `SpriteManager::arm_object` + `emit_armed_into`/`emit_placements_into` (HOST-428; demos 054/214/216) |
| Multiplexado vertical + color mux | **sí** | `SpriteManager::emit_template_into`, `HwSpriteTemplate`; por actores: `ActorDesc::sprite_template` (cadena al mismo canal) + `emit_placements_into` con `SpritePaletteEvent` (demos 053/217) |
| Asignación con degradado a BOB | **sí** | `SpriteAllocator` (first-fit; `as_bob`) |
| **Rearmado horizontal** | **sí** | `SpriteHorizontalRearm` + `Scheduler::emit_sprite_horizontal_rearm`, intent `SpriteRearm` |
| **Attached (15 colores)** | **sí** | `attach` en `SpriteConfig`/`HwSpriteTemplate`/`SpriteIntent`/`HwSpritePlacement` y cocinado de la DATA de 4 planos (`graphics/sprite_attached.hpp`, HOST-427); demos `214_attached_object` (a mano) y `216_attached_actors` (**end-to-end por actores**, secuencia + visión) |
| **Sprite DMA** (columna alta) | **sí** | `SpriteLayer` con `dma_channels` (estructura **con cabecera POS+CTL**; `SPRxPT`→cabecera) |
| **Sprite-as-playfield** (capa de fondo) | **sí** | `effects::SpriteLayer` (sobre el rearmado horizontal) |
| **Colisión hardware** (CLXCON/CLXDAT) | **sí** (utilidad + backend) | `graphics/sprite_collision.hpp`, `AmigaBackend::set/read_sprite_collision` |
| **Prioridad BPLCON2 por sprite** | parcial | `BPLCON2`/intent `Priority`, sin API de sprites |

## 12. Referencias

- `amiga-bootcamp/08_graphics/sprites.md` (y `01_hardware/ocs_a500/sprites.md`)
- [sprite-techniques-catalog.md](sprite-techniques-catalog.md) — catálogo de técnicas históricas con sprites hardware
- [sprite-tricks-games.md](sprite-tricks-games.md) — casos reales (Risky Woods, Jim Power, Agony, Shadow of the Beast, Free Form)
- [Free Form Sprite Layer](https://www.powerprograms.nl/amiga/spr-layer.html) (Jeroen Knoester) — capa de sprites *free-form* con scroll
- [sprite-horizontal-multiplex.md](sprite-horizontal-multiplex.md) — rearmado horizontal
- [WinUAE — colisión CLXCON/CLXDAT](../../emulators/winuae/collision.md) — fuente del emulador
  (`collision_level` + trampa par/impar del `match`)
- AHRM 3.ª, cap. 4 (Sprite); [índice](../../ahrm/amiga-hardware-manual-index.md)
- `engine/include/eng/graphics/{sprite_manager,sprite_allocator,sprite}.hpp`
