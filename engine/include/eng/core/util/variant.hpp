#pragma once

/// \file variant.hpp
/// `eng::util::Variant<Ts...>`: **unión etiquetada** de capacidad fija, sin heap ni
/// placement new. Guarda **una** de las alternativas (todas **trivialmente copiables**)
/// y un índice; `index`/`holds`/`get` la consultan y `visit(f)` aplica `f` a la activa
/// (el visitante suele distinguir el tipo con `if constexpr`). Sirve para colas de
/// comandos/mensajes heterogéneos, donde un `std::variant` traería heap y RTTI.
///
/// Uso:
///   struct Move { eng::s16 dx, dy; };
///   struct Wait { eng::u16 ticks; };
///   using Command = eng::util::Variant<Move, Wait>;
///   Command c { Move {2, 0} };
///   c.visit([&](const auto& cmd) {
///       using T = eng::util::remove_cvref_t<decltype(cmd)>;
///       if constexpr (eng::util::is_same_v<T, Move>) { ... }
///       else { ... }
///   });
///
/// Verificación: HOST-130.

#include <eng/core/types.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

namespace detail {

template <eng::usize I, class... Ts>
struct NthType;
template <class T, class... Rest>
struct NthType<0u, T, Rest...> {
	using type = T;
};
template <eng::usize I, class T, class... Rest>
struct NthType<I, T, Rest...> {
	using type = typename NthType<I - 1u, Rest...>::type;
};

template <class T, class... Ts>
struct IndexOf;
template <class T, class... Rest>
struct IndexOf<T, T, Rest...> {
	static constexpr eng::u8 value = 0u;
};
template <class T, class U, class... Rest>
struct IndexOf<T, U, Rest...> {
	static constexpr eng::u8 value = static_cast<eng::u8>(1u + IndexOf<T, Rest...>::value);
};

template <class... Ts>
[[nodiscard]] constexpr eng::usize variant_max_size() noexcept {
	eng::usize m = 0u;
	((m = sizeof(Ts) > m ? sizeof(Ts) : m), ...);
	return m;
}
template <class... Ts>
[[nodiscard]] constexpr eng::usize variant_max_align() noexcept {
	eng::usize m = 1u;
	((m = alignof(Ts) > m ? alignof(Ts) : m), ...);
	return m;
}

} // namespace detail

template <class... Ts>
class Variant {
	static_assert(sizeof...(Ts) > 0u, "Variant: necesita al menos una alternativa");
	static_assert((is_trivially_copyable_v<Ts> && ...),
		      "Variant: las alternativas deben ser trivialmente copiables (sin heap)");

public:
	static constexpr eng::u8 no_index = 0xffu;
	static constexpr eng::usize count = sizeof...(Ts);

	/// Arranca con la primera alternativa construida por defecto.
	constexpr Variant() noexcept {
		typename detail::NthType<0u, Ts...>::type first {};
		emplace(first);
	}

	/// Construye desde una de las alternativas.
	template <class T, enable_if_t<!is_same_v<remove_cvref_t<T>, Variant>, int> = 0>
	constexpr Variant(const T& value) noexcept {
		emplace(value);
	}

	[[nodiscard]] constexpr eng::u8 index() const noexcept { return m_index; }

	template <class T>
	[[nodiscard]] constexpr bool holds() const noexcept {
		return m_index == detail::IndexOf<T, Ts...>::value;
	}

	template <class T>
	[[nodiscard]] constexpr T& get() noexcept {
		return *reinterpret_cast<T*>(m_bytes);
	}
	template <class T>
	[[nodiscard]] constexpr const T& get() const noexcept {
		return *reinterpret_cast<const T*>(m_bytes);
	}

	template <class T>
	constexpr void emplace(const T& value) noexcept {
		static_assert(detail::IndexOf<T, Ts...>::value < no_index,
			      "Variant::emplace: el tipo no es una alternativa");
		m_index = detail::IndexOf<T, Ts...>::value;
		*reinterpret_cast<T*>(m_bytes) = value;
	}

	/// Aplica `f` a la alternativa activa. El visitante debe devolver lo mismo (o `void`)
	/// para todas las alternativas.
	template <class F>
	constexpr decltype(auto) visit(F&& f) {
		return visit_impl<0u>(f);
	}
	template <class F>
	[[nodiscard]] constexpr decltype(auto) visit(F&& f) const {
		return visit_impl<0u>(f);
	}

private:
	template <eng::usize I, class F>
	constexpr decltype(auto) visit_impl(F&& f) {
		if constexpr (I < count) {
			if (m_index == I) {
				return static_cast<F&&>(f)(
					get<typename detail::NthType<I, Ts...>::type>());
			} else {
				return visit_impl<I + 1u>(f);
			}
		} else {
			return static_cast<F&&>(f)(
				get<typename detail::NthType<0u, Ts...>::type>());
		}
	}
	template <eng::usize I, class F>
	[[nodiscard]] constexpr decltype(auto) visit_impl(F&& f) const {
		if constexpr (I < count) {
			if (m_index == I) {
				return static_cast<F&&>(f)(
					get<typename detail::NthType<I, Ts...>::type>());
			} else {
				return visit_impl<I + 1u>(f);
			}
		} else {
			return static_cast<F&&>(f)(
				get<typename detail::NthType<0u, Ts...>::type>());
		}
	}

	alignas(detail::variant_max_align<Ts...>()) eng::u8 m_bytes[detail::variant_max_size<Ts...>()] {};
	eng::u8 m_index = 0u;
};

} // namespace eng::util
