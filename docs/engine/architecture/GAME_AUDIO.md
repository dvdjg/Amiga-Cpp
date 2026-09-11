# Audio de juego — guía completa

Este documento reúne cómo se maneja el audio en el engine para un videojuego:
la arquitectura por capas, la API orientada a juego (`GameAudio`), cómo funciona
el mixing y la música, y —lo más práctico— cómo generar músicas y sonidos desde
herramientas externas para que sean eficientes en este sistema.

Los detalles del mixer de Photon están en [AUDIO_MIXER.md](AUDIO_MIXER.md) y los
reproductores de música en [MUSIC_PLAYER.md](MUSIC_PLAYER.md); aquí se integra
todo desde el punto de vista del juego.

## 1. Arquitectura en tres capas

```
┌──────────────────────────────────────────────────────────────┐
│  Juego                                                        │
│   audio.play(kSfxDisparo, frame);  audio.play_music(mod, ...);│
├──────────────────────────────────────────────────────────────┤
│  Capa de juego (eng/audio/game_audio.hpp)                     │
│   SampleBank  ·  política de voces  ·  ducking                │
│   (sfx_bank.hpp: puro, host-testable)                         │
├──────────────────────────────────────────────────────────────┤
│  Backend (eng/audio/audio_system.hpp + platform)              │
│   SfxMixer (Audio Mixer 3.7) · PtPlayer / P61Player           │
│   → escribe Paula (AUDx/DMACON) y gestiona interrupciones     │
└──────────────────────────────────────────────────────────────┘
```

- **Capa de juego**: `GameAudio` + `SampleBank` (registras sonidos por `id` y
  los disparas con `play(id)`). No hay punteros crudos ni canales a la vista.
- **Capa de backend**: `SfxMixer` (mezcla software de efectos) y los reproductores
  de música (ptplayer/P61). Viven sobre el hardware Amiga.
- **Reparto de canales**: el mixer de SFX usa `AUD0`; la música usa `AUD1..AUD3`
  (configurable). Así pueden sonar a la vez sin pisarse.

## 2. API de juego (`GameAudio`)

```cpp
eng::audio::GameAudio audio;

// El audio lo posee el backend; el GameAudio (capa de juego) se enlaza a él.
audio.attach(backend.audio());

// 1) Registrar sonidos en el banco (id + metadato de política).
audio.bank().add(kSfxDisparo, { disparo_data, /*prio*/2, /*max_inst*/3, /*cooldown*/4, /*duck*/false });
audio.bank().add(kSfxExplosion, { explosion_data, 3, 2, 10, true }); // ducking
audio.set_group_budget(kGrupoArmas, 4); // las armas comparten 4 voces

// 2) Arrancar.
backend.audio_init();                       // o audio.init(backend.memory())
audio.play_music(modulo, eng::audio::MusicFormat::Protracker);
audio.set_music_volume(40);      // volumen de música en reposo
audio.set_duck_volume(12);       // volumen de música cuando hay ducking
audio.set_music_channel_mask(0x0E); // silencia AUD0 (mixer) y deja AUD1..AUD3 (Protracker)

// 3) Por frame.
audio.play(kSfxDisparo, frame);  // aplica cooldown + límite + grupo + prioridad
audio.update(frame);             // poda voces acabadas + aplica ducking
audio.update_music();            // avanza música (solo P61; Protracker es por CIA)
```

**Política por sonido** (`SfxDef`):

| Campo | Significado |
|---|---|
| `priority` | Mayor gana una voz ocupada (el mixer roba la voz de menor prioridad). |
| `max_instances` | Instancias simultáneas máximas de ese sonido (0 = sin límite). |
| `cooldown_frames` | Frames mínimos entre dos disparos del mismo sonido (evita spam). |
| `group` | Grupo (0 = ninguno); los sonidos del mismo grupo comparten un presupuesto de voces (`set_group_budget`). |
| `duck_music` | Si `true`, baja la música (`duck_volume`) mientras suena. |

`play()` devuelve el canal (>=0) o -1 si se rechazó (cooldown/límite/sin voz libre).

La parte pura (`SampleBank` + `allow_trigger`) está validada por el test host
HOST-008; la orquestación con hardware, por la demo `062_game_audio`.

## 3. Cómo funciona el mixing (resumen)

El `SfxMixer` envuelve el **Audio Mixer 3.7 de Photon**: un *software mixer* que
mezcla hasta 4 muestras en **un solo canal de Paula** (hasta 16 con `MIXER_MULTI`).

