# HOST-409 — fachada `Vfs` (R6.1)

`eng/os/vfs.hpp` (`eng::os::Vfs<Backend>`): resuelve **paths normalizados** (`normalize_path`) y
expone `exists`/`size`/`read`/`read_all` sobre un **backend** (concepto; en host, uno **simulado en
memoria**). Errores normalizados (`VfsError`).

## Qué comprueba

- `exists` con paths absolutos/relativos, con `.`/`..`/separadores repetidos (todo normalizado) y
  escape por encima de la raíz → inválido.
- `read`/`read_all`: contenido, `NotFound`, `OutOfMemory` (destino pequeño).
