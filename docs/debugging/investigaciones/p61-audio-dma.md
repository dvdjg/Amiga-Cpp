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

- `AssetCacheBackend` guarda `eng::Ref<MemorySystem>` (referencia, no copia): sin cursor congelado.
- El fix del asignador (`configure_backing`: los bancos delegan en las arenas) comparte buffer **y cursor** → sin solape (cubierto por HOST-364).
- El `Block`s/`MemBank` devuelven bloques del tamaño pedido; la arena *bump* no retrocede.

**Observación aparte (latente, no era la causa)**: con `ENG_APP_MAIN`, `main` coloca `AmigaBackend` + el `Game` + `App` en **pila**; `sizeof(AbyssDemo)` medido = **18954 B** (un `Scene` grande). Si la pila de arranque es pequeña (p. ej. CLI de AmigaOS), podría desbordar. Mitigación propuesta: mover esos objetos a **estáticos/BSS** (requiere un `atexit` no-op en el soporte freestanding) o subir la pila. No se aplicó aquí para no ampliar el cambio.

## Referencias

- Playroutine: `support/music/p61/P6112-Play.i` (`P61_dmason`, `P61_Music`), `support/music/p61.asm`.
- Contrato del DMA: [`MUSIC_PLAYER.md`](../../engine/architecture/MUSIC_PLAYER.md).
