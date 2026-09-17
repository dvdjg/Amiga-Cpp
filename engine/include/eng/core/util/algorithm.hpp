#pragma once

/// \file algorithm.hpp
/// Algoritmos genéricos sobre `eng::Span` (`eng::util`), sin `<algorithm>`.
///
/// El engine recorre buffers constantemente (tiles, actores, intents, paletas,
/// muestras). Escribir el bucle a mano en cada sitio introduce errores de
/// límites y duplica lógica; esta cabecera concentra los recorridos habituales.
///
/// Contrato de coste, visible en la firma:
/// - Todos operan sobre memoria contigua ya existente (`Span`); **ninguno asigna**.
/// - No hay iteradores con estado: el índice es el puntero, así que en 68000 no se
///   paga ninguna abstracción respecto al `for` equivalente.
/// - `copy` y `copy_n` copian como mucho lo que cabe en el destino (no desbordan);
///   devuelven cuántos elementos escribieron.
///
/// Las funciones toman `Span<T>` (no `Span<const T>`): `eng::Span` no convierte de
/// mutable a const de forma implícita (se usa `as_const()`), y los algoritmos de
/// lectura funcionan igual sobre `Span<T>` porque solo leen.
///
/// El orden lo cubre `eng/core/sort.hpp` (`quick_sort`/`sort_items`); aquí no se
/// duplica.
///
/// Uso:
///   const eng::u16* p = eng::util::find(cells, tile_id);
///   eng::util::for_each(actors, [](Actor& a) { a.step(); });
///   const eng::usize kept = eng::util::remove_if(buf, pred).size();

#include <eng/core/span.hpp>
#include <eng/core/util/util.hpp>

