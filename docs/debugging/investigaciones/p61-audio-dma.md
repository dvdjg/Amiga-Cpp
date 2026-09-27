# P61 en la demo 213: el DMA de audio no se enciende y `m_playing` no cuadra

**Estado: abierto (2026-09).** La música P61 de la demo `213_bartman_abyss` no suena: el DMA de audio de Paula queda a 0. Se aislaron dos capas; la segunda (contradicción de `m_playing`) no se ha resuelto.

## Cómo se midió

La demo expone el estado real por `g_eng_run_status.detail` y se lee en `out/run/213_bartman_abyss/A500_debug/run-report.json`, campo **`sideChannel.value.detail`** (no está en la raíz del JSON). Registros de Paula: `DMACON` se lee en `$dff002` (no en `$dff096`, que es de escritura), `AUDxLEN` en `$dff0a6..`.

## Capa 1 — el DMA de audio no se enciende (mecanismo entendido)

Con `p61.asm` (`p61system=0`, modo VBlank), `P61_Init` escribe `DMACON=$000F` (loop silencioso) y `P61_Music`, al iniciar una nota, **apaga** el canal (`move d0,$dff096`) y **delega el encendido** a `P61_dmason`, que corre desde la **IRQ de CIA-B (nivel 6)** que el playroutine instala (vector `$78` + CIA-B `$bfd600/$bfd700/$bfdf00`). `P61_dmason` hace `move P61_dma,$dff096` (incluye `$8200`).

Medido: `DMACON=0x03C0` (**`audio_bits=0x00`**). Descartado que el display lo borre (`su copperlist` escribe `0x8380` = SET, no CLR).

Intentos:

- `INTENA` **EXTER** (`0xA000`) tras el takeover → **no** cambia.
- `INTENA` EXTER **+ máscara TB de CIA-B** (`$BFDD00=0x82`) → **no** cambia.
- Encendido **inmediato** tras `P61_Music` (`DMACON=$820F`) → **sí** (`audio_bits=0x0F`, `AUD0LEN=18496`, sample real). Es el «mismo efecto» de `P61_dmason` en el frame task; implementado en el engine como `p61_amiga::apply_pending_dma()` (`_P61_dma` exportado por `p61.asm`).

## Capa 2 — `m_playing` no cuadra (abierto)

Bloqueante real: en la 213, `AudioSystem::play_music()` devuelve **true** (⇒ `m_format == P61`, que **solo** se fija si `P61Player::play()` —que además fija `m_playing`— tuvo éxito) pero `P61Player::is_playing()` es **false** en el mismo instante y en frames posteriores. Comprobado con un centinela `(music_ok, playing_init, playing_now) = (1,0,0)`.

Consecuencia: `P61Player::update()` nunca llama a `P61_Music` (ni a `apply_pending_dma`), la música **no avanza** y el `P61_dma` no marca canales.

Contradice el código (`play()` escribe `m_playing`), así que apunta a:

- **corrupción de memoria / pila**: `main` coloca en pila objetos grandes (`AmigaBackend backend`, `AbyssDemo game`, `App app`); el backend contiene el `AudioSystem`. Candidato principal.
- dos instancias de `AudioSystem` (descartado a nivel de tipos: `App::audio()` → `m_backend.audio()` referencia; `p61()` devuelve `m_p61`).
- codegen (descartado: quitar `constexpr` a `is_playing()` no cambió el resultado).

Diagnóstico siguiente: leer por GDB la dirección de `m_playing` y la pila de `main` en un frame; o mover `backend`/`game` fuera de la pila y re-medir.

## Referencias

- Contrato del encendido del DMA: [`MUSIC_PLAYER.md`](../../engine/architecture/MUSIC_PLAYER.md) §«Encendido del DMA de audio».
- Registros de audio en el emulador: [`../../reference/emulators/winuae/audio-irq.md`](../../reference/emulators/winuae/audio-irq.md).
- Playroutine: `support/music/p61/P6112-Play.i` (`P61_dmason`, `P61_Music`), `support/music/p61.asm`.