- Mezcla en la **interrupción de audio** (nivel 4), no en VBLK: el ritmo lo marca
  el reloj de Paula, así no hay deriva con el vídeo.
- **Muestras preprocesadas**: amplitud reducida a ±128/nº de voces (p. ej. ±32
  para 4 voces) para poder **sumar sin comprobar overflow** → rápido (~3,7 % de
  CPU en A500 a 11 kHz).
- Las muestras **fuente pueden vivir en cualquier RAM** (FastRAM incluida); solo
  el buffer de salida (~0,5-8 KB) va en Chip RAM. El paralelismo es el inherente
  al Amiga: la CPU mezcla mientras el Blitter/Copper hacen su trabajo por DMA.

Es un mixer **de configuración fija** (voces, frecuencia, canal se fijan en
`support/audio_mixer/mixer_config.i`), no un algoritmo adaptativo. La estrategia
"mixing en interrupción + muestras en FastRAM + buffer pequeño en Chip" ya está
resuelta en esa configuración.

## 4. Cómo funciona la música (resumen)

- **Protracker (.mod)**: `PtPlayer` (Frank Wille). Se instala una interrupción
  CIA-B para el tempo; no requiere `update()` por frame. `set_music_channel_mask`
  reserva canales para el mixer.
- **P61 (.p61)**: `P61Player` (Photon/Scoopex). Frame-driven: llamar
  `update_music()` una vez por frame.
- **AHX (.ahx)**: pendiente (necesita el blob del replayer + libc).

## 5. Generar música desde herramientas externas

La música es un **módulo de tracker** incrustado en la demo/juego (por `incbin`).

### Protracker (.mod) — recomendado

1. Compón en un tracker compatible con Protracker (OpenMPT, MilkyTracker o el
   ProTracker original). **4 canales**, 31 muestras máximo.
2. Exporta como **.mod (ProTracker M.K.)**, no como .xm/.it.
3. **Reserva un canal para el SFX**: deja el canal 0 (el del mixer, `AUD0`) sin
   notas, o usa `set_music_channel_mask(0x0E)` para silenciarlo en el reproductor.
   La música efectiva irá por `AUD1..AUD3`.

> **Semántica de la máscara**: bit a 1 = canal audible, bit a 0 = canal silenciado
> (bit 0 = `AUD0` ... bit 3 = `AUD3`). `0x0E` silencia `AUD0` y mantiene `AUD1..AUD3`;
> `0x02` deja sonar solo `AUD1`.
4. Incrusta el `.mod` (el `incbin` de `support/`) y pásalo como
   `MusicModule { Span(module, len) }`.

> **¡Ojo con el formato de nota del ptplayer!** No es el estándar M.K.: el
> período de 12 bits va en `byte0` (bits 11-8, nibble bajo) + `byte1` (bits 7-0),
> y la muestra en `byte2` (nibble alto) + el efecto en `byte2` (nibble bajo).
> Para una nota de período `P` con la muestra 1:
>   `byte0 = P >> 8`, `byte1 = P & 0xFF`, `byte2 = 0x10`, `byte3 = 0`.
> Si se usa la codificación M.K. estándar (período repartido en 3 nibbles) el
> reproductor lee un período erróneo y suena un **tono fijo**. Las demos 060-063
> generan el `.mod` con esta codificación (`write_note`).

> **Límite de tesitura (3 octavas)**: `mt_PeriodTable` cubre solo `C-1..B-3`
> (períodos `856..113`). Una nota más aguda (período menor que 113, p. ej. C-4=107)
> queda fuera de la tabla y el reproductor la clava en B-3 (113) en vez de sonar.
> Todas las voces deben caber en ese rango; para más tesitura, transpón la pieza
> o usa un sample grabado en la octava deseada. Demo de referencia: `066_polyphony`
> (3 voces independientes en C-1..B-3).

### P61 (.p61)

1. Compón en ProTracker y conviértelo con **P61Converter (P61con)** a `.p61`.
2. Incrusta el `.p61` y reprodúcelo con `MusicFormat::P61` (recuerda
   `update_music()` por frame).

### Samples y utilidades (recursos)

Las voces del mixer se pre-renderizan en muestras 8-bit (ver `AUDIO_MIXER.md`
§"Melodías y polifonía…"). Fuentes de samples/tools:

- **AKWF FREE**: https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE ·
  https://www.adventurekid.se/akrt/waveforms/adventure-kid-waveforms/
- **AmigaPal**: https://github.com/echolevel/AmigaPal
- **Amiga Music Preservation (AMP)**: https://amp.dascene.net
- **The Mod Archive**: https://modarchive.org
- **Amiga Soundtracker Sample Packs (st-xx)**:
  https://archive.org/details/AmigaSoundtrackerSamplePacksst-xx · copia local en
  `C:\Users\dvdjg\Documents\programa\Assets\Sound\Samples\st-xx`.

