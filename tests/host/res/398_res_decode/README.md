# HOST-398 — decode genérico (`eng::res`)

`eng/res/decode.hpp` (`eng::res::decode`): la **etapa genérica de decodificación** de un blob
comprimido → bytes, compartida por el loader de assets y el codec de audio
(`ROADMAP_RESOURCES.md` R6.5, `.engz` R6.4).

## Qué comprueba

- **`Codec::Raw`**: copia `min(src, dst)` bytes.
- **`Codec::Zx0`**: descomprime el vector del compresor de referencia (el mismo de HOST-271) byte a
  byte, y devuelve `-1` si no cabe en el destino.
- **Codec desconocido**: `-1`.

## Nota

`eng/res/zx0.hpp` (`eng::res::zx0`) contiene el descompresor ZX0 **genérico**; `eng/audio/zx0.hpp`
se conserva como alias (`eng::audio::zx0`) para no romper el codec de audio.
