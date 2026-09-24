# WinUAE — Copper: autoridad de escritura (`CDANG`)

El Copper solo puede escribir **algunos** registros custom. La comprobación está en
`WinUAE-DBG/custom.cpp:2835-2846` (`test_copper_dangerous`):

```c
int addr = reg & 0x01fe;
if (addr < ((copcon & 2) ? (ecs_agnus ? 0 : 0x40) : 0x80)) {
    cop_state.state = COP_stop;   // detiene el Copper
    copper_enabled_thisline = 0;
    return 1;
}
```

- Con **`COPCON` bit 1 (`CDANG`) = 0**, el Copper no puede escribir ningún registro con
  `offset < 0x80` (incluye los del **Blitter**, `$DFF040-$DFF074`) y, si lo intenta, el Copper se
  **detiene** (`COP_stop`) — no ignora la escritura.
- Con **`CDANG` = 1**, el umbral baja a `0x40` (OCS) o `0` (ECS Agnus): el Copper ya puede
  escribir los registros del Blitter.

Consecuencia para el engine: `takeover_display` (`amiga.cpp`) activa `CDANG`
(`COPCON = $0002`) antes de arrancar la lista. Sin él, un `CopperIntentKind::BlitterJob` detenía
el Copper en la primera escritura (`BLTCON0`) y la lista nunca llegaba al final (síntoma: la
pantalla quedaba con el último color escrito y el blit no ocurría).

## Validación

- `demos/techniques/amiga/blitter/210_copper_blitter`: `BlitterJob` en el borde inferior copia 256 words; la copia
  se verifica (`RunStatus.detail = 0x21F00`) y el overlay muestra `copper blit: OK`.
- `tests/host/graphics/260_copper_blitter`: emisión (`BLTSIZE` al final) + ventana segura.

## Referencias

- `WinUAE-DBG/custom.cpp:2835-2846` (`test_copper_dangerous`); `custom.cpp:7293-7337`
  (`custom_wput_agnus`: los registros del Blitter van por la vía Agnus).
- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (Técnica A).
- `docs/reference/amiga/techniques/copper-timing-and-budget.md` §6.
