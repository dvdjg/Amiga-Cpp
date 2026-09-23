# HOST-204: integración persona ↔ cartas

Test host de `engine/include/eng/cards/ai/persona_bot.hpp`: la capa de persona aplicada al
motor de póker.

## Qué comprueba

1. **Parámetros por arquetipo**: el embustero farolea más que el pardillo; el engreído es
   más agresivo que el timorato; el timorato exige más colchón (más cauto).
2. **El estado modula**: el tilt sube agresión y farol.
3. **Emisión de tells**: el pardillo se delata más que el flemático ante el mismo estado.
4. **Lectura**: tras N showdowns, el observador aprende el tell del pardillo y lo clasifica
   como legible.
5. **Decisión**: `decide_with_persona` produce acciones legales para personas distintas.

## Salida de referencia

```
eng::cards persona:
OK: eng::cards persona (parametros por arquetipo, tells y lectura)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/204_cards_persona
```
