# HOST-269: modos de audio y reparto de canales (A0)

Test host de `eng/audio/audio_mode.hpp` (puro, sin hardware): el vocabulario de **modos** de audio
y el reparto de los 4 canales de Paula.

## Qué comprueba

1. **Reparto por modo** (`channel_quota`):

   | Modo | Mixer (HW) | Música (HW) |
   |---|---|---|
   | `Silent` | — | — |
   | `Game` | `0x1` (AUD0) | `0xE` (AUD1..AUD3) |
   | `GameSfxOnly` | `0xF` (4 canales) | — |
   | `TitleOctaMED` | — | `0xF` (4 canales, 8 voces SW) |

2. **Garantía de A0**: en **ningún** modo se solapan mixer y música, y las máscaras son de 4 bits.
3. **`paula::period_for_hz`**: `3546895 / hz` (PAL) acotado a **124..65535**; `hz == 0` → máximo.

Las escrituras de registro (`DMACON`, `AUDn*`) las hace el backend Amiga; aquí se fija el
**contrato puro** del reparto. El test se planificó en el roadmap como `HOST-240`.

## Salida de referencia

```
OK: modos de audio (reparto de canales + period_for_hz) validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/269_audio_mode
```
