# Audio streaming digital desde disquete (`eng::audio`)

Cómo reproducir una **grabación digital** (música o voz) desde disquete en un A500, con
**pseudo-streaming**: el disco llena buffers pequeños, la CPU los descomprime y Paula los
reproduce por DMA. Encaja con la E/S asíncrona del mini-SO
([MINI_OS_IO.md](MINI_OS_IO.md)) y con la capa de audio ([GAME_AUDIO.md](GAME_AUDIO.md)).

## 1. Restricción y objetivo

Paula solo lee audio desde **Chip RAM** por DMA, y el disquete es lento (~30 KB/s útiles). El
objetivo es un flujo continuo con **footprint = N buffers pequeños**, no el archivo entero: mientras
Paula reproduce un buffer, el disco llena el siguiente y la CPU descomprime el anterior. Como la
CPU queda libre, se prioriza el **ratio de compresión** sobre la velocidad de descompresión.

## 2. Compresión y formato

| Prioridad | Método | Ratio (audio 8-bit) | Descompresión 68000 | Notas |
|---|---|---|---|---|
| 1.º | **ZX0** sobre PCM/delta-PCM | muy bueno (~40–55 %) | buena | mejor equilibrio |
| 2.º | **aPLib** sobre PCM/delta-PCM | excelente | buena | descompresor minúsculo |
| 3.º | **Delta + RLE (ByteRun1)** | medio | muy rápida | si la CPU escasea |
| alt. | ADPCM 4-bit + ZX0 | muy alto | media | con pérdida ligera |

Recomendación: **delta-PCM + ZX0** (o aPLib). El *delta encoding* (diferencias entre muestras
consecutivas) mejora mucho el ratio antes de comprimir. El engine incorpora el **Delta + RLE** como
codec propio (`eng/audio/pcm_codec.hpp`, §7); ZX0/aPLib se integran como descompresores externos
(ver roadmap).

### Formato de archivo

```text
Header (32–48 bytes)
├── Magic ("AUZX")
├── sample_rate   (11025 / 14000 / 16000 / 22050)
├── channels      (1 = mono)
├── bits          (8)
├── total_samples (sin comprimir)
├── compression   (0 = ZX0, 1 = aPLib, 2 = delta+RLE)
├── num_chunks
├── chunk_samples (potencia de 2: 4 KB / 8 KB)
└── (opcional) checksum
Chunk 0 (comprimido) · Chunk 1 · …
```

Preparación del audio: **mono 8-bit con signo**, delta antes de comprimir, chunks de **4–8 KB**
descomprimidos (cómodos en Chip y con buen margen de streaming).

## 3. Arquitectura de pseudo-streaming

```text
  Floppy (pista/sector)
        │  trackdisk DMA / CPU
        ▼
  Buffer comprimido (Chip o Fast)
        │  descompresión (68000)
        ▼
  Buffer PCM A  ←→  Buffer PCM B   (Chip RAM)
        │                │
     Paula DMA        Paula DMA
```

Mientras Paula reproduce el buffer A, la CPU descomprime el siguiente chunk en B; cuando Paula
termina A, la **IRQ de audio** (nivel 4) cambia el puntero a B y libera A para el chunk siguiente.
Con **triple buffer** hay margen si un chunk tarda más (seek o descompresión irregular).

## 4. Sincronización

La IRQ de Paula **solo** cambia el puntero, recarga la longitud y marca un flag; **no** descomprime
(sería demasiado tiempo dentro de la IRQ). La descompresión pesada se hace en el bucle principal o
como **tarea cooperativa** de `eng::task::BackgroundQueue`, drenada en los huecos de VBlank.

```text
  IRQ audio (nivel 4) ──► cambia buffer + flag "falta chunk"
                              │
  bucle/tarea de fondo ───────┘ ──► lee chunk + descomprime en el buffer libre
```

## 5. Esqueleto

