# Audio Mixer 3.7 (Photon) — integración nativa

El engine incorpora de forma nativa el **Audio Mixer 3.7** de Photon
(powerprograms.nl), un motor de efectos de sonido (SFX) por software que mezcla
hasta 4 muestras en un único canal hardware de Paula. El código fuente original
en ensamblador vive en `support/audio_mixer/` y se ensambla con VASM a ELF; la
capa de juego lo usa a través de `eng::audio::SfxMixer`.

## Por qué un mixer por software

Paula solo tiene 4 canales DMA. Si cada efecto ocupara un canal, apenas cabrían
3 efectos + 1 canal de música. El mixer convierte **1 canal de Paula** en **4
voces software** (hasta 16 voces si se usan los 4 canales con `MIXER_MULTI`),
mezclando varias muestras simultáneas. Lo hace en tiempo real en la interrupción
de audio, con un coste de ~3,7% de CPU en un 68000 a 7 MHz (4 voces a 11 kHz).

## Dónde vive cada pieza

| Pieza | Ubicación | Rol |
|---|---|---|
| `mixer.asm` / `mixer.i` | `support/audio_mixer/` | Motor ASM (interrupción + mezcla + DMA) |
| `mixer_config.i` | `support/audio_mixer/` | Configuración en tiempo de ensamblado |
| `plugins.asm` / `plugins.i` | `support/audio_mixer/` | Plugins (efectos en tiempo real; opcionales) |
| `eng/audio/sfx_mixer.hpp` | `engine/include/eng/audio/` | Abstracción C++23 de juego (`SfxMixer`) |
| `demos/amiga/058_sfx_mixer` | demo | Ejemplo funcional |

El ensamblado lo hace `tools/build/build-demo.sh` con
`vasmm68k_mot -Felf -m68000 -allmp -DBUILD_MIXER ...`, produciendo un objeto ELF
compatible con el linker de GNU.

## Requisitos (obligatorios)

### 1. Muestras preprocesadas

El mixer **no** comprueba desbordamientos al sumar muestras. Cada byte de una
muestra debe caber en el rango que permite sumar `mixer_sw_channels` voces sin
overflow. Con la configuración actual (`mixer_sw_channels = 4`):

```
rango por muestra = -32 .. +31   (128 / 4 canales)
```

La longitud de la muestra debe ser **múltiplo del tamaño mínimo** (4 bytes por
defecto; ver `MixerGetSampleMinSize()`). Para samples reales, conviértelas (o
pádalas) a ese múltiplo. La regla de oro: cuantizar/limitar la amplitud antes de
usarlas; el propio mixer amplifica por el número de voces, así que las muestras
suenan más bajas que el original.

### 2. Capacidades de la máquina

- Diseñado para programas que **deshabilitan el OS** y acceden al chipset
  directamente (el engine lo hace en `takeover_display`).
- Requiere Chip RAM para el buffer de mezcla (`MixerGetBufferSize()`, ~0,5-8 KB
  según configuración) y cualquier RAM (Chip/Slow/Fast) para las muestras.
- En 68020+ alinea muestras y buffer a 4 bytes y activa `MIXER_68020` para
  rendimiento óptimo (la config actual apunta a 68000).
- Coexiste con un reproductor de música siempre que éste **no toque** el canal
  reservado para el mixer (`mixer_output_channels`).

## Configuración

Todo es en tiempo de ensamblado (`support/audio_mixer/mixer_config.i`). La
configuración por defecto del engine:

```
MIXER_SINGLE = 1          ; un canal hardware de salida
mixer_output_channels = DMAF_AUD0   ; el mixer usa AUD0
mixer_sw_channels = 4     ; 4 voces software
mixer_period = 322        ; ~11 kHz (PAL)
MIXER_C_DEFS = 1          ; alias C (_MixerXxx) para linkar desde C++
```

Para música + SFX simultáneos, el convenio es: el mixer en AUD0 y el
reproductor de música en AUD1..AUD3 (o viceversa), y la pista no usa el canal
del mixer.

> **Buffers de plugin obligatorios (aunque los plugins estén OFF)**: `MixerSetup`
> y el handler esperan punteros **no nulos** para el buffer de plugins (A1) y el
> de datos (A2), incluso con `MIXER_ENABLE_PLUGINS=0`. Pasar `nullptr` deja la
> mezcla en **silencio** (salida ≈ 0). Además, `MixerGetPluginsBufferSize()` es
> un **no-op** en ese caso (no escribe D0 y devuelve basura), así que hay que
> reservar un bloque fijo (p. ej. 896 B). `SfxMixer::init` ya lo hace; si se
> integra el mixer a mano, replicar el patrón de `demos/amiga/068_mixer_ref`.

## API de juego (`eng::audio::SfxMixer`)

```cpp
eng::audio::SfxMixer sfx;

// 1. Tras tomar el display: reserva el buffer Chip y arranca el mixer (VBR=0).
sfx.init(backend.memory());

// 2. Reproducir (devuelve el canal, -1 si no hay libre).
eng::audio::SfxSample boom = { boom_data, boom_len };
eng::audio::SfxChannel ch = sfx.play(boom, /*priority*/ 3, eng::audio::LoopMode::Once);
sfx.play_on(eng::audio::MixCh0, loop_alarm, 1, eng::audio::LoopMode::Loop); // voz fija

// 3. Control.
sfx.set_master_volume(48);   // 0..64
sfx.stop(ch);
bool busy = sfx.is_playing(ch);

// 4. Al cerrar.
sfx.shutdown();
```

