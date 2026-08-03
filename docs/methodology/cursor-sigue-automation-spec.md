# Especificación: Automatización “sigue” para Cursor (Windows / WSL)

Documento de diseño para una aplicación nativa en Windows (o con componentes en WSL) que detecte cuándo el agente de Cursor ha terminado un turno, analice el resultado (con modelos locales en LM Studio) y envíe “sigue” o más instrucciones. Incluye **visión artificial** (o alternativas) para localizar la caja de entrada cuando se usa el agente local en el IDE.

---

## 1. Objetivo y alcance

- **Objetivo:** Reducir la intervención manual en conversaciones largas con el agente de Cursor: que un programa decida cuándo enviar “sigue” o instrucciones adicionales y lo haga de forma automática.
- **Alcance:** Soporte para (A) **Background Agents API** (agente en la nube) y (B) **agente local en el IDE** (ventana de Cursor). En (B) hace falta localizar la caja de texto donde se escribe “sigue” (visión o UI Automation).
- **Modelos locales:** Uso de LM Studio para: decisión “sigue” / “listo” / “bloqueo”, y opcionalmente visión (análisis de captura de pantalla para encontrar el área de entrada).

---

## 2. Modos de funcionamiento

| Modo | Disparador | Cómo enviar “sigue” | Visión / UI |
|------|------------|----------------------|-------------|
| **API** | Webhook POST de Cursor | Llamada HTTP a add follow-up | No necesaria |
| **IDE local** | Notificación Windows o polling/ventana | Simular teclado en la caja de entrada | Necesaria para localizar la caja |

- **Modo API:** Servidor HTTP recibe webhook → analiza con LM Studio → POST add follow-up. No requiere visión ni acceso a la ventana de Cursor.
- **Modo IDE local:** Se necesita saber **dónde** está la caja de texto (o el foco) para escribir “sigue”. Ahí entran visión artificial y/o UI Automation (ver §4).

---

## 3. Uso de LM Studio

