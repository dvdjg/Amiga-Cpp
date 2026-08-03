# Verificación del display por la IA (sin depender de ti)

Objetivo: que **yo pueda comprobar** que la demo se ve bien (scroll, bobs, overlay) sin que tengas que decirme "sí, se ve bien". Tres vías: **amigaprofile + yo leo la imagen**, **LM Studio (visión)** o **Coppenheimer + Playwright**.

---

## 1. Corrección aplicada (pantalla en blanco)

El fallo de "música pero pantalla en blanco" se debía al **orden de llamadas**: hay que llamar a **TakeSystem()** y **engine_wait_vbl()** **antes** de **effect_create()**. Si se programa el copper/DMA antes de tomar el sistema, el OS puede seguir activo y sobrescribir o no usar nuestra lista. En `app/main.c` el orden correcto está aplicado; vuelve a compilar y ejecutar para confirmar que ya se ve el display.

---

## 2. Cómo puedo verificar yo el display

### Opción A: Amigaprofile + yo leo la imagen (sin LM Studio)

1. Tú: compilas, ejecutas con **F5** (o run-and-capture.ps1), y en la barra de depuración pulsas **Profile** o **Profile (Multi)** para generar un `.amigaprofile` (en %TEMP% o donde guardes).
2. Tú: me dices la ruta del perfil, p. ej. `C:\Users\...\AppData\Local\Temp\amiga_12345.amigaprofile`, o dejas el último en una ruta fija.
3. Yo ejecuto: `bash scripts/parse-amigaprofile.sh <ruta> out/screenshots` y se extraen PNG.
4. Yo leo las imágenes extraídas (read_file sobre los PNG) y compruebo si se ve fondo, bobs y overlay. **No hace falta LM Studio.**

Para que sea repetible: puedes crear un script que (1) lance WinUAE con el exe, (2) espere N segundos, (3) simule o indique cómo generar el Profile (si la extensión expone algo por línea de comandos), o dejar documentado "tras ejecutar con F5, pulsa Profile y copia la ruta del .amigaprofile aquí".

---

### Opción B: LM Studio (visión local)

Tú tienes **LM Studio** con un modelo con visión (VLM) cargado y el servidor API activo (p. ej. en `http://127.0.0.1:1234`).

- **Flujo:**  
  1. Se obtiene una imagen del display (screenshot de Coppenheimer o primer frame extraído de un .amigaprofile).  
  2. Un script envía esa imagen a LM Studio (endpoint tipo chat/completions con contenido imagen).  
  3. El prompt pide: "¿Se ve una demo de Amiga con fondo gráfico y objetos animados? Responde solo SI o NO."  
  4. El script devuelve **exit 0** si la respuesta es "SI", y **exit 1** en caso contrario.

- **Ventaja:** Yo puedo ejecutar ese script como "test de display"; si LM Studio está levantado, obtengo un veredicto automático sin leer yo la imagen.

- **Script:** `scripts/verify-display-with-lmstudio.mjs` (ver más abajo). Uso:  
  `node scripts/verify-display-with-lmstudio.mjs <imagen.png>`  
  o  
  `node scripts/verify-display-with-lmstudio.mjs --amigaprofile <ruta.amigaprofile>`  
  (el script extrae el primer frame a PNG y lo envía a LM Studio).

- Configuración: URL (y opcionalmente modelo) en `.cursor/lmstudio.json` o variables de entorno:
  - `LM_STUDIO_BASE_URL` (por defecto `http://127.0.0.1:1234`)
  - `LM_STUDIO_MODEL` (nombre del modelo con visión cargado en LM Studio; si no se pone, el servidor puede usar el modelo por defecto).
  - Ejemplo `.cursor/lmstudio.json`: `{ "baseURL": "http://127.0.0.1:1234", "model": "llava-..." }`

---

### Opción C: Coppenheimer + Playwright (yo hago la captura)

1. Yo abro Coppenheimer en el navegador (Playwright MCP): `browser_navigate` a la URL.
2. Tú (o un script futuro) cargas el ejecutable / ADF y arrancas la demo.
3. Yo espero unos segundos y hago **browser_take_screenshot** del canvas del emulador; guardo en `out/coppenheimer-frame.png`.
4. Yo **leo esa imagen** (read_file) y compruebo si hay fondo, bobs y overlay; o la paso al script de LM Studio (Opción B) para que dé SI/NO.

Limitación actual: cargar el .exe o ADF en Coppenheimer suele ser manual (drag & drop). Si en el futuro hay API o forma de inyectar el ejecutable por script, el flujo podría ser 100% automático.

---

## 3. Resumen: qué necesito para comprobar yo solo

| Método | Qué haces tú | Qué hago yo |
|--------|----------------|-------------|
| **A. Amigaprofile** | Ejecutar con F5, pulsar Profile, decirme ruta del .amigaprofile (o dejarla fija). | `parse-amigaprofile.sh` → leer los PNG extraídos. |
| **B. LM Studio** | Tener LM Studio con VLM y API activa; opcionalmente generar screenshot (amigaprofile o Coppenheimer). | Ejecutar `verify-display-with-lmstudio.mjs` con esa imagen (o con --amigaprofile); interpretar exit 0 = OK. |
| **C. Coppenheimer** | Cargar y ejecutar la demo en Coppenheimer. | Playwright: screenshot del canvas → leer imagen o pasarla a script LM Studio. |

Recomendación: usar **A** para que yo pueda ver las capturas cuando me pases la ruta del perfil; añadir **B** si quieres un test automatizado (script que yo pueda lanzar) con tu LM Studio local.

---

## 4. Script verify-display-with-lmstudio.mjs

- **Con imagen:** `node scripts/verify-display-with-lmstudio.mjs out/screenshots/profile-capture-00.png`
- **Con amigaprofile:** `node scripts/verify-display-with-lmstudio.mjs --amigaprofile ruta.amigaprofile` (usa el primer frame del perfil).
- La IA puede ejecutar este script como test de display si tienes LM Studio con un VLM cargado y el servidor activo.

## 5. Referencias

- `scripts/parse-amigaprofile.sh` – extrae capturas de un .amigaprofile a PNG.
- `doc/debug-with-ai.md` – perfiles, capturas, Playwright, Coppenheimer.
- `doc/agent-runbook.md` – run-and-capture.ps1, verificación tras cambios.
