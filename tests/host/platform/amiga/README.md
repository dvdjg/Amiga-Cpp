# Tests HOST — platform/amiga

Categoría `platform/amiga` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-006 | [input_decode](006_input_decode/README.md) | `eng::amiga::decode_joystick`: decodificación de los bits de `JOYxDAT` en direcciones (AHRM cap. 8) — backend de entrada. |
| HOST-007 | [input_cd32](007_input_cd32/README.md) | `eng::amiga::decode_cd32_buttons`: decodificación del flujo serie CD32 (9 bits → botones) — backend de entrada. |
| HOST-014 | [object3d](014_object3d/README.md) | `eng::object3d`: port 1:1 de `lib3d` (`Object3D`, transform, aristas/caras). |
| HOST-051 | [object3d_affine](051_object3d_affine/README.md) | Bit-exactitud de `update_object_transformation` (barrido de ángulos). |
| HOST-176 | [or_blob_batch](176_or_blob_batch/README.md) | `eng/platform/amiga/blob.hpp`: `OrBlobBatch` — secuencia de registros del lote de BOBs OR intercalado de `bobs3d` (`begin/one/end`), host-testable por inyección de la base de registros. |
| HOST-235 | [hw_info](235_hw_info/README.md) | Inventario de hardware `eng::hw` (`eng/hw/info.hpp`): consultas de capacidad, display, nombres y heurísticas (modelo, RAM, RTC). |
| HOST-259 | [floppy_mfm](259_floppy_mfm/README.md) | Disquete: decodificación **MFM** (`eng/os/floppy.hpp`) — `mfm_decode_long` inverso y `floppy_find_sector` sobre una pista AmigaDOS sintética (encoder = el del emulador). |
| HOST-308 | [os_cd32_pad](308_os_cd32_pad/README.md) | Mini-SO: decodificador del pad CD32 (`cd32_mask_from_shift`: stream serie → bitmask `Cd32Btn`). |
| HOST-330 | [asset_backend](330_asset_backend/README.md) | Recursos/plataforma: `AssetCacheBackend` (`eng/platform/amiga/asset_backend.hpp`) — `alloc` Chip/Slow, `free` no-op y `load` (`file_read_async` + cookie `IoUser{'A',id}`); integración con `res::AssetCache`. |
