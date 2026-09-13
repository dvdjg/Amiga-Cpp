# 06 · Descripciones con ollama local (qwen3-vl)

Las imágenes reales se describen con un modelo de visión local (`qwen3-vl`).
Aquí se guardan las descripciones capturadas (2026-09-04) como `*_desc.txt`;
para regenerarlas usa el flag `--describe` de la tool:
```
node tools/amiga-tiles/amiga-tiles.mjs image.png --colors 64 --describe --model qwen3-vl:8b-instruct-q8_0
```
La descripción se guarda en `<out>/image_description.txt`.
