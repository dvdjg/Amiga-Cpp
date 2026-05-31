# FrameScope: roadmap de analisis visual temporal

FrameScope es un subproyecto propio para analizar secuencias visuales sin depender
de que una IA mire imagenes enteras. Su objetivo es convertir videos, capturas de
WinUAE o carpetas de frames en evidencia compacta: metricas por frame, movimiento
estimado, segmentos temporales, grids ASCII y hojas de contacto anotadas.

No debe llamarse `analyze-scroll-sequence` porque no es una herramienta de scroll.
El scroll de Amiga sera solo un perfil. El nucleo debe servir tambien para videos
descargados, capturas de otros emuladores, animaciones de UI, GIFs convertidos a
frames o material de referencia de juegos comerciales.

## Referencias open source utiles

- PySceneDetect: deteccion de cambios de escena, cortes, fades y export de listas
  de escenas. Es una buena referencia para segmentacion temporal y CLI, pero no
  describe movimiento interno ni rutas de camara.
  https://www.scenedetect.com/
- OpenCV optical flow: referencia tecnica para Lucas-Kanade y Farneback. Debe ser
  la base de una fase futura cuando necesitemos estimar movimiento denso, no solo
  desplazamiento global barato.
  https://docs.opencv.org/4.x/d4/dee/tutorial_optical_flow.html
- FiftyOne: toolkit open source para datasets visuales, imagenes y videos. Es
  interesante si mas adelante queremos guardar corpus de regresion visual, buscar
  ejemplos similares o revisar fallos con UI local.
  https://huggingface.co/docs/hub/main/datasets-fiftyone
- Kinovea: herramienta open source de analisis y anotacion de video orientada a
  movimiento. Es mas manual/interactiva que automatica, pero confirma que
  trayectorias, medidas y comparaciones frame a frame son vocabulario natural para
  este tipo de herramienta.
  https://www.kinovea.org/

## Principios

- Primero metricas deterministas, despues IA visual si hace falta.
- La salida principal para IA debe ser texto/JSON pequeno.
- Las imagenes completas se reservan para inspeccion humana o para un segundo
  observador local.
- Toda prueba debe poder fallar con una razon concreta: movimiento contrario,
  frame congelado, salto brusco, corrupcion localizada, ruta no alcanzada o
  segmento ausente.
- La herramienta debe aceptar entradas genericas: carpeta de frames, video local y,
  en una fase posterior, URL descargada mediante una utilidad externa.

## Artefactos generados

La version inicial vive en:

```powershell
.\tools\framescope\frame-scope.ps1
```

Entradas soportadas:

- carpeta con `png`, `jpg`, `jpeg` o `bmp`;
- video local `mp4`, `mkv`, `mov`, `avi`, `webm`, `mpg`, `mpeg` o `m4v` si existe
  `ffmpeg` en `PATH`.

Salidas:

- `framescope-report.json`: informe completo para herramientas;
- `framescope-summary.md`: resumen legible y grids ASCII;
- `framescope-contact-sheet.png`: hoja de contacto anotada;
- segmentos de movimiento: frame inicial, final, direccion, numero de muestras y
  diferencia media.

## Fase 0: MVP determinista

Estado: implementado.

Capacidades:

- downsample a rejilla configurable;
- color dominante aproximado por celda;
- luma media y firma binaria por frame;
- diferencia media entre frames;
- proporcion de celdas cambiadas;
- diferencias por cuadrante;
- estimacion de desplazamiento global con busqueda local;
- segmentacion cuando cambia la direccion estimada;
- hoja de contacto con `diff`, direccion y desplazamiento.

Pruebas:

```powershell
.\tools\framescope\frame-scope.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -OutDir .\out\framescope\101_latest `
  -ExpectAnimated
