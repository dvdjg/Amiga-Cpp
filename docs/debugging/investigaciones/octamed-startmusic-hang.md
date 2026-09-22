# OctaMED: cuelgue tras `_startmusic` bajo el engine (A1, abierto)

**Estado**: abierto. **Reencuadrado** con una repro mínima en el árbol (demo
`274_octamed_probe`): `_startmusic` **sí retorna**; el cuelgue ocurre **después** del arranque, antes
del frame 30, y **no** está en la ruta de interrupción del playroutine.

## Repro

```bash
EXTRA_DEFINES="-DENG_AUDIO_OCTAMED" bash tools/build/build-demo.sh demos/amiga/274_octamed_probe --debug
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/amiga/274_octamed_probe --warp --wait-port 300
```

- Marca READY **justo después** de `play_music(.., OctaMED)` → alcanza READY con `detail=0x00027401`:
  `_startmusic` **retorna** y `play_music` va bien. (El síntoma anterior —«no alcanza READY»— ya no se
  da con la repro mínima actual.)
- Con la comprobación retrasada a **frame 30** (leer `DMACONR` y exigir AUD0..3EN): **timeout** → el
  cuelgue está en los frames siguientes al arranque, no dentro de `_startmusic`.

## Qué se ha medido (y descarta)

1. **El bucle `_Wait1line`** (`MED_PlayRoutine.i:2157`) gira hasta que cambia `$dff007` (byte bajo de
   VHPOSR). Una sonda en la demo confirma que `$dff007` **sí cambia** → ese bucle no es el que gira
   sin fin.
2. **La ruta de interrupción del playroutine queda descartada**: con `VBLANK=0` **y** `CIAB=0`
   (`med_feature_control.i`, sin instalar server) **también cuelga**.
3. **Bug latente en `_IntHandler`** (`MED_PlayRoutine.i:806`): hace `MOVEA.L A1,A6` tratando `A1` como
   `is_Data`, pero **exec llama al `is_Code` con `A1` = el `struct Interrupt*`** (su `is_Data` está en
   el offset **14**; el propio playroutine lo rellena con `DB` en `timerinterrupt`). Parchear a
   `MOVEA.L 14(A1),A6` **no** resolvió el cuelgue → **no es la (única) causa**, pero es un fallo real
   a corregir cuando se retome. (Cambio **revertido**: no se deja código vendado sin verificar.)

## Hipótesis (actualizada)

El cuelgue está **después** del arranque, con el playroutine ya en marcha (o tras su relocalización del
módulo), en el **bucle de frames** del engine / el `update`/`render` de la demo. Candidatos:

- la **relocalización del módulo** (`_RelocModule`) deja punteros de muestra que afectan a la DMA;
- el arranque deja **Paula/DMACON** en un estado que hace que el bucle de VBlank del engine
  (`wait_vblank`, sondeo de `VPOSR`) no progrese;
- interacción con `AudioSystem::init(TitleOctaMED)`.

## Siguiente paso

1. **GDB paso a paso**: breakpoint en `_startmusic` y, tras retornar, avanzar hasta capturar el PC
   donde se atasca (herramientas: `tools/debug/step-memory.mjs`, `verify-gdb-step-side-channel`).
   Nota: el canal lateral **no** reporta valor en un *timeout*, así que no basta con marcadores en
   `detail`; hace falta GDB o una captura de pantalla que congele el último paso.
2. **Aislar por pasos en la demo**: marcar READY/FAILED en cada etapa del `init` y del primer frame,
   bisecando el punto exacto del cuelgue.
3. Comparar con el arranque de KONEY (`OCTAMED_example3.s` + `PhotonsMiniWrapper1.04.s`, en
   `../octamed_playroutines_amiga/`) para ver qué condición de entorno espera.

Referencias: `docs/engine/architecture/MUSIC_PLAYER.md` (fila OctaMED),
`docs/guides/roadmap/ROADMAP_AUDIO.md` (A1), demo `demos/amiga/274_octamed_probe/`,
`../octamed_playroutines_amiga/`.
