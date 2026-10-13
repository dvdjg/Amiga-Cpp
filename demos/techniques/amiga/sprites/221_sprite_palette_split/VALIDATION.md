# VALIDATION — 221_sprite_palette_split (palette splitting por franja, F7/F4.3)

Informe de validación de la técnica F7/F4.3: dos anillos en un solo canal con paletas distintas por franja, arbitradas por el compositor. Campaña: 2026-10-13, A500_debug (WinUAE-DBG).

## Qué se pretendía

1. Dos anillos de 16×16 (mismo arte) en **un canal** (franjas disjuntas), cada uno con su paleta declarada.
2. El segundo anillo se pinta con **otra paleta** (conmutación por Copper al entrar en su franja), sin gastar canales ni planos.
3. 50 fps (un frame por VBlank) y sin artefactos.

## Medidas

| Métrica | Valor |
|---|---|
| `measure-fps.mjs` (20 s) | **49.92 fps** emulado; 1000 frames; **1.00159** campos/frame |

Nota de la iteración: con el bucle por **sondeo de VPOSR** (`run_frames_polling`) esta demo daba **2.00319 campos/frame** (25 fps) en debug y release; con el bucle canónico **por señal** (`os::init` + `run_frames`, patrón 216) quedó en 1.00159. Las demos 219/220 se migraron también al bucle por señal.

## Evidencia visual

Captura directa de WinUAE (A500_debug): **anillo superior cian** (paleta A, COLOR16-19 de la paleta base) y **anillo inferior magenta** (paleta B conmutada por el `SpritePaletteEvent` en la línea de su `top`). Misma forma y tamaño; solo cambia el color. Inspección visual sin artefactos (sin franjas de color erráticas ni sprites fantasma).

## Conclusión (alcance exacto)

- **VERIFICADO**: conmutación de paleta por franja end-to-end por la fachada (`ActorDesc::sprite_palette` → `compose_sprites` → `SpritePaletteEvent` → `present`), con el segundo anillo en paleta distinta; 49.92 fps/1.00159 campos.
- **VERIFICADO (HOST-391)**: el conflicto simultáneo (dos paletas distintas en la misma franja y registros, pares compartidos) **degrada el de menor `z` a BOB**; sin conflicto no hay trabajo extra.
- **NO CUBIERTO**: arbitraje con la escena en 5+ planos/EHB (los cambios de `COLOR16-31` afectan también al playfield; fuera del alcance de esta campaña); movimiento de la conmutación verificado solo con el pistón de la propia demo (la línea sigue al actor por construcción).

## Reproducir

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/221_sprite_palette_split --debug
node tools/debug/measure-fps.mjs 221_sprite_palette_split A500_debug --json
```
