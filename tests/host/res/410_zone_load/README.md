# HOST-410 — cadena de zona (`Vfs` → `.engz` → HUNK)

Integra las piezas de R6 (versión host de R6.7): un HUNK con símbolo se envuelve en **`.engz`**, se
sirve por una **`Vfs`** (backend simulado), se lee completo, se **decodifica** y se **carga**
(segmentos + relocaciones + símbolos). Es "cargar un overlay desde disco" end-to-end, sin emulador.

## Qué comprueba

- `Vfs::read_all` del `.engz` (path con `.`/`..` normalizado) y `exists` de un inexistente.
- `decode_engz` (Raw) → bytes del HUNK.
- `HunkImage::load` → 1 hunk CODE, símbolo `hero` resuelto y dato copiado.

## Nota

La demo de zona **en hardware** (`210_zone_resources`: prefetch + overlay + no bloquear el frame)
queda pendiente; necesita la E/S asíncrona (R6.2 cableada al `FileDone`).
