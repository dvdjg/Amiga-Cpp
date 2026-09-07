#pragma once

/// \file sort.hpp
/// Ordenación genérica de vistas sobre memoria contigua (sin STL).
///
/// Port de `libmisc/sort.c` de `demoscene-repo-orig` (`SortItemArray`). El
/// original ordenaba solo `SortItemT { short key, short index }` por `key` con
/// un quicksort clásico que cae a *insertion sort* en trozos pequeños
/// (THRESHOLD = 12·sizeof(SortItemT)). Esta versión generaliza el mismo
/// algoritmo a cualquier `Span<T>` con un comparador, conservando el patrón
/// "quick partido + inserción en trozos cortos" que hace el original rápido en
/// listas casi ordenadas y barato en trozos pequeños.
///
/// Uso:
///   eng::SortItem items[N];
///   eng::quick_sort(Span<eng::SortItem>{items}, [](auto& a, auto& b) { return a.key <= b.key; });
///   eng::sort_items(Span<s32>{arr}, [](s32 a, s32 b) { return a <= b; });

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng {

/// Par clave/índice, equivalente a `SortItemT` del repositorio origen.
struct SortItem {
    s16 key;
    s16 index;
};

namespace detail {

/// Inserción directa sobre `[first, last]` (inclusive). Estable y barata en
/// trozos pequeños; es el caso base del quicksort para evitar recursión en
/// listas cortas.
template <typename T, typename Less>
constexpr void insertion_sort(Span<T> items, usize first, usize last, Less less) {
    for (usize i = first + 1; i <= last; ++i) {
        T this_ = items[i];
        usize j = i;
        while (j > first && less(this_, items[j - 1])) {
            items[j] = items[j - 1];
            --j;
        }
        items[j] = this_;
    }
}

} // namespace detail

/// Umbral de tamaño para cambiar de quicksort a inserción (en bytes).
///
/// Se usa sobre el rango de memoria (como el `THRESHOLD` del original) para
/// poder mantener el mismo comportamiento "rápido en casi-ordenadas" sin
/// depender del tipo concreto: `12 * sizeof(T)` es una densidad razonable.
template <typename T>
inline constexpr usize kInsertionThreshold = 12 * sizeof(T);

/// Ordena `items` de menor a mayor según `less` (quick sort + insertion sort).
///
/// `less(a, b)` debe ser un orden débil estricto (p. ej. `a <= b`). Complexity
/// O(n·log n) esperado; recursión limitada porque los trozos pequeños caen a
/// inserción. No asigna memoria y no usa excepciones.
template <typename T, typename Less>
constexpr void quick_sort(Span<T> items, Less less) {
    if (items.size() < 2u) {
        return;
    }
    // Todo el procedimiento se hace con índices locales para no pagar la
    // recursión sobre la pila más de lo necesario.
    enum { kStack = 64 };
    usize lo_stack[kStack];
    usize hi_stack[kStack];
    usize sp = 0;
    usize lo = 0;
    usize hi = items.size() - 1u;

    for (;;) {
        while (lo < hi && hi - lo > kInsertionThreshold<T>) {
            // Partición cernidor: pivot en el primer elemento.
            usize pivot = lo;
            usize left = lo + 1u;
            usize right = hi;
            while (left < right) {
                while ((left < hi) && less(items[left], items[pivot])) {
                    ++left;
                }
                while ((right > lo) && !less(items[right], items[pivot])) {
                    --right;
                }
                if (left < right) {
                    T tmp = items[left];
                    items[left] = items[right];
                    items[right] = tmp;
                }
            }
            // El pivot va a su posición final (right).
            T tmp = items[pivot];
            items[pivot] = items[right];
            items[right] = tmp;

            // Empuja la partición más grande a la pila y continúa con la pequeña,
            // para acotar la pila a O(log n) incluso en el caso degenerado.
            if (right - lo < hi - right) {
                lo_stack[sp] = right + 1u;
                hi_stack[sp] = hi;
                ++sp;
                hi = right - 1u;
            } else {
                lo_stack[sp] = lo;
                hi_stack[sp] = right - 1u;
                ++sp;
                lo = right + 1u;
            }
        }
        // Trozo corto (o rango invertido): inserción directa.
        if (hi - lo <= kInsertionThreshold<T>) {
            detail::insertion_sort(items, lo, hi, less);
        }
        if (sp == 0) {
            break;
        }
        --sp;
        lo = lo_stack[sp];
        hi = hi_stack[sp];
    }
}

/// Ordena un array por `key` (equivalente directo de `SortItemArray` del
/// original). El quicksort no es estable: con claves iguales el orden de los
/// `index` no está garantizado, igual que en el C original.
constexpr void sort_items(Span<SortItem> items) {
    quick_sort(items, [](const SortItem& a, const SortItem& b) {
        return a.key <= b.key;
    });
}

} // namespace eng