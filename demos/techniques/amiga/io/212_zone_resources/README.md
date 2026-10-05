# 212 — recursos de zona (`Vfs` → `.engz` → overlay)

**Transición de zona**: el código de la zona se carga **desde disco** por la fachada **`Vfs`** (paths
normalizados sobre `dos.library`), se decodifica el contenedor **`.engz`**, y el overlay (HUNK) se
**carga, ejecuta y descarga**. El juego no ve handles de DOS, punteros de memoria ni el formato del
recurso (R6.1/R6.4/R6.6/R6.7 de `ROADMAP_RESOURCES.md`):

```cpp
vfs.read_all("data/code/answer.engz")            // VFS (normaliza el path)
  → eng::res::decode_engz(...)                    // contenedor comprimido (CRC)
  → dl.load(lib, bytes, &pool)                    // DynLoader (HUNK, reloc + símbolos)
  → dl.symbol(lib, "answer")()                    // ejecuta el export → 42
  → dl.unload(lib)                                // la zona termina: se descarga
```

El resultado (`answer=42`) se dibuja con el overlay de depuración del backend.

## Volumen

El runner monta `out/fs/content` (generado por `node tools/fs/make-volume.mjs`) en `DH1:`; ahí vive
`data/code/answer.engz` (el HUNK envuelto en `.engz`).

```bash
node tools/fs/make-volume.mjs --no-adf
bash ./tools/build/build-demo.sh demos/techniques/amiga/io/212_zone_resources --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/io/212_zone_resources --warp
```

## Pendiente

Carga **asíncrona** (prefetch que no bloquee el frame): hoy la cadena es síncrona (un frame); el
`RequestTable` (R6.2) + `file_read_async` ya existen para hacerla en fondo.
