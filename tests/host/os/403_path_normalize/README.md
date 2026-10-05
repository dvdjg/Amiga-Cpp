# HOST-403 — normalización de paths (R6.1)

`eng/os/path.hpp` (`eng::os::normalize_path`): función **pura** que colapsa separadores, resuelve
`.`/`..` y rechaza escapar por encima de la raíz. Es el ladrillo de normalización del VFS.

## Qué comprueba

- Relativo/absoluto, colapso de `//`, `.`, `..`, dispositivo (`DF0:`) preservado, raíz `/`.
- Errores: vacío (`Empty`), escape por encima de la raíz (`EscapesRoot`), destino sin cabida
  (`TooLong`).