`SfxSample` describe una muestra ya preprocesada; `LoopMode` distingue
`Once`/`Loop`/`LoopOffset`; la prioridad (mayor gana) decide qué efecto cede su
voz ante otro más importante.

## Preprocesar muestras

El proyecto original incluye un conversor (`SampleConverter`, en
`AmigaAudioMixer/Tools/`) que escala y rellena muestras 8-bit con signo. Para el
engine, lo más cómodo es generarlas ya conformes (amplitud ±32 para 4 voces,
múltiplo de 4) o convertir en el host antes de incrustarlas. Ejemplo en
`demos/amiga/058_sfx_mixer/src/main.cpp` (`gen_square`).

## Melodías y polifonía con muestras pre-renderizadas

El mixer reproduce cada muestra a un ritmo fijo (el `mixer_period`), sin control
de tono por voz (el plugin de pitch está desactivado). Para tocar una **melodía**
por una voz hay dos caminos: pre-renderizar la secuencia en la propia muestra, o
activar el plugin de pitch. El patrón usado en el engine es el primero: cada
"instrumento" es una muestra que ya contiene su línea (notas a distintas
frecuencias concatenadas) y que se reproduce en bucle en una voz del mixer.

Serie incremental (una voz por demo, amplitudes escaladas a `120/N` para que la
suma no desborde el rango de Paula):

| Demo | Voces | Contenido |
|---|---|---|
| `067_mixer_melody` | 1 (MixCh0) | melodía del Himno a la Alegría |
| `069_mixer_two_voices` | 2 (MixCh0-1) | melodía + bajo |
| `070_mixer_three_voices` | 3 (MixCh0-2) | + contramelodía |
| `071_mixer_four_voices` | 4 (MixCh0-3) | + línea aguda (máximo single) |

Reglas del patrón:

- **Amplitud**: `±120 / nº_voces`. Con 1 voz ±120; con 4 voces ±30 (la suma de
  las cuatro nunca pasa de ±120). Es más fuerte que el ±32 fijo "para 4 voces"
  cuando suenan menos voces.
- **Misma longitud de bucle** en todas las voces para que se mantengan en fase
  (p. ej. 12800 B: `N` notas × duración fija). El mixer loopea cada voz por
  separado.
- **Síntesis por acumulador de fase**: la tabla `sine_byte` tiene 64 entradas, así
  que una vuelta de tabla = `64 << 16 = 2^22` unidades de fase y
  `inc = (f << 22) / sample_rate` por muestra (con `f << 16` cada nota sonaría
  ~64× más grave, casi DC). Ver `demos/amiga/067_mixer_melody/src/main.cpp`.
- **Fundido** corto (32 muestras) al inicio y al final de cada muestra para que el
  punto de bucle no chasquee.
- **Chip RAM**: cada voz cuesta su longitud en Chip RAM más los buffers internos
  del mixer (672 B de mezcla + buffer/datos de plugin). Con 4 voces de 12800 B y
  4 planos de bitplane se roza el límite de 96 KB de Chip RAM.

## Rendimiento (referencia, 11 kHz / 4 voces, sin optimizaciones)

| Sistema | CPU |
|---|---|
| A500 68000@7MHz (slow RAM) | ~3,7% |
| A1200 68020@14MHz | ~1,9% |

Con `MIXER_SIZEXBUF` + `MIXER_WORDSIZED` se baja a ~3,4% en A500. En HQ mode
(muestras 8-bit sin preprocesar) el coste sube a ~13%.

## Música

El `MusicPlayer` (reproductores de tracker) es una capa separada: ver
`docs/engine/architecture/MUSIC_PLAYER.md`. Ambos conviven reservando canales
de Paula distintos.

## Recursos: samples y herramientas

Muestras 8-bit libres (compatibles con Paula) y utilidades:

- **AKWF FREE** (Adventure Kid Waveforms):
  https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE y
  https://www.adventurekid.se/akrt/waveforms/adventure-kid-waveforms/ — miles de
  formas de onda de un ciclo (ideales para el acumulador de fase).
- **AmigaPal** (echolevel): https://github.com/echolevel/AmigaPal — utilidad para
  preparar imágenes/sonido para Amiga.
- **Amiga Music Preservation (AMP)**: https://amp.dascene.net — módulos de música
  Amiga (ptplayer/P61).
- **The Mod Archive**: https://modarchive.org — repositorio de módulos.
- **Amiga Soundtracker Sample Packs (st-xx)**:
  https://archive.org/details/AmigaSoundtrackerSamplePacksst-xx — packs ST-01..ST-xx.
  Copia local: `C:\Users\dvdjg\Documents\programa\Assets\Sound\Samples\st-xx`.

Los samples son fuentes 8-bit con signo; para el mixer hay que escalar la
amplitud a `±120/N` y dejar la longitud en múltiplo de 4 (o del mínimo que
devuelve `MixerGetSampleMinSize()`).

## Referencias

- Documentación completa del mixer: `AmigaAudioMixer/Documentation/Documentation.md`.
- Diseño del engine: `ENGINE_DESIGN.md` §2.6.
