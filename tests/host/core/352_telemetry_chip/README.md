# HOST-352 - telemetry_chip

Test de `eng/debug/telemetry.hpp` y `eng/memory/chip_storage.hpp`.

## Qué cubre

- **Panel de telemetría** (`eng::debug::draw_telemetry`): compone líneas (`FPS`/`FRAME`/`CHIP`/`SLOW`/`FAST`)
  con `eng::util::StaticString` + `to_chars_u32` (decimal sin división) y las envía a un *sink* con
  `text(x, y, cstr, rgb)` —el mismo contrato que el overlay del depurador—, sin tocar la escena.
  Se verifica con un sink falso que captura las líneas.
- **`ChipStorage<Tag, Bytes>`** (`ENG_CHIP_RAM`): búfer estático certificado en Chip RAM; `view()`
  devuelve la vista de dominio (`Bytes<Tag>`), `address()` da la `Address<MemoryKind::Chip>` y
  escribir por la vista afecta al almacén.

En host `ENG_CHIP_RAM` es un no-op (no hay mapa Amiga); el test comprueba el **tipo** y la API, no la
colocación física (que es del target m68k).

## Cómo se ejecuta

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/352_telemetry_chip
```
