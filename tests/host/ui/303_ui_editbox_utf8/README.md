# HOST-303: `EditBox` en UTF-8 (edición por code point)

Test host de `eng/ui/editbox.hpp`: el campo de texto guarda **UTF-8** (1–2 bytes por carácter) y
edita por **code point**, no por byte.

## Qué comprueba

1. **Inserción multibyte**: `insert_cp` codifica el code point (`П` U+041F → `0xD0 0x9F`,
   `é` U+00E9 → `0xC3 0xA9`); `len`/`caret` son desplazamientos de byte.
2. **Backspace**: borra el code point **anterior completo** (no un byte suelto).
3. **Delete**: borra el code point **bajo el caret** completo.
4. **Left/Right**: mueven el caret por code point (salta el par de bytes de un cirílico).
5. **Vía de eventos**: un `KeyDown` con un code point cirílico entra en el campo (`event_edit`),
   gracias a que `is_printable_key` cubre U+04xx.

## Notas

- `len`/`caret`/`view` siguen siendo **byte offsets** (sin heap); el dibujo decodifica UTF-8 y el
  caret se coloca por columna de code point.
- `eng::utf8::encode` (1–2 bytes) es la inversa de `eng::utf8::decode`.

## Salida de referencia

```
OK: EditBox UTF-8 (edicion por code point) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/303_ui_editbox_utf8
```
