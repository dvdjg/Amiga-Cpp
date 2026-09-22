# HOST-263: GUI — keymaps nacionales (ES/FR/IT/DE/RU)

Test host de `eng/ui/keymap.hpp`: en Amiga el **rawkey es la posición física** y el OS
(`keymap.library` + `DEVS:Keymaps/*`) traduce a carácter con **variantes nacionales**. Ver
`amiga-bootcamp/11_libraries/keymap.md`.

## Qué comprueba

1. **US por defecto**: `0x15 → y`.
2. **Español**: `0x29 → ñ/Ñ`, `0x0C → ¡/¿`, `0x0D → ç`, `0x2B+shift → ª`.
3. **Alemán (QWERTZ)**: `0x15 → z`, `0x31 → y`, `0x1A → ü`, `0x0B → ß`, `0x29 → ä`.
4. **Francés (AZERTY)**: `0x10 → a`, `0x20 → q`, `0x11 → z`, `0x31 → w`, `0x02 → é`.
5. **Italiano**: `0x1A → è/é`, `0x0C → ì`.
6. **Ruso (cirílico)**: `0x10 → й/Й`, `0x24 → п`.
7. **Teclas especiales** (Return, flechas) comunes a todas; `dispatch_msg` aplica el `layout`.

## Limitaciones

- Tablas **best-effort** para el área principal (0x00–0x3F); **pendiente** validarlas contra los
  keymaps reales del ROM y **componer teclas muertas** (Alt+H = agudo).
- Los caracteres de **RU son cirílicos (U+04xx)**, fuera de la cobertura de `Font8`/`Font5x7`
  (ASCII+LATIN-1): el input los produce, el dibujo aún no.

## Salida de referencia

```
OK: GUI keymaps nacionales (ES/FR/IT/DE/RU) validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/263_ui_keymap_layouts
```
