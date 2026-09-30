# P61 en la demo 213: el DMA de audio no se encendía (resuelto)

**Resuelto (2026-09).** La música P61 de `213_bartman_abyss` no sonaba. Eran **dos** causas encadenadas; la clave fue un **clobber de asm inline**.

## Síntomas

- Sonido ausente; `DMACON` (`$dff002`) con **`audio_bits = 0`**.
- Contradicción: `AudioSystem::play_music()` devolvía **true**, pero `P61Player::is_playing()` era **false** en el mismo objeto y al instante (comprobado con lecturas `noinline`).

## Causa 1 — el DMA lo difiere el playroutine a una IRQ que el `App` no atiende

Con `p61.asm` (`p61system=0`, modo VBlank), `P61_Music` **apaga** el canal al iniciar nota y **delega el encendido** a `P61_dmason`, que corre desde la **IRQ de CIA-B (nivel 6)** que el playroutine instala. El `App` no la atiende. **Fix**: el engine aplica el mismo encendido en el frame task — `P61Player::update` llama a `p61_amiga::apply_pending_dma()` (escribe `_P61_dma`, exportado por `p61.asm`, en `DMACON`). Ver [`MUSIC_PLAYER.md`](../../engine/architecture/MUSIC_PLAYER.md) §«Encendido del DMA de audio».

Verificado a nivel de registro: forzar `DMACON=$820F` ya producía sample (`audio_bits=0x0F`, `AUD0LEN=18496`).

## Causa 2 (raíz) — clobber de registro no declarado en el asm inline

Las envolturas `p61_amiga::init/music/end/set_position` (`music_player.hpp`) hacen `jsr _P61_*` con asm inline. Los `_P61_*` de `p61.asm` salvan/restauran `d2-d7/a2-a6`, pero **clobberan `d0/d1`** (y `a0/a1`). El asm inline **no declaraba `d1`**, así que el compilador mantenía un valor vivo en `d1` a través del `jsr` → corrompía el estado C++ circundante: `P61Player::play()` devolvía `m_playing` (true) pero el **almacén de `m_playing`** quedaba mal → `is_playing()` false → `P61_Music` nunca se llamaba → la música no avanzaba ni el DMA marcaba canales.

**Fix**: declarar los clobbers reales — `"d1"` en `init`/`set_position` y `"d0","d1"` en `music`/`end`.

Medido tras el fix: `is_playing=1`, `DMACON audio_bits != 0` (sin necesidad de otros cambios).

## Auditoría de gestión de memoria (a raíz de la duda)

Se revisaron las rutinas de memoria recientes y **no** eran la causa:

- `AssetCacheBackend` guarda una referencia al gestor de memoria (la implementación vigente usa
  `eng::Ref<MemoryManager>`, no una copia): no congela ningún cursor.
- El fix del asignador (`configure_backing`: los bancos delegan en las arenas) comparte buffer **y cursor** → sin solape (cubierto por HOST-364).
- El `Block`s/`MemBank` devuelven bloques del tamaño pedido; la arena *bump* no retrocede.

**Observación aparte (latente, no era la causa)**: con `ENG_APP_MAIN`, `main` coloca `AmigaBackend` + el `Game` + `App` en **pila**; `sizeof(AbyssDemo)` medido = **18954 B** (un `Scene` grande). Si la pila de arranque es pequeña (p. ej. CLI de AmigaOS), podría desbordar. Mitigación propuesta: mover esos objetos a **estáticos/BSS** (requiere un `atexit` no-op en el soporte freestanding) o subir la pila. No se aplicó aquí para no ampliar el cambio.

## ¿Regresión o bug latente?

Es un **bug latente**, no un fix perdido: `git log -S '"d1"' -- music_player.hpp` muestra que `music_player.hpp` **nunca** declaró el clobber (ni se añadió ni se borró). El wrapper de `p61.asm` no cambió (siempre salvó `d2-d7/a2-a6`), y las estructuras del `.i` viejo (`support/music/P6112-Play.i`) y del nuevo (`support/music/p61/P6112-Play.i`) son idénticas en los prólogos. La manifestación depende del **contexto de llamada** (qué valor vive en `d1` al entrar): por eso puede haber funcionado con el mini-SO y romperse con el `App`.

## Mismo patrón en otros wrappers (misma clase de bug)

Los envoltorios de asm inline que hacen `jsr` a rutinas demoscene **sin declarar los clobbers** tienen el mismo riesgo. Las rutinas `_MixerXxx` **no siguen el ABI C**: por sus `movem`, `MixerStart` salva `d0/d1/d7/a0/a6` (→ clobbea `d2-d6,a1-a5`) y `MixerPlayFX` salva `d2/d1/d4-d7/a1/a2/a6` (→ clobbea `d3,a3-a5`).

- **`engine/include/eng/audio/sfx_mixer.hpp` — RESUELTO**: los 15 `jsr _Mixer*` declaran ya los clobbers reales (macros `MX_CB_ALL`/`MX_CB_NO_D0`/`MX_CB_NO_A0D0`/`MX_CB_SETUP`: todos los registros de dato/dirección salvo los fijados como entrada/salida). La 213 (que incluye el header) compila; `059/058/067/069/073` **no** sirven para validar porque tienen una rotura previa (ver abajo).
- **`engine/include/eng/audio/music_player.hpp` (PT/med) — RESUELTO**: los argumentos ya estaban fijados (variables `register ... __asm("a0"/"a1"/"d0")`); faltaban los clobbers. Como `_Pt*` sí salvan `d2-d7/a2-a6`, solo clobberan `d0/d1/a0/a1` → set **preciso** (`PT_CB_*`: `d0/d1/a0/a1` menos los fijados como entrada/salida); med (`_startmusic`/`_endmusic`) igual. **Ojo**: sobre-declarar con los 15 registros dispara un **ICE de gcc m68k** (`print_operand_address`, `config/m68k/m68k.cc:5281`) → hay que ceñirse a lo que la rutina clobbea de verdad (el mixer, que no sigue el ABI, sí necesita más registros; compila).

### Rotura previa de las demos de audio (no relacionada con los clobbers)

`058_sfx_mixer`, `059_music_player`, `067_mixer_melody`, `069_mixer_two_voices`, `073_sample_mixer`, … **no compilan**: pasan `block.view` (`Bytes<Tag>`) donde el engine pide `mem_view()` (`MemView<Tag, Chip>`), y el bloque ni está etiquetado Chip (`MemView<…, Any>`). Es una **migración de API pendiente** en esas demos (están en `tools/build/skip-demos.txt`, por eso no la cazó la regresión). Bloquea validar los wrappers de audio de extremo a extremo.


## Referencias

- Playroutine: `support/music/p61/P6112-Play.i` (`P61_dmason`, `P61_Music`), `support/music/p61.asm`.
- Contrato del DMA: [`MUSIC_PLAYER.md`](../../engine/architecture/MUSIC_PLAYER.md).
