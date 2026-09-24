# 208 — self-test de copia lineal por Blitter (`blitter_memcpy`)

Valida en hardware `AmigaBackend::blitter_memcpy` (copia **lineal** de RAM, `D=A`, módulos 0):

1. **Síncrona**: copia un buffer conocido (2 KB) y compara byte a byte.
2. **Asíncrona**: `blitter_memcpy_async` + **IRQ BLIT** que publica `Msg{BlitDone}` en un
   `eng::os::MsgPort`; el bucle consume el mensaje y compara.

Resultado en el overlay: `sync: OK` y `async (IRQ BLIT -> MsgPort): OK`.

```
   bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/208_blitter_memcpy --debug
   bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/208_blitter_memcpy --warp
```

## Referencias

- `docs/reference/amiga/techniques/blitter-memcpy.md` (contexto y modo asíncrono).
- `engine/include/eng/os/port.hpp` (puerto de mensajes del mini-SO).
- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (técnicas Blitter↔Copper).
