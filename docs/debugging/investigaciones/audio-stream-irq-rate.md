# Ritmo de la IRQ de audio en el streaming (A5) — 272_audio_stream

La demo `demos/amiga/272_audio_stream` reproduce PCM desde RAM con `PcmStream` y cambia de buffer
en la **IRQ de audio (nivel 4)**. La infraestructura funciona (el servicio se instala, la IRQ
dispara, el ISR reprograma la voz), pero **la IRQ dispara ~34× más rápido que la duración del
buffer** (`AUDxPER * AUDxLEN`), lo que produce *underruns* y comportamiento errático (a veces la
demo informa en el frame 20, a veces se cuelga). A5 **no queda verificado**.

## Medición

Con contadores de 8 bits consistentes (`irq == swap + underrun`) y ventana de 20 frames (~0,4 s):

```text
irq=207  swap=51  underrun=156      ->  ~517 IRQ/s   (esperado ~15/s con len=1024, per=221)
```

## Mecanismo (referencia)

- **AHRM 3.ª** (`docs/reference/ahrm/...cat.md:4378`): las location/length se leen **antes** de
  empezar el bloque (a back-up registers) y la interrupción ocurre **justo después de esa lectura**;
  se pueden reescribir los registros para el siguiente segmento. Una IRQ **por bloque**.
- **WinUAE** (`../WinUAE-DBG/audio.cpp`): la IRQ se levanta en el **wrap** del DMA
  (`event_audxdat_func:2523-2527`: al llegar `wlen==1` recarga `wlen=len` y marca `intreq2` → el
  `setirq` ocurre en la transición 2→3, `:1920`). Escribir `AUDxLEN` solo fija `len`
  (`AUDxLEN:2722-2732`) y `AUDxLCH/LCL` fijan `lc` (`:2631-2684`): **no re-arman** la IRQ.

Conclusión: ni la técnica del *swap* ni una re-armadura explícita explican la tormenta.

## Descartado

1. **`ADKCON` (ATPER/ATVOL, bits 1..4)**: era una hipótesis del ritmo disparado (el periodo/volumen
   saldrían de los datos del canal anterior). Se limpian en `PaulaAudio::silence()`
   (`engine/include/eng/platform/audio_paula.hpp`) y **el ritmo no cambió**.
2. **INTREQ de otro canal**: armar solo `AUD3` en `INTENA` (en vez de AUD0..3) no lo arregló.
3. **`period_for_hz`**: `period_for_hz(16000) = 221` (correcto, `eng/audio/audio_mode.hpp:73`).
4. **Releer los registros**: `AUDxLEN`/`AUDxPER` son **write-only** (la lectura devuelve `0xFFFF`);
   no se pueden verificar los valores efectivos en runtime.

## Arreglado de paso (bugs reales)

1. **Estado compartido con la IRQ sin `volatile`**: `ChunkStream` (`eng/os/stream.hpp`) y los
   contadores de la demo. El compilador cacheaba `m_ready`/`m_eof` en registros (la IRQ no es
   visible para su modelo de datos) y el bucle no veía los buffers liberados → cuelgue. Ahora son
   `volatile`.
2. **Contadores de 32 bits**: en el 68000 un `u32` se parte en dos accesos y la lectura se
   **desgarra** (daba `swap > irq`, imposible). Los contadores de la IRQ son ahora `volatile u8`
   (un byte se lee/escribe de forma atómica).

## Hipótesis abierta (para retomar)

El producto efectivo `len * per` es ~34× menor de lo pedido (o hay otra fuente de nivel 4). Falta
observarlo **desde el emulador**: habilitar el log de audio de WinUAE (`DEBUG_AUDIO`/`debugchannel`,
`audio.cpp:1558-1562/2560-2577`) o instrumentar `AUDxLEN` con un `write_log`, y volcar el puntero de
DMA en cada IRQ para ver qué buffer/longitud está usando realmente. Descartado leer los registros
(write-only) para este fin.
