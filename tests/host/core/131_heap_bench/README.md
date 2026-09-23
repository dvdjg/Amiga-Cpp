# HOST-131: medición del heap (R5.5)

Test de **medición** (no de una API nueva): compara el número de comparaciones del heap
binario del engine (`eng::util::PriorityQueue`) frente a un heap **4-ario** equivalente en
una carga tipo *open set* de A* (N inserciones y N extracciones de mínimo). Verifica que
ambos dan la **misma secuencia** y reporta la tabla.

## Resultado (host)

```
  open set   N= 32  binario=   220  cuatro=   253  ratio=1.15
  open set   N= 64  binario=   581  cuatro=   615  ratio=1.06
  open set   N=128  binario=  1405  cuatro=  1502  ratio=1.07
  open set   N=256  binario=  3359  cuatro=  3502  ratio=1.04
```

**Conclusión**: el 4-ario hace **más** comparaciones que el binario → **no se adopta**; se
mantiene `PriorityQueue`. (Decisión de R5.5, registrada en `ROADMAP_UTIL_LIBRARY.md`.)

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/131_heap_bench
```
