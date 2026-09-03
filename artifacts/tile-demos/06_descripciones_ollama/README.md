# 06 Ã‚Â· Descripciones con ollama local (qwen3-vl)

Las imÃƒÂ¡genes reales se describen con un modelo de visiÃƒÂ³n local (`qwen3-vl`).
AquÃƒÂ­ se guardan las descripciones capturadas (2026-09-04) como `*_desc.txt`;
para regenerarlas usa el flag `--describe` de la tool:
```
node tools/amiga-tiles/amiga-tiles.mjs image.png --colors 64 --describe --model qwen3-vl:8b-instruct-q8_0
```
La descripciÃƒÂ³n se guarda en `<out>/image_description.txt`.
