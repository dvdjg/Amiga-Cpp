# Formato .amigaprofile

Los archivos `.amigaprofile` son generados por el **Frame Profiler** de la extensión [vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug). Se pueden abrir directamente en Visual Studio Code / Cursor y visualizarse con el Amiga Profile Visualizer.

## Ubicación

Se guardan temporalmente en:
```
%TEMP%\amiga-profile-YYYY.MM.DD-HH.MM.SS.amigaprofile
```
Por ejemplo: `C:\Users\<usuario>\AppData\Local\Temp\amiga-profile-2026.02.07-00.45.15.amigaprofile`

Comando para limpiar: **Amiga: Clean Temp Files** (Ctrl+Shift+P)

## Formatos

### 1. Perfil de un solo frame (`IAmigaProfile`)

Array JSON con estructura de perfil CPU estilo Chrome/V8:

```json
[{
  "nodes": [
    {
      "id": 1,
      "callFrame": {
        "functionName": "(root)",
        "scriptId": "0",
        "url": "",
        "lineNumber": -1,
        "columnNumber": -1
      },
      "hitCount": 0,
      "children": [2, 3, 9],
      "locationId": 0,
      "positionTicks": []
    },
    {
      "id": 4,
      "callFrame": {
        "functionName": "main",
        "scriptId": "main.c",
        "url": "path/main.c",
        "lineNumber": 458,
        "columnNumber": 0
      },
      "hitCount": 30,
      "children": [5, 7, 12, ...],
      "locationId": 3,
      "positionTicks": []
    }
    // ... más nodos
  ]
}]
```

### 2. Perfil multi-frame con capturas (`IAmigaProfileSplit`)

Objeto JSON con perfiles por frame y **capturas de pantalla embebidas**:

```json
{
  "$id": "IAmigaProfileSplit",
  "numFrames": 50,
  "firstFrame": {
    "nodes": [ /* árbol de llamadas igual que formato simple */ ]
  },
  "frames": [
    {
      "nodes": [ /* perfil del frame N */ ],
      "capture": "data:image/jpeg;base64,/9j/4AAQSkZJRg..."  // miniatura JPEG
    }
  ]
}
```

- **numFrames**: número de frames perfilados
- **firstFrame**: perfil agregado del primer frame
- **frames**: array con perfil + captura (thumbnail JPEG en base64) por cada frame

## Campos de cada nodo

| Campo | Descripción |
|-------|-------------|
| `id` | Identificador único del nodo |
| `callFrame` | Información de la función |
| `callFrame.functionName` | Nombre de la función |
| `callFrame.url` | Ruta del archivo fuente |
| `callFrame.lineNumber` | Línea de código |
| `hitCount` | Ciclos/samples en esa función |
| `children` | IDs de nodos hijos (árbol de llamadas) |
| `locationId` | Referencia a ubicación |
| `positionTicks` | Profiling a nivel de línea |

## Nodos especiales

- **(root)**: raíz del árbol
- **[IRQ]**: tiempo en interrupciones
- **[External]**: código externo (p.ej. P61, librerías)
- **[Kickstart]**: código de la ROM Kickstart

## Uso programático

### Parsear con JavaScript/Node.js

```javascript
const fs = require('fs');
const profile = JSON.parse(fs.readFileSync('archivo.amigaprofile', 'utf8'));

// Formato split (multi-frame)
if (profile.$id === 'IAmigaProfileSplit') {
  console.log('Frames:', profile.numFrames);
  profile.frames?.forEach((frame, i) => {
    if (frame.capture) {
      // frame.capture = "data:image/jpeg;base64,..."
      const base64 = frame.capture.split(',')[1];
      const buffer = Buffer.from(base64, 'base64');
      fs.writeFileSync(`frame-${i}.jpg`, buffer);
    }
  });
}

// Extraer top funciones por hitCount
function getTopFunctions(nodes, top = 10) {
  return nodes
    .filter(n => n.callFrame?.functionName && !n.callFrame.functionName.startsWith('('))
    .sort((a, b) => (b.hitCount || 0) - (a.hitCount || 0))
    .slice(0, top)
    .map(n => ({ name: n.callFrame.functionName, hits: n.hitCount }));
}
```

### Scripts del proyecto (bash, salida PNG)

En el repo:

- **`scripts/parse-amigaprofile.sh <archivo.amigaprofile> [dir_salida]`** — Parsea el JSON, imprime resumen (formato, numFrames, top nodos por hitCount) y, si se pasa directorio, extrae las capturas de cada frame como **PNG** (sin pérdida). Requiere `jq`, `base64` y, para JPEG→PNG, ImageMagick.
- **`scripts/parse-latest-amigaprofile.sh [dir_salida]`** — Busca el `.amigaprofile` más reciente en `$TEMP` y llama al parser anterior.

**Instalar jq** (si falta): [jqlang.github.io/jq](https://jqlang.github.io/jq/) — en Windows (Git Bash) se puede usar Chocolatey (`choco install jq`) o descargar el binario; en Linux/macOS suele estar en el gestor de paquetes.

## Correspondencia entre capturas y código C/ASM

**Sí, las capturas se corresponden con el código de la aplicación.**

Cada frame del perfil multi-frame (`IAmigaProfileSplit`) contiene:

1. **`nodes`**: Árbol de llamadas con funciones de tu código fuente:
   - `callFrame.url` → ruta del archivo (main.c, gcc8_c_support.c, gcc8_a_support.s, etc.)
   - `callFrame.functionName` → nombre de la función
   - `callFrame.lineNumber` → línea en el archivo
   - `hitCount` → ciclos DMA/samples en esa función

2. **`capture`**: Miniatura JPEG de la pantalla del Amiga en ese momento.

**Correlación temporal**: La captura del frame N muestra lo que estaba **visible en pantalla** cuando se perfiló ese frame. Los `nodes` del frame N indican qué **código C/ASM** estaba ejecutándose (p.ej. main, Wait10, blit, p61Music). Es decir: captura = salida visual; nodes = pilas de ejecución.

Ejemplo: si en frame 5 la captura muestra el logo ABYSS y sprites animados, los nodes mostrarán funciones como `main`, `debug_clear`, `debug_filled_rect`, los blits de bob, `p61Music`, etc.

## Workflow de análisis (para IA)

Para analizar un archivo `.amigaprofile` en el futuro:

1. **Leer el archivo**: JSON, puede ser muy grande (varios MB).
2. **Detectar formato**: `$id === "IAmigaProfileSplit"` → multi-frame con capturas.
3. **Extraer capturas**: buscar `"data:image/jpeg;base64,..."`, decodificar, guardar como .jpg.
4. **Leer imágenes**: usar `Read` sobre los .jpg para "ver" la salida visual.
5. **Extraer perfiles**: iterar `nodes`, ordenar por `hitCount`, correlacionar con archivos/lineas del código.
6. **Correlacionar**: frame N → captura N + nodes N → qué código produjo esa imagen.

### Extraer capturas como PNG (bash)

```bash
./scripts/parse-amigaprofile.sh /ruta/al/archivo.amigaprofile doc/captures
# Crea doc/captures/profile-capture-00.png, 01.png, ...
```

## Referencias

- [vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug) - Extensión
- Basado en formato de perfil CPU de Chrome DevTools / vscode-js-profile-visualizer
