# Plan: Input vía Kernel (Intuition + input.device)

Plan para reemplazar el acceso directo al hardware (JOY0DAT, CIA, keyboard.device) por las APIs del kernel de Amiga. Referencia principal: `amiga mouse.md` (Downloads).

---

## 1. Contexto y motivación

### Problema actual

El engine usa **hardware directo** para input:

- **Ratón**: JOY0DAT (0xdff00a) para deltas, CIA (0xbfe001, 0xdff016) para botones
- **Teclado**: keyboard.device con KBD_READEVENT (SendIO/CheckIO)

Esto es frágil: no portable entre modelos, rompe multitarea si no se hace con cuidado, y en modo TakeSystem puede dejar de funcionar correctamente.

### Recomendación (amiga mouse.md)

| Nivel | API | Cuándo | Ventajas |
|-------|-----|--------|----------|
| Alto (GUI) | **Intuition.library** + IDCMP | Ventanas con botones y gadgets | Botones y string gadgets ya gestionan ratón/teclado |
| Medio (juegos) | **input.device** + handler | Pantalla completa + GUI custom | Máximo control, eventos raw de ratón y teclado |
| Bajo | gameport/keyboard.device | Muy específico | Casi nunca necesario |
| Hardware | JOY0DAT / CIA | Evitar | Rápido pero frágil |

---

## 2. Arquitectura objetivo

### Dos mundos, dos modos

1. **Intuition (interfaz principal)**
   - Custom screen + ventana borderless
   - Gadgets (boolean, string)
   - IDCMP: IDCMP_GADGETUP, IDCMP_MOUSEBUTTONS, IDCMP_MOUSEMOVE, IDCMP_RAWKEY
   - Usado para: menú principal, launcher, configuración

2. **input.device (demos/juegos)**
   - Handler con prioridad 60 (> Intuition 50)
   - Procesa InputEvent (IECLASS_RAWMOUSE, IECLASS_RAWKEY)
   - Usado para: cada demo/juego que se carga desde el menú

### Flujo de estados

```
[MENU - Intuition]
  - Custom screen + window + gadgets
  - Wait() en window port, procesar IDCMP
  - Al seleccionar "Demo 1" → cerrar window/screen, TakeSystem

[DEMO - input.device]
  - TakeSystem()
  - Instalar input handler (IND_ADDHANDLER)
  - Bucle: PollInput() lee estado acumulado por handler
  - Ambos botones → quitar handler, FreeSystem, volver a MENU
```

---

## 3. API de abstracción unificada

El engine expondrá una API que la app usa sin saber si por debajo es Intuition o input.device:

```c
/* Estado de ratón (común) */
typedef struct {
    WORD x, y;              /* Posición absoluta en pantalla */
    UBYTE left;             /* Botón izquierdo pulsado */
    UBYTE right;            /* Botón derecho pulsado */
    UBYTE left_click;       /* Transición: no-pulsado → pulsado este frame */
    UBYTE right_click;
} EngineMouseState;

/* Evento de tecla (común) */
typedef struct {
    UWORD code;             /* Raw keycode (ie_Code & 0x7F) */
    UWORD qualifier;        /* Qualifiers (Shift, Ctrl, Alt) */
    UBYTE pressed;          /* 1=down, 0=up */
} EngineKeyEvent;

/* API alta nivel */
void engine_input_poll(void);           /* Llamar 1x por frame */
void engine_input_get_mouse(EngineMouseState *out);
int engine_input_get_key(EngineKeyEvent *out);  /* -1 si no hay evento */
```

Por debajo:
- **Modo Intuition**: `engine_input_poll` lee de cola IDCMP, actualiza estado
- **Modo input.device**: handler acumula en buffers, `engine_input_poll` copia estado

---

## 4. input.device (demos/juegos)

**Arquitectura**: Intuition SOLO para menú. input.device SOLO durante demos.
Al salir de demo: IND_REMHANDLER + CloseDevice, volver a Intuition. Ver `doc/input-device-rkm.md`.

### Handler (RKM Devices)

- A0 = cadena InputEvent, A1 = is_Data
- Retorna 0 (D0) = consumir todo, no propagar
- Prioridad 60 > Intuition 50

### Ciclo de vida

- **Entrar demo**: cerrar Intuition → TakeSystem → OpenDevice + IND_ADDHANDLER
- **Salir demo**: IND_REMHANDLER → CloseDevice → FreeSystem → reabrir Intuition

### Referencia

- RKM Devices: Input Device
- AmigaOS wiki: Input Device
- Patrón habitual: handler de prioridad > Intuition, RAWKEY + RAWMOUSE

---

## 5. Intuition (menú principal)

### Setup

- OpenScreen (custom, 320×256, 32 colores)
- OpenWindow (borderless, WA_CustomScreen, WA_Gadgets)
- Gadgets: boolean (botones), opcional string (cajas de texto)
- IDCMP: IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY

### Procesamiento

- Wait(1 << window->UserPort->mp_SigBit)
- GetMsg(), ReplyMsg()
- Interpretar IntuiMessage → actualizar EngineMouseState, encolar EngineKeyEvent

---

## 6. Fases de implementación

### Fase A: input.device (prioridad) — COMPLETADA

1. ✅ Crear `engine/src/input_devices.c`: OpenInput, CloseInput, handler
2. ✅ Handler: IECLASS_RAWMOUSE + IECLASS_RAWKEY → buffers globales
3. ✅ engine_input_poll (modo devices): leer buffers
4. ✅ Reemplazar input.c actual: engine_mouse_*, engine_key_* delegan a input.device
5. ✅ TakeSystem/FreeSystem: engine_input_devices_open() al tomar sistema, engine_input_devices_close() al liberar
6. **Test pendiente**: demo scroll+bobs con ratón y teclado vía input.device (WinUAE/Coppenheimer)

### Fase B: Intuition (calculadora + Demo) — COMPLETADA

1. ✅ Crear `app/intuition_calc.c`: OpenScreen, OpenWindow, gadgets
2. ✅ Calculadora: string gadget (display), boolean gadgets (0-9, +, -, *, /, =, C)
3. ✅ Botón "Demo" → cerrar Intuition, TakeSystem, run efecto
4. ✅ **Test**: WinUAE — UI nativa Amiga con botones ROM

### Fase C: Integración y transición

1. app/main.c: arrancar en modo Intuition (menú)
2. Al seleccionar demo: cerrar Intuition → TakeSystem → instalar input.device → run demo
3. Al salir demo: quitar handler → FreeSystem → reabrir Intuition (menú)
4. **Test**: flujo completo menú ↔ demo

---

## 7. Documentación de referencia

- **amiga mouse.md** (Downloads): resumen de niveles, input.device, Intuition
- **ROM Kernel Reference Manual – Devices**: Input Device, IECLASS_*, IND_*
- **ROM Kernel Reference Manual – Libraries**: Intuition, IDCMP, Gadgets

---

## 8. Extensiones futuras

Con input.device e Intuition estables, valorar:

- Helpers KS 1.3 para puertos y IORequest si se duplican en varios módulos
- Tabla raw→ASCII y API de teclado de más alto nivel si el menú o el juego lo exigen
- `engine_input_edges` y `engine_key_held` en el engine actual cubren flancos y estado mantenido
