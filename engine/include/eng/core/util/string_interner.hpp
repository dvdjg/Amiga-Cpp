#pragma once

/// \file string_interner.hpp
/// `eng::util::StringInterner<MaxStrings, A>`: **internado de cadenas**. Deduplica texto
/// por **contenido** y le asigna un id `u16` estable; los bytes se copian una sola vez en
/// un `Allocator` (arena), de modo que no se repite la misma cadena en RAM. Es útil para
/// nombres de assets/config construidos en runtime, etiquetas de animación o comandos.
///
/// El índice usa `HashMap<StringView, u16, MaxStrings>` (hash por contenido) y un array
/// `id -> StringView` para la vuelta. Como el asignador es bump, los bytes no se mueven:
/// las vistas siguen siendo válidas mientras el interner viva.
///
/// Uso:
///   eng::util::InlineAlloc<256> arena;
///   eng::util::StringInterner<16, eng::util::InlineAlloc<256>> names {arena};
///   const eng::u16 id = names.intern("enemy_idle");
///   const eng::util::StringView s = names.lookup(id);
///
/// Verificación: HOST-124.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/allocator.hpp>
#include <eng/core/util/hash_map.hpp>
#include <eng/core/util/string_view.hpp>

namespace eng::util {

template <eng::u16 MaxStrings, class A>
class StringInterner {
	static_assert(Allocator<A>, "StringInterner: A debe cumplir Allocator");

public:
	static constexpr eng::u16 invalid = 0xffffu;

	constexpr explicit StringInterner(A& alloc) noexcept : m_alloc(&alloc) {}

	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return MaxStrings; }
	[[nodiscard]] constexpr eng::u16 size() const noexcept { return m_count; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0u; }

	/// Interna `text` (no vacío): devuelve el id existente o crea uno nuevo copiando los
	/// bytes en el asignador. `invalid` si la cadena está vacía, no caben más o no hay
	/// bytes.
	[[nodiscard]] eng::u16 intern(StringView text) noexcept {
		if (text.empty()) {
			return invalid;
		}
		const eng::u16* found = m_ids.find(text);
		if (found != nullptr) {
			return *found;
		}
		if (m_count >= MaxStrings) {
			return invalid;
		}
		const eng::Span<eng::u8> bytes = m_alloc->allocate(text.size(), 1u);
		if (bytes.data() == nullptr || bytes.size() < text.size()) {
			return invalid;
		}
		for (eng::usize i = 0; i < text.size(); ++i) {
			bytes[i] = static_cast<eng::u8>(text.data()[i]);
		}
		const StringView stored {reinterpret_cast<const char*>(bytes.data()), text.size()};
		const eng::u16 id = m_count;
		m_by_id[id] = stored;
		if (m_ids.insert(stored, id) == nullptr) {
			return invalid;
		}
		++m_count;
		return id;
	}

	/// Texto asociado a `id`, o una vista vacía si no existe.
	[[nodiscard]] constexpr StringView lookup(eng::u16 id) const noexcept {
		return (id < MaxStrings && id < m_count) ? m_by_id[id] : StringView {};
	}

	/// Vuelve a empezar (el asignador conserva los bytes ya copiados; su vida la lleva el
	/// llamador).
	constexpr void clear() noexcept {
		m_ids.clear();
		m_count = 0u;
	}

private:
	A* m_alloc;
	eng::util::HashMap<StringView, eng::u16, MaxStrings> m_ids {};
	StringView m_by_id[MaxStrings] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::util
