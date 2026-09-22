# HOST-265: teclas muertas (composición de acentos)

Test host de la **composición de teclas muertas** en `eng/ui/keymap.hpp`: en Amiga las
letras acentuadas se teclean en **dos pulsaciones** (primero el acento muerto, p. ej.
Alt+H = agudo, y luego la letra; `keymap.library` las compone). Ver
`amiga-bootcamp/11_libraries/keymap.md`.

## Qué comprueba

1. **`compose`**: acento + letra → Latin-1 precompuesto (`agudo+e=é`, `tilde+n=ñ`,
   `dieresis+u=ü`, `cedilla+c=ç`, `anillo+a=å`, mayúsculas incluidas) y `0` si no combina.
2. **`dead_key_of`**: reconoce las teclas de acento con Alt (Alt+H agudo, Alt+K tilde,
   Alt+C cedilla) y no marca las normales.
3. **`rawkey_to_char`**: Alt+H deja el agudo pendiente y no produce carácter; la siguiente
   letra compone (`e → é`, `n → ñ`); si no combina, cae a la letra y descarta la muerta.
4. **`dispatch_msg`**: Alt+H seguido de `e` inserta `é` en un `EditBox` (entrada por
   mensajes end-to-end).

## Notas

- La **asignación Alt+tecla** es *best-effort* (el ejemplo documentado es Alt+H = agudo);
  queda pendiente validarla contra el keymap del ROM.
- El `EditBox` guarda **un byte por carácter** (Latin-1), así que las letras acentuadas
  Latin-1 entran bien; el **cirílico (U+04xx)** aún no se puede teclear en un campo
  (necesita UTF-8/ancho, pendiente).

## Salida de referencia

```
OK: teclas muertas (composicion de acentos) validadas.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/265_ui_deadkeys
```
