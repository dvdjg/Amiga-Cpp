# Niveles de abstracción

Las **formas de usar el engine** y su profundidad. Elige el nivel más alto que resuelva tu intención;
baja solo con motivo.

```
nivel A (fachada)      App · Screen · SpriteScene · ScrollLayer · IndexedDisplay · World
nivel B (dispositivo)  Device · Scene · FramePlan · copper::Scheduler · RasterLayout
nivel C (metal)        hw · cpu · parallel · backend · registros · DMA
```

| Página | Contenido |
|---|---|
| 01_nivel_a_fachada.md | El nivel normal: la fachada `eng/api/*`. Un juego se escribe aquí. |
| 02_nivel_b_dispositivo.md | Escapes agrupados: `Device`, `Scene`, `FramePlan`, Copper. |
| 03_nivel_c_metal.md | Hardware directo: `eng::hw`, `eng::cpu`, backend, registros. |
| 04_escape_cuando_bajar.md | Árbol de decisión (ASCII): cuándo bajar de nivel y cómo volver. |

Volver al [índice del manual](../README.md).