LM Studio expone una **API compatible con OpenAI** en local (por defecto `http://localhost:1234/v1/...`). Documentación: [LM Studio – OpenAI compatibility](https://lmstudio.ai/docs/developer/openai-compat).

### 3.1 Decisión “sigue” / “listo” / “bloqueo”

- **Endpoint:** `POST http://localhost:1234/v1/chat/completions`
- **Uso:** Enviar la última respuesta del agente (o resumen) en un mensaje de usuario y un system prompt que fije el rol, por ejemplo:

  ```
  Eres el supervisor. El agente de Cursor ha respondido con el siguiente texto.
  Responde con exactamente una de estas opciones:
  - SIGUE — si el trabajo no está completo y no hay bloqueo ni decisión para el humano.
  - LISTO — si el objetivo está cumplido.
  - BLOQUEO: <breve razón> — si hace falta intervención humana (error, decisión, etc.).
  No des más texto que la opción.
  ```

- **Implementación:** Cliente HTTP (fetch, axios, httpx, HttpClient, etc.) con JSON estándar de chat completions; leer `choices[0].message.content` y actuar en consecuencia (enviar “sigue”, parar, o notificar al usuario).

### 3.2 Visión para localizar la caja de entrada (opcional)

Si usas un **modelo con visión** en LM Studio (multimodal):

- **Entrada:** Captura de pantalla (o región de la ventana de Cursor) en base64 (PNG/JPEG).
- **Prompt:** “En esta captura de la ventana de Cursor/VS Code, ¿dónde está la caja de texto donde el usuario escribe mensajes al agente (chat input)? Responde solo con las coordenadas del centro del área en píxeles: X,Y.”
- **Salida:** Parsear X,Y y usar esas coordenadas para hacer clic y luego escribir “sigue” + Enter (SendInput / pyautogui, etc.).

Alternativa sin modelo de visión: **UI Automation** o **detección por plantilla/OCR** (ver §4).

---

## 4. Visión artificial y localización de la caja “sigue”

La “caja” es el área donde se escribe en el chat del Agent (agente local en el IDE). Cursor suele ser una app **Electron**; la entrada puede ser un `contenteditable` o un input dentro del árbol de accesibilidad.

### 4.1 Opción A: UI Automation (Windows)

- **Tecnología:** Windows UI Automation (UIA); desde C# (`System.Windows.Automation`), C++ (IUIAutomation) o Python vía **pywinauto** (que usa UIA o Win32 según backend).
- **Idea:** Conectar a la ventana de Cursor por título/clase, recorrer el árbol de elementos, buscar por:
  - `ControlType.Edit` o control tipo “document”/“edit”,
  - `Name` o `AutomationId` que contenga “message”, “input”, “chat”, “composer”, etc. (inspeccionar con **Inspect.exe** o **Accessibility Insights** para ver nombres reales).
- **Ventaja:** No depende de píxeles ni de tema; robusto si Cursor expone nombres/roles estables.
- **Inconveniente:** Electron a veces expone poco texto útil; puede que haya que buscar por jerarquía (panel lateral → área de chat → último control editable).

Sugerencia: usar **Inspect.exe** (Windows SDK) o **Accessibility Insights** con Cursor abierto y el Agent visible, anotar el `ControlType`, `Name`, `AutomationId` del control de entrada para usarlos en condiciones de búsqueda.

### 4.2 Opción B: Captura + modelo de visión (LM Studio u otro)

- **Flujo:** Captura de pantalla (ventana de Cursor o pantalla completa) → enviar imagen a un modelo con visión en LM Studio (o a otro servicio local/remoto).
- **Prompt:** Pedir coordenadas del “chat input” o del “botón Send” (por ejemplo: “Devuelve solo el rectángulo: left,top,right,bottom en píxeles”).
- **Uso:** Calcular centro del rectángulo, mover ratón y clicar, luego escribir “sigue” + Enter.
- **Ventaja:** Funciona aunque el árbol UIA sea pobre; se adapta a cambios visuales si el modelo generaliza.
- **Inconveniente:** Depende de un modelo de visión local (LM Studio u otro) y de la resolución/DPI; hay que definir bien la región a capturar (ventana vs pantalla).

### 4.3 Opción C: Detección por imagen (sin LLM visión)

- **Template matching (OpenCV):** Guardar una pequeña imagen de referencia del icono “Send” o del borde del input; buscar en la captura con `matchTemplate` y usar la posición para clic + escritura.
- **OCR (Tesseract, Windows OCR):** Buscar en la captura texto como “Message”, “Ask Cursor”, “Agent”, etc., y colocar el clic cerca de esa zona (p. ej. debajo o a la derecha).
- **Ventaja:** Sin modelo de visión; rápido y desacoplado de LM Studio para esta parte.
- **Inconveniente:** Frágil ante cambios de tema, idioma o layout de Cursor.

### 4.4 Opción D: Heurísticas y coordenadas relativas

- Obtener posición y tamaño de la ventana de Cursor (GetWindowRect / pywinauto).
- Aplicar offsets fijos (p. ej. “input a 80% de altura, centrado en ancho”) basados en un layout típico, y escribir “sigue” ahí.
- **Ventaja:** Implementación muy simple.
- **Inconveniente:** Se rompe si Cursor cambia el layout o el usuario redimensiona paneles.

### 4.5 Recomendación

- **Prioridad 1:** Probar **UI Automation** (pywinauto o C#) con Cursor abierto e inspeccionar el árbol; si el input tiene nombre o id estable, usarlo.
- **Prioridad 2:** Si UIA no basta, añadir **visión con LM Studio** (modelo multimodal) para “dónde está el input” a partir de captura de la ventana.
- **Respaldo:** Template matching o coordenadas relativas como fallback configurable (p. ej. en un archivo de configuración con márgenes/porcentajes).

---

## 5. Arquitectura sugerida

```
┌─────────────────────────────────────────────────────────────────┐
│                     Disparador (trigger)                         │
│  Modo API: Webhook POST  │  Modo IDE: Notificación / polling     │
└──────────────────────────┬──────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│  Orquestador                                                     │
│  - Obtiene último mensaje del agente (API o no disponible en IDE)│
│  - Llama a “Supervisor” con el texto (y opcionalmente captura)   │
└──────────────────────────┬──────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│  Supervisor (LM Studio)                                          │
│  - Input: texto (y opcionalmente imagen para visión)             │
│  - Output: SIGUE | LISTO | BLOQUEO: razón                        │
└──────────────────────────┬──────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│  Ejecutor                                                        │
│  - SIGUE → Modo API: POST add follow-up "sigue"                  │
│            Modo IDE: localizar input (§4) + teclear "sigue" + ↵   │
│  - LISTO → Parar; opcional notificación al usuario               │
│  - BLOQUEO → Notificar (toast, log, archivo)                     │
└─────────────────────────────────────────────────────────────────┘
```

- **Módulos recomendados:** Trigger, Orquestador, Supervisor (cliente LM Studio), Localizador de input (UIA y/o visión), Ejecutor (API Cursor + simulación teclado).

---

## 6. Stack técnico y entorno

| Componente | Windows nativo | WSL |
|------------|-----------------|-----|
| Servidor webhook (Modo API) | Python (FastAPI/Flask), Node, C# (ASP.NET) | Mismo; exponer con ngrok/túnel si Cursor está en la nube |
| Cliente LM Studio | Cualquier HTTP (Python, Node, C#, curl) | Mismo; LM Studio suele correr en Windows, llamar a `localhost:1234` desde WSL (localhost compartido) |
| UI Automation / localizar input | C#, Python (pywinauto), C++ | No disponible en WSL; debe correr en Windows |
| Captura de pantalla | Python (mss, PIL), C# (Graphics.CopyFromScreen), C++ | No para ventanas Windows; solo en Windows |
| Simular teclado/ratón | pyautogui, SendInput (C#/C++), pywinauto | No para ventanas Windows; solo en Windows |

- **Conclusión:** La parte que reacciona al **IDE local** (localizar caja, teclear “sigue”) debe ejecutarse en **Windows**. La lógica de webhook + LM Studio puede estar en Windows o en WSL; si está en WSL, la URL del webhook debe ser accesible desde internet (ngrok) o desde Cursor (Background Agents).

---

## 7. Flujos concretos

### 7.1 Modo API (Background Agents)

1. Usuario (o script) lanza un agente con `POST https://api.cursor.com/v0/agents` incluyendo `webhook_url` apuntando a tu servidor (ej. `https://tu-ngrok.ngrok.io/cursor-webhook`).
2. Al terminar un turno, Cursor envía POST al webhook (ver [docs Cursor webhooks](https://docs.cursor.com/en/background-agent/api/webhooks)); el payload incluye identificador de agente y estado.
3. Tu servidor recibe el POST, (opcionalmente) verifica firma, obtiene el último mensaje o transcript del agente (según API de Cursor).
4. Servidor envía a LM Studio el texto (y opcionalmente captura si usas visión) y recibe SIGUE / LISTO / BLOQUEO.
5. Si SIGUE: `POST` al endpoint de add follow-up (docs: [add follow-up](https://docs.cursor.com/en/background-agent/api/add-followup)) con cuerpo `"sigue"` (o variante). Si LISTO o BLOQUEO: parar y notificar.

### 7.2 Modo IDE local (ventana Cursor)

1. **Disparador:** (a) Listener de notificaciones de Windows (Toast), o (b) polling: cada N segundos comprobar si la ventana de Cursor tiene título/estado “listo” (p. ej. ya no “Thinking…”); o (c) monitorear el proceso y ventana (cuando el proceso deja de usar CPU de forma relevante, como heurística de “terminó”).
2. Cuando se considera “turno terminado”: opcionalmente capturar pantalla o ventana de Cursor (para visión). No hay API para “último mensaje” del chat local; se puede dejar el texto vacío y que LM Studio decida “SIGUE” por defecto, o intentar OCR del área de respuesta (frágil).
3. Supervisor (LM Studio): si hay texto (p. ej. de OCR), analizar; si no, devolver SIGUE por defecto hasta un máximo de iteraciones.
4. Localizador (§4): UI Automation o visión para obtener coordenadas/control del input.
5. Ejecutor: foco en Cursor, clic en el input (o ya enfocado), escribir “sigue” + Enter con SendInput / pyautogui.

---

## 8. Seguridad y configuración

- **API key de Cursor:** Guardar en variable de entorno o archivo no versionado; no hardcodear. Usar solo en el servidor que llama a add follow-up y lanza agentes.
- **Webhook:** Si Cursor soporta verificación de firma (ej. header con HMAC), validar en el servidor antes de procesar.
- **LM Studio:** En localhost no exponer a internet; si el servidor está en WSL o otra máquina, LM Studio puede escuchar en una interfaz concreta y restringir por firewall.
- **Configuración sugerida en archivo (JSON/YAML):** Modo (api | ide), webhook_url, cursor_api_key, lm_studio_base_url, reglas de decisión por defecto (max_iterations, “si no hay texto entonces SIGUE”), rutas de captura/plantillas para modo IDE.

---

## 9. Resumen de sugerencias

| Tema | Sugerencia |
|------|------------|
| **Decisión “sigue”** | LM Studio con prompt fijo (SIGUE / LISTO / BLOQUEO); endpoint `/v1/chat/completions`. |
| **Localizar caja en IDE** | 1) UI Automation (pywinauto / C#) inspeccionando Cursor con Inspect.exe. 2) Modelo de visión en LM Studio con captura de ventana. 3) Fallback: template matching o coordenadas relativas. |
| **Modo API vs IDE** | Preferir API para automatización fiable; IDE + visión/automation para no depender de Background Agents. |
| **Dónde corre** | Lógica de teclado y captura/UI: Windows. Servidor webhook y cliente LM Studio: Windows o WSL (LM Studio en Windows, llamar a localhost:1234). |
| **Idioma/stack** | Python (pywinauto, requests, FastAPI) o C# (HttpClient, UIA, SendInput) según preferencia; WSL para partes solo servidor si se desea. |

Este documento puede usarse como base para un desarrollo nativo en Windows (o híbrido Windows + WSL): definir primero modo (API o IDE), luego implementar Trigger → Supervisor (LM Studio) → Ejecutor, y en modo IDE añadir el módulo de localización de la caja (visión y/o UI Automation) según las opciones de §4.
