# HOST-264: glifos cirílicos en `Font8`

Test host de `eng/graphics/font8.hpp`: verifica el **alfabeto cirílico ruso** que usa
`draw_text`/`draw_codepoints` (la fuente por defecto del engine).

## Qué comprueba

1. **Cobertura**: las 64 letras `U+0410..U+044F` (А..Я, а..я) tienen glifo no vacío.
2. **Formas compartidas** con el latín usan el mismo dibujo: `А=A`, `В=B`, `Е=E`, `К=K`,
   `М=M`, `Н=H`, `О=O`, `Р=P`, `С=C`, `Т=T`, `Х=X` y `а=a`, `е=e`, `о=o`, `с=c`, `х=x`.
3. **Glifos propios** donde la forma difiere: `Б`, `И`, `Я` no coinciden con `B`/`H`/`R`.
4. **`Ё`/`ё`** (`U+0401`/`U+0451`) se componen como base (`Е`/`е`) + diéresis.
5. **Fuera de rango** (`U+0400`, `U+0450`, `U+0100`) devuelve 0.
6. **UTF-8**: `eng::utf8::decode` entrega el code point correcto para un literal cirílico
   (p. ej. `"Привет"` → `U+041F U+0440 …`) y ese code point tiene glifo.

## Salida de referencia

```
OK: Font8 con cirilico ruso validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/264_font_cyrillic
```