```

La salida debe indicar `Status = ok` y generar `framescope-summary.md` sin necesitar
incluir las imagenes completas en la conversacion.

## Fase 1: perfil WinUAE/Amiga

Objetivo: relacionar lo que la demo cree estar haciendo con lo que se ve.

Estado: implementacion inicial disponible mediante `-Profile amiga-scroll`.

Tareas:

- leer `run-report.json` junto a la secuencia cuando exista; estado: hecho;
- correlacionar `runStatus.detail`, `frame`, `cameraX`, `cameraY`, trabajos de tile
  y flags de prefetch; estado: hecho;
- comparar direccion esperada del contenido contra direccion visual observada;
  estado: hecho;
- generar un `expected-motion.json` desde la demo o desde un perfil declarativo;
- comprobar que el movimiento visual observado coincide con la ruta esperada;
- detectar cambios dentro del rectangulo visible que no sean explicables por
  scroll uniforme.

Aceptacion para la demo 101:

- detectar segmento derecha;
- detectar segmento izquierda;
- detectar segmento arriba;
- detectar segmento abajo;
- detectar orbita o cambio continuo de direccion;
- confirmar que el prefetch no produce cambios locales bruscos dentro del viewport.

Uso diagnostico actual:

```powershell
.\tools\framescope\frame-scope.ps1 `
  -Source .\out\run\101_ehb_tile_scroll_driver\sequence `
  -OutDir .\out\framescope\101_amiga_scroll `
  -Profile amiga-scroll `
  -RequireProfileMatch `
  -ExpectAnimated
```

En la demo 101 actual este modo puede fallar con `profile_mismatch`, que es justo
la evidencia que buscamos antes de corregir el scroll: la telemetria de camara
indica una direccion esperada, pero el desplazamiento visual detectado no coincide.
`analyze-sequence.ps1` ejecuta FrameScope sin `-RequireProfileMatch` para dejar
siempre el diagnostico generado sin romper la regresion hasta que el driver se
corrija.

## Fase 2: video generico y material externo

Objetivo: estudiar videos de referencia, incluidos videos descargados previamente
de YouTube o capturas de otros juegos.

Tareas:

- aceptar videos largos con muestreo temporal;
- extraer escenas con `ffmpeg` y, opcionalmente, PySceneDetect;
- generar mini-clips de segmentos interesantes;
- resumir movimientos por escena;
- detectar bucles, scrolls, parallax, flashes, fades y cortes.

No se descargara contenido desde la herramienta inicial. Si se añade descarga,
sera un wrapper separado para mantener claro el origen del material.

## Fase 3: OpenCV

Objetivo: sustituir la busqueda global barata por estimaciones mas ricas.

Tareas:

- integrar Python + OpenCV como backend opcional;
- estimar flujo sparse con Lucas-Kanade;
- estimar flujo denso con Farneback para regiones seleccionadas;
- exportar vectores agregados por rejilla;
- detectar capas con movimiento distinto, por ejemplo parallax.

Aceptacion:

- en una secuencia con fondo y foreground, detectar al menos dos velocidades;
- en una demo de scroll uniforme, reportar un campo de movimiento coherente;
- en una corrupcion localizada, distinguirla de movimiento global.

## Fase 4: observador IA local

Objetivo: usar vision-language models solo cuando las metricas no basten.

Interfaz recomendada:

- enviar `framescope-contact-sheet.png`;
- enviar `framescope-summary.md` truncado;
- pedir respuesta JSON: objetos visibles, cambios, anomalias y confianza.

Modelo recomendado para 16 GB VRAM:

- Qwen2.5-VL-7B-Instruct cuantizado en LM Studio, preferentemente `Q6_K` o `Q8_0`
  si entra con margen suficiente.

Uso esperado:

- segunda opinion sobre fallos ambiguos;
- descripcion humana de una animacion;
- clasificacion de artefactos visuales que no queremos codificar aun.

## Fase 5: integracion en regresion

Objetivo: que cada demo pueda declarar expectativas temporales.

Formato propuesto:

```json
{
  "profile": "amiga-scroll",
  "expect": [
    {"segment": "right", "minFrames": 4},
    {"segment": "left", "minFrames": 4},
    {"segment": "up", "minFrames": 4},
    {"segment": "down", "minFrames": 4},
    {"segment": "orbit", "minFrames": 8}
  ],
  "maxUnexpectedLocalChangeRatio": 0.08
}
```

Cada demo podra tener `framescope-profile.json`. `tools/test-regression.ps1`
ejecutara FrameScope si detecta ese perfil, igual que ahora detecta
`analyze-sequence.ps1`.

## Siguiente paso recomendado

Crear el perfil WinUAE/Amiga para la demo 101. Esa fase debe leer telemetria de
`run-report.json`, clasificar los segmentos de la ruta visible y fallar si el final
de la animacion no corresponde a la orbita programada.
