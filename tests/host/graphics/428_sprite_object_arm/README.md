# HOST-428 - `SpriteManager::arm_object` (armado de objeto de sprite)

Test host del **armado de un objeto de sprite** del engine
(`eng/graphics/sprite_manager.hpp`): escribe `SPRxPTH/L` a la DATA, `SPRxPOS` y `SPRxCTL`
con `VSTOP` exclusivo (AHRM cap. 4, «la línea siguiente a la última visible») y `ATTACH`
(bit 7) solo en el canal impar. Es el patrón validado en vivo por la demo
`214_attached_object` (par *attached* de 15 colores + chispas de 3 colores) y el usado para
los objetos de la 208.

Cubre:

- **Par *attached*** (canales 0/1): misma `SPRxPOS`, `VSTOP = y+alto`, `ATTACH` solo en el
  impar, `SPRxPT` apuntando a la DATA de cada canal, y canales no armados intactos.
- **Paridad de la X**: `SPRxPOS` lleva `HSTART>>1` y el bit 0 de `SPRxCTL` lleva `HSTART[0]`.
- **`SpriteManager::emit_armed_into`**: un solo `WAIT` en la línea de armado y el armado de
  todos los canales habilitados, sin `WAIT` por `VSTART` (patrón de objetos).
- **`SpriteManager::emit_placements_into`**: primera config de cada canal en la línea de
  armado compartida y **rearme vertical** de un canal reutilizado por el allocator en otra
  franja (multiplexado), en orden no decreciente de `vstart` (demo `216_attached_actors`).
- **Rechazos sin emisión**: canal ≥ 8, DATA vacía y alto 0.

Es host (sin hardware): la copperlist se construye en un bloque Chip del arena y se
inspeccionan los MOVEs. El `main.cpp` usa `printf` solo para informar.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/428_sprite_object_arm   # solo este
bash tools/run-host-tests.sh                                            # todos
```

## Referencias

- AHRM 3.ª, cap. 4 (posición/control de sprite, `VSTOP`).
- `docs/reference/emulators/winuae/sprite-dma.md` (estructura con cabecera) y
  `winuae/sprite-color-priority.md` (`BPLCON2`).
- Demo `demos/techniques/amiga/sprites/214_attached_object`.
