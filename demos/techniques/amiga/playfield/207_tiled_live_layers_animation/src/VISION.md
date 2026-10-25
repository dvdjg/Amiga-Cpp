# VISION - 207_tiled_live_layers_animation

Modelo: `qwen3-vl:8b-instruct-q8_0` (Ollama local), 5 frames `frame_000..004`,
100 ms de separación, tras `pre_scroll` a Y=300.

## Prompt

```text
Las imágenes adjuntas van en orden temporal. Describe la fogata (campfire), el agua
y las flores. Indica si la llama cambia de forma entre imágenes, si el agua/flores
están animadas, y si ves rectángulos, huecos, bandas o artefactos. Responde breve y
concreto.
```

## Respuesta

```text
- Fogata: La llama cambia de forma entre imágenes (más alta en la 2ª, más baja en la 3ª).
- Agua: No está animada; es un plano estático.
- Flores: No están animadas; son elementos estáticos.
- Rectángulos/huecos/bandas/artefactos: No se observan.
```