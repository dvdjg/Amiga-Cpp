# HOST-270: eventos de audio (MusicEnd / AudioUnderrun) una vez por evento (A2)

Test host de `eng/audio/audio_events.hpp` (puro, sin hardware): `AudioMsgEdges` convierte el estado
continuo de los backends en mensajes del mini-SO (`MsgType::MusicEnd`, `MsgType::AudioUnderrun`)
**solo en el flanco de subida**.

## Qué comprueba

1. **Una vez por evento, no por buffer**: un `MusicEnd` (o `AudioUnderrun`) sostenido se reporta
   **una sola vez**; no se repite mientras el estado no cambie.
2. **Re-arme**: al cesar el evento, el detector se re-arma; el siguiente flanco vuelve a reportar.
3. **Ambos eventos** en el mismo tick y `reset()` para re-armar manualmente (cambio de modo/música).

El envío al puerto (`AudioSystem::tick_frame(port)`) y la detección de fin del P61 (`P61Player::ended()`)
son del lado Amiga; aquí se fija el **contrato puro**. El test se planificó como `HOST-241`.

## Salida de referencia

```
OK: eventos de audio (MusicEnd/AudioUnderrun, uno por evento) validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/audio/270_audio_events
```
