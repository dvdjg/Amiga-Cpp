# Arquitectura de capas

El engine separa **qué quiere el juego** de **cómo lo hace el hardware**. Cinco capas, cada una con un
contrato claro; ninguna de arriba necesita saber cómo funciona la de abajo.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. Lógica del juego  (tu código)                                            │
│    init / update / render(App&)   ·   describe intención                    │
└───────────────────────────────▲─────────────────────────────────────────────┘
                                │ llora a la fachada (nivel A)
┌───────────────────────────────┴─────────────────────────────────────────────┐
│ 2. Fachada   eng::api  (api.hpp)                                            │
│    App · Screen · SpriteScene · ScrollLayer · IndexedDisplay · World        │
└───────────────────────────────▲─────────────────────────────────────────────┘
                                │ llamada de dominio (o escape Device, nivel B)
┌───────────────────────────────┴─────────────────────────────────────────────┐
│ 3. Motor   eng/{field,scene,graphics,audio,res,os,ui,ai,…}                  │
│    playfields · mundo · composición de Copper · mixer · caché · mini-SO     │
└───────────────────────────────▲─────────────────────────────────────────────┘
                                │ pide memoria/tiempo/DMA (tipado por banco)
┌───────────────────────────────┴─────────────────────────────────────────────┐
│ 4. Backend   eng::platform::amiga::AmigaBackend   (+ perfiles de memoria)    │
│    memoria (Chip/Slow/Fast) · Blitter · Copper · VBlank · audio · ficheros  │
└───────────────────────────────▲─────────────────────────────────────────────┘
                                │ escribe registros / usa ROM kernel
┌───────────────────────────────┴─────────────────────────────────────────────┐
│ 5. Hardware   Agnus / Denise / Paula / CIA   (o el emulador)                 │
│    planos, DMA, Copper, sprites, canales de audio, IRQ                      │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Reglas de la frontera

1. **La capa de abajo no conoce a la de arriba**: el backend no sabe de tu juego; el motor no sabe de
   registros; la fachada no sabe de planos.
2. **El backend se instancia una vez, en `main()`**, y se pasa al `App`. El código de juego (capa 1)
   **no nombra** tipos del backend (`AmigaBackend`, `C2p4State`, `OrBobEntry`…): usa el tipo de dominio
   (`eng::graphics::C2p4`, `eng::graphics::OrBob`, `Device`, `FramePlan`).
3. **La memoria llega tipada**: el motor reserva en un banco con un `Tag` de dominio; nunca ve un
   `void*` ni elige banco a mano.
4. **Los tags deciden el método**: si la fuente y el destino del C2P están en **Chip**, se usa el
   Blitter; si **alguno** no lo está (Fast/Slow), el Blitter no lo ve → CPU. El cambio de banco cambia
   la implementación **en compilación**, sin que el llamador elija (`eng/graphics/c2p.hpp`).

## El eje vertical de un frame

```
 VBlank ──► update()  ──► render()
   │           │             │
   │           │             ├─ app.screen()...   (dibujo al plan del frame)
   │           │             ├─ app.emit_bobs_banded(...)
   │           │             └─ app.present()      (ejecuta blits + publica Copper)
   │           │
   │           └─ avanza cámara/actores; el App conduce las capas registradas
   │
   └─ IRQ: latido del mini-SO (entrada, timers, cola de mensajes)
```

Volver al [índice de introducción](README.md) · [índice del manual](../README.md).
