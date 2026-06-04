# Sistema de depuracion por aserciones de pixel (sin IA)

Este documento define un sistema de validacion visual determinista para demos Amiga,
con intervencion humana minima. El objetivo es decidir de forma inequivoca si una
secuencia renderizada es correcta o incorrecta usando comparaciones de pixeles,
regiones y desplazamientos esperados por frame.

## Objetivo

- Reducir dependencia de inspeccion manual y de modelos de vision.
- Validar cada frame contra reglas explicitas de render.
- Detectar rapido errores de clipping, scroll, copper reset, punteros de bitplane,
  tiles reciclados visibles y artefactos internos.
- Integrar el resultado en regresion automatica (`pass/fail` con razon concreta).

## Principios

- Determinista: mismo input, mismo veredicto.
- Explicable: cada fallo debe decir que regla se rompio, en que frame y en que ROI.
- Rapido: checks por ROI y por rejilla, no procesamiento global costoso.
- Portable: mismas reglas para pruebas locales, CI futura y sesiones de depuracion.
- No bloqueante: el analisis debe ejecutarse en paralelo a otras tareas cuando sea
  posible (post-proceso sobre frames ya capturados).

## Alcance inicial

- Entrada: secuencia `frame_*.png` capturada por `run-demo`.
- Entrada opcional: `run-report.json` para correlacionar telemetria (`cameraX/Y`,
  `runStatus.detail`, frame index).
- Salida: JSON de evidencia, resumen Markdown y opcionalmente overlays PNG.
- Integracion: llamada desde `demos/<demo>/analyze-sequence.ps1` y desde
  `tools/test-regression.ps1`.

## Arquitectura propuesta

1. Captura de secuencia
- Usar `tools/run/run-demo.ps1` con `-SequenceFrames` y `-SequenceIntervalMs`.
- Mantener captura sincronizada con `runStatus` (ya disponible en `run-report.json`).

2. Especificacion de contrato visual por demo
- Añadir archivo declarativo por demo: `demos/<demo>/pixel-contract.json`.
- El contrato define viewport, ROIs y reglas por frame/segmento.

3. Motor de aserciones de pixel
- Implementar script Python (`Pillow` obligatorio, `opencv-python` opcional).
- Wrapper PowerShell en `tools/analyze` para integrarlo con scripts existentes.

4. Reporte
- `pixel-assert-report.json` con resultados por regla y por frame.
- `pixel-assert-summary.md` para lectura humana rapida.
- Salida `non-zero` si hay fallos.

## Reglas (checks) recomendadas

Estas reglas cubren la mayoria de errores graficos sin IA:

- `equal_region`
  - Compara una ROI entre dos frames con tolerancia RGB.
  - Uso: zonas estaticas que no deben cambiar.

- `shifted_region_match`
  - Compara ROI de frame `N` contra ROI de frame `N+1` aplicando desplazamiento
    esperado `(dx, dy)` y mide error residual.
  - Uso principal: scroll pixel a pixel o por pasos conocidos.

- `template_anchor`
  - Busca un patron pequeno en una ventana limitada y valida posicion esperada.
  - Uso: comprobar que un tile/marker no salta o no aparece antes de tiempo.

- `forbidden_color_ratio`
  - Falla si un color prohibido excede un ratio dentro del viewport activo.
  - Uso: detectar negro interno por corrupcion (similar a `assert-no-inner-black`).

- `edge_stability`
  - Verifica que una banda de borde (ej. columna izquierda) no tenga cambios
    espurios entre frames.
  - Uso: validar fine scroll y clipping horizontal.

- `local_diff_budget`
  - Limita el porcentaje de pixeles cambiados en ROIs donde solo deberia haber
    movimiento global suave.
  - Uso: detectar tile pop, tearing, parches fuera de tiempo.

## Modelo de contrato sugerido

Archivo: `demos/<demo>/pixel-contract.json`

```json
{
  "version": 1,
  "viewport": {
    "mode": "auto_non_black"
  },
  "defaults": {
    "rgbTolerance": 8,
    "maxErrorRatio": 0.01
  },
  "segments": [
    {
      "name": "scroll-right-fine",
      "frames": [20, 40],
      "checks": [
        {
          "type": "shifted_region_match",
          "roi": { "x": 24, "y": 24, "w": 256, "h": 192 },
          "dx": -1,
          "dy": 0,
          "maxErrorRatio": 0.015
        },
        {
          "type": "edge_stability",
          "roi": { "x": 0, "y": 0, "w": 8, "h": 256 },
          "maxChangedRatio": 0.002
        }
      ]
    }
  ],
  "globalChecks": [
    {
      "type": "forbidden_color_ratio",
      "color": [0, 0, 0],
      "maxRatio": 0.001
    }
  ]
}
```

