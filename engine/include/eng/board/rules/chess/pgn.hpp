#pragma once

/// \file pgn.hpp
/// Escritor **PGN** (Portable Game Notation) mínimo, sin heap ni I/O: vuelca
/// cabeceras, jugadas SAN, comentarios y resultado en un `Span<char>` del llamador.
/// Sirve para exportar partidas del motor y analizarlas con herramientas estándar.
///
/// Formato de salida (una partida por buffer):
///
///   [Event "..."]
///   [White "..."]
///   ...
///
///   1. e4 {comentario} e5 2. Nf3 ... 1-0
///
/// Cada escritura es segura ante buffers pequeños: la longitud lógica sigue
/// contando, pero `ok()` pasa a `false` si algo no cupo.
///
/// Verificación: HOST-160.

#include <eng/board/rules/chess/board.hpp>
#include <eng/core/span.hpp>
#include <eng/core/util/string_view.hpp>

namespace eng::board::chess {

class PgnWriter {
public:
	explicit PgnWriter(eng::Span<char> out) noexcept : m_out(out) {}

	void tag(const char* name, const char* value) noexcept {
		tag(name, eng::util::StringView {value});
	}

	void tag(const char* name, eng::util::StringView value) noexcept {
		put_char('[');
		put_text(name);
		put_text(" \"");
		for (eng::usize i = 0u; i < value.size(); ++i) {
			const char c = value[i];
			if (c == '"' || c == '\\') {
				put_char('\\');
			}
			put_char(c);
		}
		put_text("\"]\n");
	}

	/// Línea en blanco que separa cabeceras de jugadas.
	void end_tags() noexcept { put_char('\n'); }

	/// Jugada en SAN. El número de jugada solo se escribe cuando mueven las blancas.
	void move(eng::u32 fullmove, Color side, eng::util::StringView san) noexcept {
		need_separator();
		if (side == Color::White) {
			put_uint(fullmove);
			put_text(". ");
		}
		put_text(san);
	}

	/// Comentario PGN `{ ... }` (se coloca tras la jugada).
	void comment(eng::util::StringView text) noexcept {
		need_separator();
		put_char('{');
		put_text(text);
		put_char('}');
	}

	/// Resultado final (`1-0`, `0-1`, `1/2-1/2`, `*`) y salto de línea.
	void result(eng::util::StringView r) noexcept {
		need_separator();
		put_text(r);
		put_char('\n');
	}

	[[nodiscard]] eng::usize length() const noexcept { return m_len; }
	[[nodiscard]] bool ok() const noexcept { return !m_truncated; }

private:
	void put_char(char c) noexcept {
		if (m_len < m_out.size()) {
			m_out[m_len] = c;
		} else {
			m_truncated = true;
		}
		++m_len;
		m_last = c;
	}

	void put_text(eng::util::StringView text) noexcept {
		for (eng::usize i = 0u; i < text.size(); ++i) {
			put_char(text[i]);
		}
	}

	void put_uint(eng::u32 value) noexcept {
		char digits[10];
		eng::usize n = 0u;
		if (value == 0u) {
			put_char('0');
			return;
		}
		while (value > 0u && n < sizeof(digits)) {
			digits[n++] = static_cast<char>('0' + (value % 10u));
			value /= 10u;
		}
		while (n > 0u) {
			--n;
			put_char(digits[n]);
		}
	}

	void need_separator() noexcept {
		if (m_len != 0u && m_last != '\n' && m_last != ' ') {
			put_char(' ');
		}
	}

	eng::Span<char> m_out {};
	eng::usize m_len = 0u;
	char m_last = '\n';
	bool m_truncated = false;
};

} // namespace eng::board::chess
