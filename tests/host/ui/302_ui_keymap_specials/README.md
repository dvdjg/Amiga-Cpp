# HOST-302: teclas comunes del keymap vs la AHRM 3.ª

Test host de `eng/ui/keymap.hpp`: valida `rawkey_to_key` para las **teclas comunes** (0x40–0x5F)
contra la tabla *«RAW Keycodes 40-5F hex (Codes common to all keyboards)»* del **Amiga Hardware
Reference Manual 3.ª** (`docs/reference/ahrm/`).

## Qué comprueba

| Rawkey | Tecla (AHRM) | Resultado |
|---|---|---|
| 0x40 | Space | `' '` (imprimible) |
| 0x41 | Backspace | `kKeyBackspace` |
| 0x42 | Tab | `kKeyTab` |
| 0x44 | Return | `kKeyReturn` |
| 0x45 | Escape | `kKeyEsc` |
| 0x46 | Delete | `kKeyDelete` |
| 0x4C | Cursor **arriba** | `kKeyUp` |
| 0x4D | Cursor **abajo** | `kKeyDown` |
| 0x4E | Cursor derecha | `kKeyRight` |
| 0x4F | Cursor izquierda | `kKeyLeft` |

## Hallazgos corregidos

- **Space (0x40) no estaba mapeado** (la tabla base 0x00–0x3F no lo incluye): ahora devuelve `' '`.
- **Cursores arriba/abajo invertidos**: la AHRM da `0x4C = up` y `0x4D = down`; el código los tenía
  al revés.

## Notas

Las **posiciones base** (0x00–0x3F) son el QWERTY US posicional de la AHRM. La asignación de
carácter de cada distribución nacional y los Alt+tecla de las teclas muertas son *best-effort*
hasta volcarlos de `DEVS:Keymaps` del ROM (no disponibles en el repo).

## Salida de referencia

```
OK: teclas comunes del keymap (AHRM 40-5F) validadas.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/302_ui_keymap_specials
```