```cpp
constexpr eng::u16 kChunkSamples = 4096;   // 4 KB de audio 8-bit
constexpr eng::u8  kNumBuffers   = 2;      // 2 o 3 (triple buffer = más margen)

struct AudioHeader {                       // cabecera del archivo (§2)
	char magic[4]; eng::u16 sample_rate; eng::u32 total_samples;
	eng::u16 chunk_samples, num_chunks; eng::u8 compression;
};

struct PcmStream {
	AudioHeader header {};
	eng::u8* pcm[kNumBuffers] {};           ///< buffers en Chip RAM (Paula los lee por DMA)
	eng::u8  compressed[8192] {};           ///< chunk comprimido (puede ir en Fast)
	volatile eng::u8  play_buf = 0;         ///< buffer que suena
	eng::u16 next_chunk = 0;
	volatile eng::u8  need_chunk = 0;       ///< lo pone la IRQ de audio
	bool eof = false, underrun = false;
};

/// IRQ de audio (nivel 4): cambia de buffer y pide el siguiente chunk. No descomprime.
void paula_audio_isr(PcmStream& s) {
	s.play_buf ^= 1u;
	paula::set_buffer(s.pcm[s.play_buf], kChunkSamples / 2u); // AUDxLCH/LCL + AUDxLEN (en words)
	s.need_chunk = 1u;
}

/// Tarea de fondo / bucle: llena el buffer libre. Vuelve al momento si no hay trabajo.
bool pcm_stream_service(PcmStream& s) {
	if (s.need_chunk == 0u) { return true; }
	s.need_chunk = 0u;
	const eng::u8 free_buf = static_cast<eng::u8>(s.play_buf ^ 1u);
	if (s.next_chunk >= s.header.num_chunks) { s.eof = true; return false; }

	// 1) Leer el chunk comprimido (E/S asíncrona del mini-SO; ver MINI_OS_IO.md).
	const eng::u32 n = read_chunk(s.next_chunk, eng::Span<eng::u8>{s.compressed});
	// 2) Descomprimir directo al buffer PCM de Chip.
	const eng::s32 got = pcm_codec::decode(
		eng::Span<const eng::u8>{s.compressed, n},
		eng::Span<eng::u8>{s.pcm[free_buf], kChunkSamples}, s.header.compression);
	if (got < 0) { s.underrun = true; return false; }
	++s.next_chunk;
	return true;
}
```

Al arrancar se descomprimen los `kNumBuffers` primeros chunks, se programa Paula
(`AUDxLCH/LCL`, `AUDxLEN = samples/2` porque cuenta en *words*, `AUDxPER = period_for_hz(rate)`,
`AUDxVOL`) y se activa el DMA de audio + su IRQ.

## 6. Integración con el mini-SO

- La **lectura del chunk** usa la E/S asíncrona (`file_read_async`); el `FileDone` **no** trae un
  mensaje por buffer: la tarea de fondo comprueba `need_chunk` y lee.
- El `underrun` (buffer de reproducción no listo) puede postear `MsgType::AudioUnderrun`
  (opcional, `post_underrun_msg`) para que el juego baje calidad o repita el último buffer.
- El streaming **no** comparte el tick de VBlank: Paula va por su IRQ; el servicio de descompresión
  se reparte como tarea de fondo.

## 7. Decodificadores

El engine incorpora un codec **Delta + RLE (ByteRun1)** propio y freestanding
(`eng/audio/pcm_codec.hpp`, validado por test host): decodifica directo a Chip, sin heap, apto para
el 68000. Los formatos de mayor ratio (**ZX0**, **aPLib**) se integran como descompresores externos
(port de `unzx0_68000`/`aPLib`), con el mismo contrato `decode(comprimido, destino, tipo)`. Ver el
roadmap en [`ROADMAP_AUDIO.md`](../../guides/roadmap/ROADMAP_AUDIO.md).

## 8. Detalles de implementación

- **Periodo de Paula**: `AUDxPER = period_for_hz(sample_rate)` (PAL 3546895; ver `GAME_AUDIO.md` §7).
- **Chip RAM obligatorio** para los buffers PCM; el comprimido puede ir en Fast.
- **Motor on** (`TD_MOTOR`) entre chunks de un stream, para no pagar el *spin-up* cada vez.
- **Datos contiguos en disco** y en orden de reproducción, para minimizar *seeks*.
- **Triple buffer** si la descompresión o la lectura son irregulares.

## 9. Referencias

- [`MINI_OS_IO.md`](MINI_OS_IO.md) — E/S asíncrona y streaming (doble/triple buffer) del mini-SO.
- [`GAME_AUDIO.md`](GAME_AUDIO.md) — capa de audio, Paula y modos.
- [`STREAMING_LOADER.md`](STREAMING_LOADER.md) — loader de chunks y trackloader de hardware.
- Paula: `../amiga-bootcamp/01_hardware/ocs_a500/paula_audio.md` (canales DMA, `AUDxLEN` en words).
- Compresión: ZX0 (Einar Saukas) y aPLib (Jørgen Ibsen).
