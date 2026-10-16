# 085: el bloque de copperlist solapa el 4.º plano del bitmap 1 (disco negro un frame de cada dos)

## Síntoma

En la demo `085_copper_plan_scene`, **la mitad de los frames** muestran el disco (BOB) en **negro**
en lugar de con su arcoíris interior y su borde blanco; los otros frames se ven correctos. Con
capturas tomadas cada 400 ms (≈20 frames) todas caen en la misma paridad y el defecto parece
permanente según el momento de la captura.

## Causa (medida en WinUAE-DBG, canal GDB)

La copperlist del `copper::DoubleBuffer` del `Plan` cae **dentro del 4.º plano del bitmap 1**:

- Bitmaps del demo (4 planos × `kPlaneBytes` = 10240 B): `[0x17860, 0x21860)` y `[0x21860, 0x2B860)`.
- Al mostrar el bitmap 1, `BPL1PT = 0x21860`, `BPL4PT = 0x21860 + 3·10240 = 0x29060`.
- `COP1LC ∈ {0x29060, 0x2A060}` (los dos bloques del doble buffer) → el **plano 4 del bitmap 1
  contiene la copperlist viva**.

`BPLCON0 = 0x4200` (BPU=4): el display lee los 4 planos; el cuarto es la copperlist, así que
cada píxel del disco (índice 1) se convierte en 1|8 = 9 → `COLOR09 = 0x000` (**negro**), y el
borde (5|8 = 13 → `COLOR13 = 0`) también, salvo en las líneas donde el bit de la copperlist es 0
(quedan "puntos" de arcoíris). El cielo (índice 0) también pasa a 8 → `COLOR08 = 0`, pero se
disimula sobre las bandas oscuras.

Evidencia de registro (sesión con el binario de `HEAD`, sin cambios locales):

```
$DFF0E0 BPL1PTH = $0001 BPL1PTL = $7860   ; bitmap0 (0x17860)
$DFF0E4 BPL2    = 0x1A060  BPL3 = 0x1C860  BPL4 = 0x1F060   ; bitmap0 tiene 4 planos reales
$DFF080 COP1LC  = $0002A060               ; bloque de copper del frame actual
dump 0x29060: 00 96 83 80 01 00 42 00 01 02 00 00 …  ; ¡es una copperlist!
```

Y al mostrar el bitmap 1 (`BPL1PT = 0x21860`, `BPL4PT = 0x29060`): la captura interna del
emulador muestra el disco negro; con el bitmap 0 (`BPL4PT = 0x1F060`, plano limpio) el disco
sale con colores. Conteo objetivo de píxeles negros interiores en capturas alternas del MISMO
binario: `6076` / `0` / `6076` / `3124` → paridad confirmada.

**Es preexistente al refactor tipado de copper de 2026-10** (reproducido con el binario de
`HEAD` en un `git worktree` limpio, sin los cambios locales): no lo causa el cambio de slots.

## Implicación (deuda a resolver)

- Es el caso que `eng::setup::RegionTable` está pensado para cazar (solape parcial entre
  regiones de la arena): el demo 085 no registra sus bloques. Dos vías posibles:
  1. **Demo**: no mostrar 4 planos (BPU=3 + `kPlanes=3`, ya que el disco usa 3) o registrar las
     regiones y validar; el plano 4 no aporta nada.
  2. **Allocator/backend**: investigar por qué el segundo `reserve` del demo termina dentro del
     bloque de planos (¿el `DoubleBuffer` reservado en otra sub-arena con direcciones
     solapadas?); el reporte `memory_report` por banco de `configure()` debería cazarlo.
- El `PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` recomienda validar **secuencias** (no una captura): este
  caso es un ejemplo directo de por qué (mitad de frames limpios, mitad rotos).