Notas:
- Coordenadas de ROI relativas al viewport activo, no al PNG completo.
- `frames: [a, b]` significa aplicar checks a pares consecutivos `a->a+1 ... b-1->b`.
- `dx/dy` es desplazamiento esperado del contenido visible.

## Flujo de implementacion recomendado

### Fase 1 (MVP util inmediato)

- Crear `tools/analyze/assert-pixel-contract.ps1` (wrapper).
- Crear `tools/analyze/assert-pixel-contract.py` con:
  - carga de `frame_*.png`;
  - deteccion viewport `auto_non_black`;
  - checks `shifted_region_match`, `equal_region`, `forbidden_color_ratio`;
  - salida JSON + Markdown + exit code.
- Integrar en `demos/101_ehb_tile_scroll_driver/analyze-sequence.ps1` con flag
  `-PixelAssert` y opcion estricta `-RequirePixelAssertOk`.

### Fase 2 (robustez)

- Añadir `template_anchor` y `edge_stability`.
- Correlacionar `run-report.json` para seleccionar segmentos por telemetria
  (`cameraX/Y`, flags de prefetch, etc.).
- Guardar overlays de error por frame (`diff-mask`) para diagnostico rapido.

### Fase 3 (optimizacion y escalado)

- Paralelizar por pares de frames.
- Activar OpenCV opcional para busqueda/plantillas mas robusta si es necesario.
- Integrar en `tools/test-regression.ps1` como columna adicional (`PixelAssert`).

## Criterios de aceptacion sugeridos (demo 101)

- En segmentos de scroll uniforme, `shifted_region_match` debe pasar con el
  desplazamiento esperado y error residual bajo.
- La banda izquierda no debe presentar cambios bruscos en cruces de fine scroll.
- El ratio de negro interno en viewport debe permanecer por debajo del umbral.
- Cambios locales no explicables por scroll deben quedar bajo presupuesto.

## Uso operativo

### Ejecucion manual de secuencia

```powershell
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver `
  -SequenceFrames 16 `
  -SequenceIntervalMs 120
```

### Ejecucion de aserciones de pixel

```powershell
.\tools\analyze\assert-pixel-contract.ps1 `
  -SequenceDir .\out\run\101_ehb_tile_scroll_driver\sequence `
  -Contract .\demos\101_ehb_tile_scroll_driver\pixel-contract.json `
  -RunReport .\out\run\101_ehb_tile_scroll_driver\run-report.json `
  -OutDir .\out\analysis\101_pixel_assert
```

### Integracion en analizador de demo

```powershell
.\demos\101_ehb_tile_scroll_driver\analyze-sequence.ps1 `
  -RequirePixelAssertOk
