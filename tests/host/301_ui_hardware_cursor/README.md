# HOST-301: cursor por sprite de hardware (`eng::ui::HardwareCursor`)

Test host de `eng/ui/hardware_cursor.hpp`: un cursor de ratón 16×16 en un sprite Amiga. Encapsula
la **estructura DMA** (POS, CTL, DAT/DATB por línea y terminador) que Agnus lee vía `SPRxPT`, y su
emisión a la copperlist.

## Qué comprueba

1. **`bind`**: rechaza `nullptr` y buffers menores que `kBytes`; acepta uno válido.
2. **`set_bitmap`**: escribe DAT/DATB por línea y deja el terminador DMA nulo.
3. **`set_position`**: codifica POS (`VSTART<<8 | HSTART[8:1]`) y CTL (`VSTOP<<8 | HSTART[0]`),
   incluido el bit 0 de HSTART con X impar.
4. **`emit_into`**: emite `SPR0PTH`/`SPR0PTL` → estructura y `DMACON` con SPREN.

## Notas

- No posee memoria: el llamador le da **Chip RAM** (los sprites solo ven Chip RAM).
- La demo 215 lo usa como cursor real; como la captura PNG del runner no incluye sprites, allí se
  valida por registros/copperlist.

## Salida de referencia

```
OK: cursor por sprite de hardware (estructura + emision) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/301_ui_hardware_cursor
```
