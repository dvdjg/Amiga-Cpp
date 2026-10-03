# HOST-400 — contenedor `.engz` (R6.4)

`eng/res/engz.hpp`: contenedor comprimido con cabecera que declara **codec, tamaños, alineación y
CRC-32**, sobre la etapa genérica `res::decode` (R6.5).

## Qué comprueba

- **Construir → parsear → decodificar** (vector ZX0 de HOST-271): el payload se recupera byte a byte.
- **CRC del payload**: un byte corrupto en el payload → `Result::Corrupt`.
- **Magic inválido** → `Result::InvalidArgument`.
- **Truncado** (cabecera o payload incompletos) → error.
