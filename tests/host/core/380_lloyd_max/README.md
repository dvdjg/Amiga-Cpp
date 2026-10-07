# HOST-380: Lloyd-Max genérico

Valida `eng/core/util/quantizer.hpp`, una plantilla sin heap para entrenar centroides. El test documenta y ejercita las instanciaciones `float` y `Fixed<s32,16>`, y el tope de niveles como parámetro de plantilla (`lloyd_max<float, 4>`).

```bash
bash tools/run-host-tests.sh tests/host/core/380_lloyd_max
```
