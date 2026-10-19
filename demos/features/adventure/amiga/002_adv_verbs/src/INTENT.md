# INTENT — 002_adv_verbs

## Qué hace

Ejercita el **hito A1 de la capa de aventura** (`eng::adv`: verbos, hotspots, say e inventario) sobre contenido propio (nada del juego original) y la misma sala base de la demo 001: un mundo de 352×208 px con tres walkboxes, actor que camina (cámara de mundo con ventana por hardware) y una **barra de verbos clásica** en la franja inferior con frase en construcción, réplicas y los dos primeros objetos del kernel `eng::rpg` (llave y moneda sobre el suelo) más la cerradura de la puerta.

## Qué pretende demostrar

- La tabla de verbos es **contenido** (`eng::adv::VerbSet`: etiqueta, semántica, aridad y cursor por verbo): el runtime no conoce la barra y `resolve_action` valida la frase (aridad `Target` vs `TwoObjects`) antes de ejecutar.
- La prioridad del puntero (GUI → hotspot por profundidad → suelo) con el **hit-test del HUD** de `eng::hud` (dos rejillas: verbos e inventario) y los hotspots propios de sala.
- El ciclo de interacción completo contra el kernel `eng::rpg`: `pick_up` de los objetos del suelo a la `ItemBag` (el inventario se pinta como **proyección** de la bolsa a slots del HUD, `HudSlotView`) y `LockSet::try_open` con la llave (etiqueta `key` + metadato de instancia) para abrir la puerta.
- Las réplicas (`eng::adv::SayQueue`): cola FIFO con duración exacta por VBlank y texto de una tabla de contenido.
- El cursor por verbo (`CursorMode` → flecha/ojo/mano) y la barra con resaltado de verbo seleccionado/resaltado sin repintar toda la franja (`eng::playfield::DiffText`).

## Elementos en pantalla

- **Mundo**: la sala de la 001 (cielo, mar, playa, muelle, cartel, pared con la puerta) más dos props estáticos con save-under: **llave** dorada (132;152) y **moneda** (240;176).
- **Actor y puntero**: BOBs enmascarados con save-under; el puntero cambia de arte con el verbo activo (flecha por defecto, ojo para mirar/hablar, mano para usar/coger).
- **Franja HUD** (48 líneas desde la 252, 1 plano): frase (`VERBO [SUJETO] CON OBJETIVO`), línea de réplica, barra de 9 verbos en 2 filas (MIRA/COGE/USA/ABRE/HABLA/CIERRA/EMPUJA/TIRA/DA) con subrayado del verbo seleccionado y del que está bajo el puntero, inventario (LLAVE/MONEDA con marca de sujeto), nombre `002VERBS`, versión con build y contador de frames `F<num>`.
- **Puerta**: cerrada, es una puerta pintada en el fondo; al abrirla se dibuja encima un prop de hueco oscuro con marco (save-under); cerrarla lo destapa.

## Evolución temporal

- Sin input: ~2 s de paseo y después el **guion de atracción** (determinista, en frames): ABRE PUERTA (réplica «está cerrada»), COGE MONEDA, COGE LLAVE (ambos desaparecen del mundo y aparecen en el inventario), USA + LLAVE + PUERTA (la cerradura se abre, la puerta se abre y suena la réplica «la llave gira…») y un paseo final. Cualquier click real cancela el guion y toma el control (el paseo de atracción se reanuda al soltar).
- Click en la barra: selecciona verbo (frase nueva); click en el inventario con `USA`/`DA`: fija el sujeto; click en un hotspot: ejecuta la frase (con auto-«MIRA» si no hay verbo); click en el suelo: camina.
- Objetivo de presupuesto: 1 frame por VBlank (50 Hz PAL). **Estado: funcional y verificado por secuencia (attract) y visión canónica; medida estable ~48-49 fps (~1,0 campos/frame)** con `measure-fps` (ventanas de régimen 47,9-49,1; la media global la bajan las ventanas de arranque), y **flicker declarado** con candidatos en zonas esperadas de la franja (texto que cambia por diseño). Detalle, alcance y lo no cubierto: `VALIDATION.md`.
