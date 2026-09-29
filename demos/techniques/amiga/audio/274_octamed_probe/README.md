# 274_octamed_probe — repro de A1 (OctaMED `_startmusic`)

Repro mínima del bloqueo **A1**: arranca el playroutine OctaMED incrustado con
`AudioSystem` en modo `AudioMode::TitleOctaMED` → `play_music(..., MusicFormat::OctaMED)` emite
`jsr _startmusic`. Sirve además de **banco de escucha**: incluye varios módulos de KONEY.

## Módulos disponibles (`MED_MODULE_NUM`)

| `-DMED_MODULE_NUM` | Fichero | Tamaño |
|---|---|---|
| 0 (defecto) | `octamed_test.med` | 18 KB |
| **1** | `mammagamma.med` | **191 KB** |
| 2 | `mammagamma_SPD.med` | 191 KB |
| 3 | `playroutine_test.med` | 45 KB |

## Build + run (dos defines: opt-in del playroutine y módulo)

```bash
export EXTRA_DEFINES='-DENG_AUDIO_OCTAMED -DMED_MODULE_NUM=1 -DOCTAMED_READY_FRAME=0'
bash tools/build/build-demo.sh demos/techniques/amiga/audio/274_octamed_probe --debug
CFG=$(ls -dt out/demos/274_octamed_probe/A500_eng_audio_octamed* | head -1 | xargs basename)
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/techniques/amiga/audio/274_octamed_probe \
    --config "$CFG" --warp --wait-port 300
```

> **Importante**: el runner prioriza `A500_debug` (la build **default**, sin flags) sobre la de
> `EXTRA_DEFINES`; hay que pasar **`--config <id>`** para ejecutar la demo **con** OctaMED. Si no, se
> ejecuta la default (que marca `detail=0x27400` sin ejercitar nada).

`OCTAMED_READY_FRAME=0` marca READY justo tras `_startmusic`; `=N` marca READY en el frame N
exigiendo AUD0..3EN en `DMACONR` (es decir, que el playroutine esté **reproduciendo**).

## Qué comprueba y qué encontró

- **`_startmusic` retorna y SUENA**: con `OCTAMED_READY_FRAME=0` la demo arranca y **se oye la música**
  (`detail=0x27401`, el playroutine ya escribió sus primeros buffers).
- **Cuelga al esperar frames**: con `OCTAMED_READY_FRAME=60` → **timeout**: el playroutine necesita su
  timing de VBlank y el `INTENA` del takeover lo deja apagado (§ Causa raíz en la nota de debugging).
- **Sonda `$dff007`** (VHPOSR bajo): confirma que el contador H **sí cambia** → el bucle `_Wait1line`
  no es el que gira sin fin.

### Escuchar (script)

`tools/audio/listen-octamed.sh <n> [segundos]`: compila, fuerza la config y deja el emulador **vivo**
y **sin `--warp`** (a velocidad real) para oír el módulo. `n`: 0 octamed_test · 1 mammagamma · 2
mammagamma_SPD · 3 playroutine_test.

Detalle, hipótesis descartadas y siguiente paso:
[`docs/debugging/investigaciones/octamed-startmusic-hang.md`](../../../docs/debugging/investigaciones/octamed-startmusic-hang.md).
