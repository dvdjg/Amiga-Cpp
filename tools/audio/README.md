# tools/audio — muestras para el mixer

Herramientas host (TypeScript; se compilan a `dist/` con `npm run build`) que convierten audio
real o sintético en los `.raw` de **8 bits con signo** que el mixer del engine incrusta por
`incbin` (hunk de Chip RAM).

| herramienta | qué hace |
|---|---|
| `gen-wave.ts` | genera una onda sintética (`sine`/`square`/…) en `.raw` + `.wav` |
| `prep-sample.ts` | WAV PCM (8/16 bit, mono/estéreo) → `.raw` 8-bit con signo: remuestrea, normaliza al pico y divide por `voices` (para sumar sin desbordar) |
| `sample-converter.ts` | convierte un `.raw` entre layouts de voces |
| `raw-to-header.ts` | `.raw` → cabecera C++ con el array de bytes |
| `wave.ts` | utilidades de WAV compartidas |

## Comandos

```bash
npm run build   # compila tools/ a dist/ (tsc -p tsconfig.json)

node dist/tools/audio/gen-wave.js    <sine|square|...> <freq> <rate> <segundos> <salida>
node dist/tools/audio/prep-sample.js <in.wav> <out.raw> [rate=11025] [voices=4] [peak=120]
node dist/tools/audio/raw-to-header.js <in.raw> <out.h> <nombre>
```

La salida de los pipelines del repo va **siempre a `out/`** (gitignored); nunca a `assets/`.

## Assets que consumen las demos

Cada `.raw` debe existir en `out/assets/audio/` **antes** de compilar la demo (si falta,
`tools/build/build-all-demos.sh` la marca `ASSET`):

| `.raw` | demo(s) | parámetros |
|---|---|---|
| `alien.raw` | 072 | tasa ≈44.3 kHz (`kAudPeriod = 80`), 1 voz |
| `alien_11k.raw` | 073 | 11025 Hz (remuestreo 44100→11025), 1 voz |
| `kick_mix.raw` | 074, 076 | 11025 Hz, 4 voces |
| `snare_mix.raw` | 074, 076 | 11025 Hz, 4 voces |
| `hihat_mix.raw` | 074, 076 | 11025 Hz, 4 voces |
| `claps_mix.raw` | 074, 076 | 11025 Hz, 4 voces |

Ejemplo (percusión del mixer):

```bash
node dist/tools/audio/prep-sample.js samples/kick.wav  out/assets/audio/kick_mix.raw  11025 4 120
node dist/tools/audio/prep-sample.js samples/snare.wav out/assets/audio/snare_mix.raw 11025 4 120
node dist/tools/audio/prep-sample.js samples/hihat.wav out/assets/audio/hihat_mix.raw 11025 4 120
node dist/tools/audio/prep-sample.js samples/claps.wav out/assets/audio/claps_mix.raw 11025 4 120
```

Los parámetros por asset están **inferidos de las constantes de cada demo** (`kAudPeriod`/`kSampleRate`);
el repo no guarda el comando exacto con que se generó cada `.raw`.

## Los WAV de entrada NO están en el repositorio

Los ficheros fuente (p. ej. `alien.wav` y las muestras de percusión) **no** están versionados:
la media no entra en git. Consecuencia: en un checkout limpio, las demos **072, 073, 074 y 076**
no compilan hasta aportar WAVs equivalentes y ejecutar `prep-sample` para producir los `.raw`.
Para ondas sintéticas (tonos) no hace falta fuente externa: `gen-wave` las genera.