```

## Salidas esperadas

- `pixel-assert-report.json`
  - resultado por regla y por par de frames;
  - metrica principal (error ratio, changed ratio, etc.);
  - coordenadas ROI;
  - razon de fallo.

- `pixel-assert-summary.md`
  - estado global;
  - tabla de checks fallidos;
  - sugerencias de diagnostico rapido.

- `overlays/`
  - opcional: mascara de diferencias por frame para inspeccion puntual.

## Estrategia de umbrales

- Empezar con umbrales conservadores por demo y refinar con 3-5 ejecuciones
  estables del mismo escenario.
- Evitar umbrales globales unicos para todas las demos.
- Versionar umbrales dentro del contrato (`pixel-contract.json`) para mantener
  trazabilidad cuando cambie el driver.

## Riesgos y mitigaciones

- Variacion por captura/compression: usar PNG sin perdida y tolerancia RGB pequena.
- Alias visual en tilemaps repetitivos: usar varias ROIs y anchors simbolicos.
- Falsos positivos por HUD/overlay: excluir zonas no deterministas del contrato.
- Coste de CPU: downsample por ROI donde no haga falta comparacion full-res.

## Relacion con herramientas actuales

- Complementa, no sustituye, `tools/framescope/frame-scope.ps1`.
- Puede reutilizar la logica de viewport de `tools/analyze/assert-no-inner-black.ps1`.
- Debe convivir con `analyze-frame-sequence.ps1`: ese script valida movimiento
  general; este contrato valida exactitud local por pixel.

## Resultado esperado

Con este sistema, la decision "render correcto o incorrecto" deja de depender de
mirar capturas manualmente. La regresion puede fallar de forma precisa con pruebas
deterministas por frame y por region, incluso en bugs sutiles de scroll y clipping.

## Estado actual (implementado)

Ya existe un MVP funcional integrado en el repositorio:

- Motor Python: `tools/analyze/assert-pixel-contract.py`
- Wrapper PowerShell: `tools/analyze/assert-pixel-contract.ps1`
- Contrato inicial demo 101: `demos/101_ehb_tile_scroll_driver/pixel-contract.json`
- Contratos adicionales demos 050/051/052:
  - `demos/050_blitter_bobs/pixel-contract.json`
  - `demos/051_blitter_shifted_bobs/pixel-contract.json`
  - `demos/052_tile_staging_blits/pixel-contract.json`
- Integracion opcional en secuencia demo 101:
  - `-PixelAssert`
  - `-RequirePixelAssertOk`
- Integracion en secuencia demos 050/051/052:
  - `demos/050_blitter_bobs/analyze-sequence.ps1`
  - `demos/051_blitter_shifted_bobs/analyze-sequence.ps1`
  - `demos/052_tile_staging_blits/analyze-sequence.ps1`
- Integracion en regresion global:
  - `tools/test-regression.ps1 -PixelAssert -RequirePixelAssertOk`
  - nueva columna `PixelAssert` en `regression-report.md`

Checks soportados en MVP:

- `forbidden_color_ratio`
- `equal_region`
- `shifted_region_match`
- `telemetry_shift_match`
- `telemetry_direction_match`

Notas del contrato 101:

- Usa viewport auto (`auto_non_black`) para ignorar borde negro de WinUAE.
- Escala ROIs de coordenadas logicas Amiga (320x256) al viewport capturado.
- Valida direccion de movimiento respecto a telemetria (`cameraX/Y`) en lugar de
  exigir un desplazamiento exacto de 1 px por par, porque la secuencia temporal
  puede avanzar varios frames de juego entre capturas.

Notas contratos 050/051/052:

- 050, 051 y 052 validan estabilidad visual post-READY mediante `equal_region`.
- 051 y 052 ignoran el primer par para evitar falso positivo por transicion de
  arranque (`frames: [1, -1]`).
- Los tres mantienen `forbidden_color_ratio` para detectar corrupcion negra interna.
- Cuando una demo necesita ignorar transiciones de arranque, puede usar
  `ignoreFirstPairs` dentro de cada check para no evaluar los primeros pares
  de frames de un segmento.
- Para checks globales de tipo `forbidden_color_ratio`, se soporta
  `ignoreFirstFrames` para excluir los primeros frames de la evaluacion.

## Troubleshooting rapido

- `compatibleRatio` bajo en `telemetry_direction_match`:
  - revisar signos `cameraXToContentDx` y `cameraYToContentDy`;
  - revisar que `runStatus` de secuencia corresponda al frame capturado.

- Falsos positivos por escala de captura:
  - confirmar `viewport.logicalWidth` y `viewport.logicalHeight` del contrato;
  - reducir ROIs a zonas de alta textura/identidad visual.

- Fallos intermitentes:
  - subir `SequenceFrames` para mayor muestra estadistica;
  - usar `minCompatibleRatio` menos estricto en secuencias con saltos temporales.

## Selftest (positivo + negativo)

El sistema incluye una bateria sintetica para verificar que el motor detecta tanto
casos validos como invalidos:

```powershell
.\tools\analyze\verify-pixel-assert.ps1
```

Casos actuales:

- `positive_equal_region` (debe pasar)
- `negative_equal_region` (debe fallar)
- `positive_shifted_region` (debe pasar)
- `negative_forbidden_color` (debe fallar)

Salidas:

- `out\analysis\pixel-assert-selftest\selftest-summary.md`
- `out\analysis\pixel-assert-selftest\selftest-results.json`

El selftest falla con exit code no cero si cualquier caso produce un resultado
distinto al esperado.

## Overlays de diagnostico

Cuando una regla falla, el motor genera overlays PNG en `<out-dir>/overlays` para
ver rapidamente la zona del error:

- `equal_region` / `shifted_region_match` / `telemetry_*`: resalta en rojo los
  pixeles que no cumplen el emparejamiento esperado.
- `forbidden_color_ratio`: resalta en rojo los pixeles del color prohibido dentro
  del ROI.

Ejemplo en selftest:

- `out\analysis\pixel-assert-selftest\negative_equal_region\report\overlays\equal-fail_equal_region_f000_f001.png`
- `out\analysis\pixel-assert-selftest\negative_forbidden_color\report\overlays\global_forbidden_color_ratio_f001.png`
