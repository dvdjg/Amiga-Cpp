# 100 - HOTLINE (Superhot-like first-person, 2.5D)

Prototipo de juego first-person por nodos inspirado en Superhot: el tiempo solo avanza cuando el jugador se mueve. Los personajes usan una **malla única de 55 vértices** con esqueleto de 6 huesos y skinning rígido con pesos suaves en hombros/caderas. La sala es 2.5D, con fondo pre-renderizado, 3 bitplanes y relleno exacto de las caras por color. La UI también se dibuja con polígonos.

## Estado

**Vertical slice funcional.** Arranca, dibuja la sala, tres personajes con malla esqueletada, coberturas, arma y HUD de polígonos. Tiempo detenido, disparo, colisión, muerte, victoria y reinicio están implementados. La demo se auto-demuestra a los 6 s sin input.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/100_hotline --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/100_hotline --config A500_release
```

## Diseño

- 320x240 jugables + 16 px de HUD; **3 planos** (negro, blanco, dos grises y tres rojos principales) y 3 buffers.
- Cuatro vistas pre-renderizadas de la sala; el jugador avanza/retrocede por nodos y gira 90 grados.
- **Malla única por personaje**: 55 vértices y 67 caras triangulares, 6 huesos (`root`, `neck`, `arm_L`, `arm_R`, `leg_L`, `leg_R`). Las caras se rasterizan exactamente; no hay cajas por segmento.
- **Skinning con transición**: además del hueso primario de cada vértice, los hombros, caderas y base del cuello reparten peso hacia el hueso vecino (`skin_weight`), suavizando las costuras de la unión.
- **Tres rojos estáticos y coherentes**: cabeza `kRedLight`, torso `kRed`, visor `kRedDark`; los grises de brazos/piernas se asignan una sola vez desde la normal de rest y una luz fija superior/frontal. No se recalcula iluminación por frame.
- **Render exacto optimizado**: buffer de color por píxel con painter de caras, culling de caras traseras y **una máscara 1bpp por color** (en Chip) + **un cookie-cut por plano**. Son 5 colores × 3 planos, no un blit por cara.
- **Coberturas** como cajas grises; la línea de visión 2D bloquea disparos y el fuego enemigo.
- **UI poligonal**: chevron del logo, pips de enemigos vivos, indicador de estado, arma y mirilla. Sin texto en el HUD.
- Tiempo detenido: solo `up/down` avanza el mundo (enemigos y balas). Girar y disparar no mueven el tiempo.
- Un impacto mata. Disparo por raycast 2D al centro de la mirilla; los enemigos disparan proyectiles que se esquivan moviéndose.
- `Enemy::anim` avanza con cada tick y mueve brazos/piernas; `aim` levanta los brazos cuando el enemigo ve al jugador.

## Rendimiento medido

`measure-fps` (A500_release, ventana de 12 s): **21,81 fps emulados de media**, 325.185 ciclos/frame, 2,29 campos/frame. La media mezcla reposo a ~50 fps con ventanas de acción de **5,25..9,69 fps**; la cadencia es inestable porque solo se repinta al actuar. Las 67 caras × 3 enemigos se rasterizan con painter y culling; el color se materializa con 5 máscaras y 15 cookie-cuts (5 colores × 3 planos).

## Validación

- Captura limpia: `out/vision-review/100_hotline_3reds.png`.
- La revisión local con Ollama confirma **3 tonos de rojo** y luz coherente hacia arriba; no ve huecos ni artefactos de píxel. La lectura sigue siendo low-poly/modular por el estilo del modelo, no por cajas separadas.
- Secuencia de attract (`out/run/100_hotline/A500_release/sequence`): las poses cambian en cada tick, el disparo elimina pips y termina en victoria.
- `flicker-check` marca candidatos por la animación discreta y el OSD de WinUAE; el detector no distingue bien una pose discreta de un glitch.
- La inyección de joystick del runner/MCP no llegó al juego en esta sesión; el camino `poll_input` con joystick físico queda **SIN VERIFICAR**.

## Límites actuales

- El culling y la unión rígida dejan pequeñas transiciones duras al girar mucho las articulaciones.
- Las ventanas de acción están en ~5–10 fps; la media de 21,8 fps no es un presupuesto continuo.
- Sin portada, menú, audio ni varios niveles.
- Sin commit de esta iteración; demo nueva fuera de la regresión global.
