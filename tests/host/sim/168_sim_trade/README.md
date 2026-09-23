# HOST-168: trueque y regateo (`eng::sim`)

Test host del **intercambio** de objetos entre criaturas y su efecto económico y social.

## Qué comprueba

1. **`trade_value`**: balance de valor del intercambio (recibes menos/más de lo que das).
2. **`bargain_score`**: sube con el balance de valor y con la **necesidad** de lo recibido.
3. **`execute_trade`**: intercambia materiales, sube la demanda de lo intercambiado y falla
   si a alguna parte le falta lo suyo.
4. **`SimWorld::offer_trade`**: cierra el trato entre criaturas y **sube la reputación** de
   ambas facciones.

## Salida de referencia

```
Sim trade:
OK: Sim trade (valor, regateo, intercambio, reputacion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/168_sim_trade
```
