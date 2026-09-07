# `playground/` — experimentos de algoritmos y calidad

Zona para probar algoritmos, medir calidades de ejecución y validar ideas antes
de formalizarlas como tool del pipeline o como demo del engine. Los **resultados
y medidas se escriben en `out/playground/<experimento>/`**, nunca en el propio
`playground/`.

La especificación completa está en `docs/STRUCTURE.md` (§8).

## Estructura

```
playground/
├── README.md                  → este fichero (criterio unificado de medición)
└── <experimento>/             → un directorio por experimento
    ├── código / script del experimento
    └── (resultados → out/playground/<experimento>/)
```

## Criterio unificado de medición (obligatorio en cada experimento)

Para que dos ejecuciones (dos personas, una IA, dos fechas) sean comparables:

1. **Inputs deterministas**: fija las imágenes/entradas en `assets/<plataforma>/…`
   o dentro del propio `playground/<experimento>/`, con nombres estables.
2. **Config reproducible**: registra en cada salida (`run.json` o cabecera del
   informe) la tool, versión, flags, fecha/hora y hash de los inputs.
3. **Mismo espacio de comparación**: compara cuantizaciones/tiles en la misma
   geometría y paleta, con el mismo assert (ver
   `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md`).
4. **Evidencia**: un resultado va acompañado de cómo reproducirlo; sin eso no es
   una medida válida.
5. **Ejecución por IA**: invocada desde una IA, la salida usa el mismo `--out`
   por defecto y la misma estructura que una invocación manual.

## Ciclo de vida

- Madura → promueve el algoritmo a `engine/` y la automatización a `tools/`.
- Se abandona → borrar el contenido de `out/playground/<experimento>/` y, si no
  aporta nada, el propio `playground/<experimento>/`.