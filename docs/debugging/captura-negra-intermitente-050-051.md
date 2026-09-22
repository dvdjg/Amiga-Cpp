# Captura negra intermitente en las demos 050/051 (Blitter + BOBs)

**Estado**: abierto, para retomar más adelante. No es un fallo de la demo (ver abajo).

## Síntoma

Tras el run, `out/run/<demo>/A500_debug/screenshot.png` es **100 % negro** (`(0,0,0)`,
un solo color distinto) para las demos `050_blitter_bobs` y `051_blitter_shifted_bobs`.
El runner dice READY (`state=3`) y el comando `screenshot` del emulador responde
`OK 756x576 <ruta>` (la captura no falla: el display está realmente negro). Pasa también en
los frames de `--sequence-frames`. Otras demos (030, 052, 101, 111, 210) renderizan bien con
el mismo emulador y configuración.

## Qué se descartó (evidencia)

- **No es el fuente de la demo**: copiar el `main.cpp` íntegro de la 051 sobre la 050 → la
  050 sigue negra; y los `.exe` de 050 y 051 (mismo fuente) difieren en **2 bytes**.
- **No es el config de WinUAE**: los `runner.uae` de 050 y 051 son idénticos salvo la ruta del
  mount `dh1` (el directorio de salida). La `startup-sequence` es constante
  (`stack 131072` / `cd dh1:` / `:a.exe`).
- **No son los blits**: quitar `execute_frame_plan` de `init` y `update` (dejar solo el fondo)
  sigue dando negro.
- **No es memoria**: subir el Chip de 72 K a 128 K sigue negro.
- **No es el freeze** de validación (`m_validation_frame_ready`) ni `--warp` ni el `--settle-ms`.
- **Es intermitente/por estado**: la 051 renderizaba bien al principio de la sesión y después
  pasó a salir negra de forma consistente → no es un estado de registro cableado.
- **Estado de la escena** (leído desde la propia demo en el `detail`): `buffer_count=1`,
  `back_index=0` → el buffer dibujado es el que muestra el `takeover` (no es desajuste de
  doble buffer).

## Qué NO sirvió

- Leer los registros custom desde el 68000 (`0xDFF100` BPLCON0, `0xDFF096` DMACON) devolvió
  **`0xFFFF`** (bus abierto): la vía "demo-side" no sirve para diagnosticar el estado del chipset.
- `--keep-running` no deja WinUAE/canal lateral vivos (el proceso termina igual).

## Siguiente paso propuesto

1. Leer los registros **reales desde el emulador** (GDB, no desde la demo) en el instante de la
   captura: `BPLCON0`, `DMACON`, `BPL1PT`–`BPL6PT`, `DIWSTRT/STOP`, `DDFSTRT/STOP`, `BPLCON2`
   y la paleta (`COLOR00..`), comparando una captura buena vs. una negra del **mismo** binario.
2. Revisar en la fuente de WinUAE (`../WinUAE-DBG/`) el camino del comando `screenshot` y su
   interacción con `warp` (posible captura de un framebuffer no renderizado en warp).
3. Reproducir con un lazo `run → histograma` para acotar cuándo el display queda negro
   (por ejemplo, si depende de haber ejecutado otras demos antes).

Referencias: `docs/build/BUILD_AND_RUN.md` (runner/telemetría), `docs/debugging/DEBUG-WINUAE-V2-GUIDE.md`.