### Notas de mezcla música+SFX

- Arranca la música **primero** y el mixer **después** (los reproductores
  inicializan todos los canales al arrancar).
- El módulo no debe tocar el canal reservado para el mixer.
- Ajusta el volumen de la pista teniendo en cuenta que las muestras del mixer
  suenan más bajas (preprocesadas): sube el volumen de la música si hace falta.

## 6. Generar sonidos desde herramientas externas

Los SFX son **muestras PCM 8 bits con signo**, preprocesadas.

1. **Edita/genera el sonido** en un editor de audio (Audacity, un sintetizador,
   un generador de efectos). La frecuencia objetivo es **11 kHz** (el mixer usa
   `mixer_period=322`); si el editor te da otra frecuencia, **resamplea a 11 kHz**.
2. **Exporta a 8 bits con signo, PCM crudo (`.raw`)**. En Audacity: *Exportar →
   Other uncompressed files → Header: RAW, Encoding: Signed 8-bit PCM*. Un archivo
   `.wav` 8-bit también sirve si luego lo conviertes.
3. **Preprocesa** con la herramienta del proyecto (escala la amplitud y rellena):

   ```bash
   node dist/tools/audio/sample-converter.js 4 disparo.raw disparo.raw
   ```

   El `4` es el número de voces del mixer (`mixer_sw_channels`). Esto:
   - escala cada byte a **±32** (rango ±128/4) para que la suma no desborde;
   - rellena la longitud a **múltiplo de 4 bytes** (requisito del mixer).
4. Incrusta el `.raw` resultante (por `incbin` o un array) y regístralo:

   ```cpp
   audio.bank().add(kSfxDisparo, { Span(disparo_raw, disparo_len), 2, 3, 4, false });
   ```

### Requisitos exactos de las muestras (mixer estándar, 4 voces)

| Requisito | Valor |
|---|---|
| Formato | PCM 8 bits con signo |
| Frecuencia | ~11 kHz (coincidir con `mixer_period`) |
| Amplitud | **-32..+31** (para 4 voces; ±64 para 2, ±43 para 3) |
| Longitud | **múltiplo de 4 bytes** (el conversor la rellena) |

> Si no respetas la amplitud, al sumar varias voces se oyen **distorsiones
> graves** (overflow). Si no respetas la longitud, se oyen **clics/pops** al
> final. El modo *High Quality* (`MIXER_HQ_MODE=1`) evita el escalado de amplitud
> a cambio de mucha más CPU (~13 %), y sigue exigiendo el múltiplo de 4.

### Buenas prácticas

- **Normaliza antes de preprocesar**: el conversor divide; si la fuente es muy
  baja, el resultado sonará débil. Maximiza el nivel sin *clipping* primero.
- Los **bucles** deben ser múltiplo del bloque del mixer (4 bytes); para bucles
  perfectos, corta el loop al múltiplo exacto.
- Los **sonidos muy cortos** (< 1/50 s) en bucle cuestan más CPU; alárgalos o
  actívalos como `Once`.

## 7. Resumen de posibilidades y límites

| Necesidad | Cómo | Estado |
|---|---|---|
| Disparar SFX con política (cooldown/límite/prioridad) | `GameAudio::play(id, frame)` | Listo (demo 062) |
| Música + SFX simultáneos | Canales separados (AUD0 mixer, AUD1-3 música) | Listo |
| Ducking (música baja con un efecto) | `SfxDef::duck_music` | Listo |
| Volumen global / por SFX / por música | `set_master_volume`/`set_sfx_volume`/`set_music_volume` | Listo |
| Muestra en FastRAM (fuente) | Automático (solo el buffer va a Chip) | Listo |
| Mixing adaptativo en runtime | No (config fija en `mixer_config.i`) | No aplica |
| AHX | — | Pendiente |

## 8. Referencias

- [AUDIO_MIXER.md](AUDIO_MIXER.md) — requisitos y configuración del mixer de Photon.
- [MUSIC_PLAYER.md](MUSIC_PLAYER.md) — reproductores de música y plan.
- `support/audio_mixer/mixer_config.i` — configuración del mixer.
- `tools/audio/sample-converter.ts` — preprocesado de muestras (escala + relleno).
- Demos: `058_sfx_mixer` (SFX), `060_music_pt` (música), `061_audio_system`
  (coexistencia), `062_game_audio` (capa de juego).
