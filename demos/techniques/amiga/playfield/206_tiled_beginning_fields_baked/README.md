# 206 — Beginning Fields cocido desde Tiled (EHB) con XYLimited

Primera demo del pipeline Tiled: el proyecto `The Fan-tasy Tileset / Beginning Fields.tmx` se compone en host, se cuece a EHB y se reproduce en Amiga con la familia XYLimited. El objetivo es validar **contenido cocinado**, no parsear Tiled en runtime.

## Pipeline

```bash
# 1) Componer el bake (capas, grupos, objetos, sombras, primer frame de animaciones)
node tools/tiled/compose-map.mjs "C:/Users/dvdjg/Documents/programa/Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.cook-plan.json \
  --out out/assets/tiled/beginning-fields

# 2) Cocinar perfiles y promover EHB
node tools/tiled/cook-map.mjs "C:/Users/dvdjg/Documents/programa/Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.cook-plan.json \
  --out out/assets/tiled/beginning-fields \
  --profiles 16c,32c,ehb --dither checker --select ehb

# 3) Compilar y ejecutar
bash tools/build/build-demo.sh demos/techniques/amiga/playfield/206_tiled_beginning_fields_baked --debug --clean
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/206_tiled_beginning_fields_baked
```

La propuesta automática queda en `out/assets/tiled/beginning-fields/proposal.md`; `selected/` contiene `tiled_map.h`, `tilebank_indexed.h` y `tilebank.xlimited.bin` con el perfil elegido. La demo solo incluye esos assets: cambiar 16c/32c/EHB o imponer una paleta no requiere tocar el juego.

## Perfiles medidos

| Perfil | Planos | Tiles únicos | Banco interleaved | PSNR (checker) | Chip estimado |
|---|---:|---:|---:|---:|---:|
| 16c | 4 | 1208 | 153 KB | 25.17 | 201 KB |
| 32c | 5 | 1185 | 188 KB | 28.16 | 246 KB |
| **ehb** | 6 | 1183 | 225 KB | 28.53 | 293 KB |

EHB se recomienda por la capa de sombras (`Shadows`/`Object Shadows`) y los 121 colores únicos del bake: el sexto plano aporta los half-brights sin gastar una paleta plana de 64 colores. El informe lo decide el usuario/IA con los datos; `--palette` impone la paleta final y `--planes` su profundidad.

## Búsqueda previa

Se reutiliza la familia existente en vez de crear un renderer nuevo: `playfield::XlimitedScene` + `XlimitedSceneConfig` (single EHB, `AxisPolicy::Off` en Y), el formato de banco interleaved de `tools/ehb/emit-xlimited-bank.mjs` y el corte/cuantización de `tools/ehb/slice-tiles.mjs`/`quantize-ehb.mjs`. `scene_cfg.map.cells` usa el mismo contrato `TileMap` que `WorldTileMapView`/`TileLayerMap`; `blocks_prebuilt` evita la conversión indexado→planar en el Amiga.

## Alcance

- **Verificado**: scroll horizontal continuo, 49,92 fps en A500_debug, imagen EHB coherente con el bake de Tiled.
- **No incluido aún**: capas `Overlay`/`Occluder` en runtime, animación de tiles por Blitter, objetos como actores y MapBundle/UAF-R. Esas fases son T5/T6 y se validarán en las demos 207/208.