namespace eng::util {

// --- Búsqueda ---------------------------------------------------------------

/// Primer elemento igual a `value`; devuelve `end()` si no está.
template <class T, class V>
[[nodiscard]] constexpr T* find(Span<T> items, const V& value) noexcept {
	for (usize i = 0; i < items.size(); ++i) {
		if (items[i] == value) {
			return items.data() + i;
		}
	}
	return items.data() + items.size();
}

/// Primer elemento que cumple `pred`; devuelve `end()` si no hay ninguno.
template <class T, class Pred>
[[nodiscard]] constexpr T* find_if(Span<T> items, Pred pred) noexcept {
	for (usize i = 0; i < items.size(); ++i) {
		if (pred(items[i])) {
			return items.data() + i;
		}
	}
	return items.data() + items.size();
}

/// ¿Está `value` en la vista?
template <class T, class V>
[[nodiscard]] constexpr bool contains(Span<T> items, const V& value) noexcept {
	return find(items, value) != items.data() + items.size();
}

/// Cuántos elementos son iguales a `value`.
template <class T, class V>
[[nodiscard]] constexpr usize count(Span<T> items, const V& value) noexcept {
	usize n = 0;
	for (const T& item : items) {
		if (item == value) {
			++n;
		}
	}
	return n;
}

/// Cuántos elementos cumplen `pred`.
template <class T, class Pred>
[[nodiscard]] constexpr usize count_if(Span<T> items, Pred pred) noexcept {
	usize n = 0;
	for (const T& item : items) {
		if (pred(item)) {
			++n;
		}
	}
	return n;
}

// --- Cuantificadores --------------------------------------------------------

/// ¿Todos cumplen `pred`? (vacío → `true`).
template <class T, class Pred>
[[nodiscard]] constexpr bool all_of(Span<T> items, Pred pred) noexcept {
	for (const T& item : items) {
		if (!pred(item)) {
			return false;
		}
	}
	return true;
}

/// ¿Alguno cumple `pred`?
template <class T, class Pred>
[[nodiscard]] constexpr bool any_of(Span<T> items, Pred pred) noexcept {
	for (const T& item : items) {
		if (pred(item)) {
			return true;
		}
	}
	return false;
}

/// ¿Ninguno cumple `pred`?
template <class T, class Pred>
[[nodiscard]] constexpr bool none_of(Span<T> items, Pred pred) noexcept {
	return !any_of(items, pred);
}

// --- Recorrido --------------------------------------------------------------

/// Aplica `fn` a cada elemento (muta la vista). Devuelve `fn`.
template <class T, class Fn>
constexpr Fn for_each(Span<T> items, Fn fn) {
	for (T& item : items) {
		fn(item);
	}
	return fn;
}

/// Transforma cada elemento in situ.
template <class T, class Fn>
constexpr void transform(Span<T> items, Fn fn) {
	for (T& item : items) {
		item = fn(item);
	}
}

// --- Copia y relleno --------------------------------------------------------

/// Copia `src` en `dst` (como mucho lo que cabe) y devuelve cuántos escribió.
/// Si `dst` es mayor, el resto queda intacto.
template <class T>
constexpr usize copy(Span<T> src, Span<T> dst) noexcept {
	const usize n = min(src.size(), dst.size());
	for (usize i = 0; i < n; ++i) {
		dst[i] = src[i];
	}
	return n;
}

/// Copia los primeros `n` elementos de `src` en `dst` (como mucho lo que cabe).
/// Devuelve cuántos escribió.
template <class T>
constexpr usize copy_n(Span<T> src, usize n, Span<T> dst) noexcept {
	const usize m = min(min(n, src.size()), dst.size());
	for (usize i = 0; i < m; ++i) {
		dst[i] = src[i];
	}
	return m;
}

/// Escribe `value` en los primeros `n` elementos (como mucho lo que quepa).
template <class T>
constexpr usize fill_n(Span<T> dst, usize n, const T& value) noexcept {
	const usize m = min(n, dst.size());
	for (usize i = 0; i < m; ++i) {
		dst[i] = value;
	}
	return m;
}

/// ¿Dos vistas tienen el mismo contenido? (mismo tamaño y elemento a elemento).
template <class T>
[[nodiscard]] constexpr bool equal(Span<T> a, Span<T> b) noexcept {
	if (a.size() != b.size()) {
		return false;
	}
	for (usize i = 0; i < a.size(); ++i) {
		if (!(a[i] == b[i])) {
			return false;
		}
	}
	return true;
}

// --- Reducción --------------------------------------------------------------

/// Suma acumulada partiendo de `init`.
template <class T, class Acc>
[[nodiscard]] constexpr Acc accumulate(Span<T> items, Acc init) noexcept {
	Acc acc = init;
	for (const T& item : items) {
		acc = acc + item;
	}
	return acc;
}

/// Reducción con operación explícita: `acc = op(acc, item)`.
template <class T, class Acc, class Op>
[[nodiscard]] constexpr Acc accumulate(Span<T> items, Acc init, Op op) {
	Acc acc = init;
	for (const T& item : items) {
		acc = op(acc, item);
	}
	return acc;
}

/// Puntero al menor elemento; `end()` si la vista está vacía.
template <class T>
[[nodiscard]] constexpr T* min_element(Span<T> items) noexcept {
	if (items.empty()) {
		return items.data();
	}
	T* best = items.data();
	for (usize i = 1; i < items.size(); ++i) {
		if (items[i] < *best) {
			best = items.data() + i;
		}
	}
	return best;
}

/// Puntero al mayor elemento; `end()` si la vista está vacía.
template <class T>
[[nodiscard]] constexpr T* max_element(Span<T> items) noexcept {
	if (items.empty()) {
		return items.data();
	}
	T* best = items.data();
	for (usize i = 1; i < items.size(); ++i) {
		if (*best < items[i]) {
			best = items.data() + i;
		}
	}
	return best;
}

// --- Búsqueda binaria (vista ordenada) --------------------------------------

/// Primer elemento no menor que `value` (cota inferior).
template <class T, class V>
[[nodiscard]] constexpr T* lower_bound(Span<T> items, const V& value) noexcept {
	usize lo = 0;
	usize hi = items.size();
	while (lo < hi) {
		const usize mid = lo + (hi - lo) / 2u;
		if (items[mid] < value) {
			lo = mid + 1u;
		} else {
			hi = mid;
		}
	}
	return items.data() + lo;
}

/// Primer elemento mayor que `value` (cota superior).
template <class T, class V>
[[nodiscard]] constexpr T* upper_bound(Span<T> items, const V& value) noexcept {
	usize lo = 0;
	usize hi = items.size();
	while (lo < hi) {
		const usize mid = lo + (hi - lo) / 2u;
		if (value < items[mid]) {
			hi = mid;
		} else {
			lo = mid + 1u;
		}
	}
	return items.data() + lo;
}

/// ¿`value` está en una vista ordenada?
template <class T, class V>
[[nodiscard]] constexpr bool binary_search(Span<T> items, const V& value) noexcept {
	T* p = lower_bound(items, value);
	return p != items.data() + items.size() && !(value < *p);
}

// --- Permutación ------------------------------------------------------------

/// Invierte el orden de los elementos in situ.
template <class T>
constexpr void reverse(Span<T> items) noexcept {
	usize i = 0;
	usize j = items.size();
	while (i < j) {
		--j;
		if (i >= j) {
			break;
		}
		swap(items[i], items[j]);
		++i;
	}
}

/// Rota los elementos a la izquierda de modo que `items[middle]` pasa al frente.
/// `middle` se interpreta módulo el tamaño. Se hace con tres inversiones (sin
/// memoria auxiliar y sin asignar).
template <class T>
constexpr void rotate(Span<T> items, usize middle) noexcept {
	if (items.empty()) {
		return;
	}
	const usize n = items.size();
	middle %= n;
	if (middle == 0u) {
		return;
	}
	reverse(items.first(middle));
	reverse(items.subspan(middle));
	reverse(items);
}

/// Escribe `value, value+1, ...` en la vista.
template <class T, class V>
constexpr void iota(Span<T> items, V value) {
	for (T& item : items) {
		item = static_cast<T>(value);
		++value;
	}
}

// --- Compactación -----------------------------------------------------------

/// Mueve los elementos que NO cumplen `pred` al frente (orden estable) y devuelve
/// la vista acotada a los conservados. El resto de la vista queda con residuos.
template <class T, class Pred>
constexpr Span<T> remove_if(Span<T> items, Pred pred) {
	usize w = 0;
	for (usize i = 0; i < items.size(); ++i) {
		if (!pred(items[i])) {
			if (w != i) {
				items[w] = move(items[i]);
			}
			++w;
		}
	}
	return items.first(w);
}

/// Elimina duplicados consecutivos (vista ordenada) y devuelve la vista acotada.
template <class T>
constexpr Span<T> unique(Span<T> items) {
	if (items.size() < 2u) {
		return items;
	}
	usize w = 1;
	for (usize i = 1; i < items.size(); ++i) {
		if (!(items[i] == items[w - 1u])) {
			if (w != i) {
				items[w] = move(items[i]);
			}
			++w;
		}
	}
	return items.first(w);
}

} // namespace eng::util
