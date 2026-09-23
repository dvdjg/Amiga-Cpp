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

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

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

/// Orden **estable** con buffer auxiliar `scratch` (merge sort ascendente). Estable
/// significa que las claves iguales conservan su orden relativo (útil cuando el orden
/// de inserción importa: animaciones, capas, ids de red). Sin heap: `scratch` lo
/// aporta el llamador y debe tener el tamaño de `items`; si no, cae a inserción
/// (estable, O(n²)) para listas cortas.
template <typename T, typename Less>
constexpr void stable_sort(Span<T> items, Less less, Span<T> scratch) {
    const usize n = items.size();
    if (n < 2u) {
        return;
    }
    if (scratch.size() < n) {
        detail::insertion_sort(items, 0u, n - 1u, less);
        return;
    }
    // Merge sort ascendente (bottom-up): no recursión, pila O(1).
    for (usize width = 1u; width < n; width *= 2u) {
        for (usize i = 0u; i < n; i += 2u * width) {
            const usize mid = (i + width < n) ? (i + width) : n;
            const usize end = (i + 2u * width < n) ? (i + 2u * width) : n;
            usize a = i;
            usize b = mid;
            usize k = i;
            while (a < mid && b < end) {
                if (less(items[b], items[a])) {
                    scratch[k++] = items[b++];
                } else {
                    scratch[k++] = items[a++]; // empate: primero el de la izquierda
                }
            }
            while (a < mid) {
                scratch[k++] = items[a++];
            }
            while (b < end) {
                scratch[k++] = items[b++];
            }
        }
        for (usize i = 0u; i < n; ++i) {
            items[i] = scratch[i];
        }
    }
}

/// Orden estable **sin** buffer (inserción). Coste O(n²): para listas cortas o casi
/// ordenadas; con listas largas usa la sobrecarga con `scratch`.
template <typename T, typename Less>
constexpr void stable_sort(Span<T> items, Less less) {
    if (items.size() >= 2u) {
        detail::insertion_sort(items, 0u, items.size() - 1u, less);
    }
}

/// Deja en `items[nth]` el elemento que ocuparía esa posición si estuviera ordenado,
/// con los menores (o iguales) antes y los mayores después, **sin ordenar el resto**
/// (quickselect, O(n) esperado). Útil para medianas y `top-k`.
template <typename T, typename Less>
constexpr void nth_element(Span<T> items, usize nth, Less less) {
    const usize n = items.size();
    if (n < 2u || nth >= n) {
        return;
    }
    usize lo = 0u;
    usize hi = n - 1u;
    while (lo < hi) {
        const usize pivot = lo;
        usize left = lo + 1u;
        usize right = hi;
        while (left < right) {
            while (left < hi && less(items[left], items[pivot])) {
                ++left;
            }
            while (right > lo && !less(items[right], items[pivot])) {
                --right;
            }
            if (left < right) {
                T tmp = items[left];
                items[left] = items[right];
                items[right] = tmp;
            }
        }
        T tmp = items[pivot];
        items[pivot] = items[right];
        items[right] = tmp;
        if (nth == right) {
            return;
        }
        if (nth < right) {
            hi = right - 1u;
        } else {
            lo = right + 1u;
        }
    }
}

/// Ordena los `n` primeros elementos (los `n` menores, ya ordenados) dejando el resto
/// sin garantía de orden. Combina `nth_element` y un quicksort de la primera parte.
template <typename T, typename Less>
constexpr void partial_sort(Span<T> items, usize n, Less less) {
    const usize size = items.size();
    if (n >= size) {
        quick_sort(items, less);
        return;
    }
    if (n == 0u) {
        return;
    }
    nth_element(items, n, less);
    quick_sort(items.first(n), less);
}

/// ¿Está la vista ordenada según `less`?
template <typename T, typename Less>
[[nodiscard]] constexpr bool is_sorted(Span<T> items, Less less) {
    for (usize i = 1u; i < items.size(); ++i) {
        if (less(items[i], items[i - 1u])) {
            return false;
        }
    }
    return true;
}

/// Orden **por conteo (LSD radix)** de claves `u16` en dos pasadas de 8 bits, estable
/// y sin heap: `scratch` (tamaño `items`) lo aporta el llamador. Devuelve `false` si
/// `scratch` es insuficiente. Usa 256 contadores `u32` en pila (1 KiB): para muchas
/// claves de 16 bits (ids, índices) suele batir al quicksort y no depende de la
/// distribución.
constexpr bool radix_sort_u16(Span<u16> items, Span<u16> scratch) {
    const usize n = items.size();
    if (n < 2u) {
        return true;
    }
    if (scratch.size() < n) {
        return false;
    }
    u32 count[256];
    for (int pass = 0; pass < 2; ++pass) {
        const u32 shift = static_cast<u32>(pass) * 8u;
        for (u32 b = 0u; b < 256u; ++b) {
            count[b] = 0u;
        }
        for (usize i = 0u; i < n; ++i) {
            ++count[(items[i] >> shift) & 0xffu];
        }
        u32 sum = 0u;
        for (u32 b = 0u; b < 256u; ++b) {
            const u32 c = count[b];
            count[b] = sum;
            sum += c;
        }
        for (usize i = 0u; i < n; ++i) {
            const u32 key = (items[i] >> shift) & 0xffu;
            scratch[count[key]++] = items[i];
        }
        for (usize i = 0u; i < n; ++i) {
            items[i] = scratch[i];
        }
    }
    return true;
}

} // namespace eng