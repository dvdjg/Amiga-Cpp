#pragma once

/// \file version_strip.hpp
/// **Franja de versión BARATA** (norma del repo): fondo + nombre + etiqueta de versión se
/// dibujan **una sola vez**; el resto de frames cuesta CERO. La estimación de fps se actualiza
/// **como mucho 1 vez por segundo** y solo reescribe los caracteres que cambian; los mensajes
/// de depuración se escriben **a demanda** (cuando cambian), nunca por frame.
///
/// Sustituye a los `draw_version_strip(frame)` que rehacían fondo + textos + números cada
/// frame (~10-30k ciclos/frame): el coste por frame de este helper es ~2 lecturas y una
/// comparación; por segundo, la redibujada de los dígitos del fps (~1-2 caracteres).
///
/// Uso típico:
///   `m_strip.init({planos, ...}, "066 BOB SHOWCASE", {1, 0});`
///   `m_strip.message("FASE 2");`   // solo cuando el mensaje cambie
///   `m_strip.tick(f, eng::debug::prof_clock());`   // cada frame (barato)

#include <eng/core/data/utf8.hpp>
#include <eng/core/types/types.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/font5x7.hpp>

namespace eng::debug {

/// Ventana de dibujo de la franja sobre los planos del display (layout **planar**).
struct StripTarget {
	eng::u16* planes = nullptr;       ///< palabras del plano 0 (Chip)
	eng::u16 words_per_row = 20u;     ///< palabras por fila (320 px / 16)
	eng::u32 plane_stride_words = 0u; ///< palabras entre planos contiguos
	eng::u16 first_line = 240u;       ///< fila superior de la franja
	eng::u16 height = 16u;            ///< alto de la franja (norma: 16)
	eng::u8 planes_used = 4u;         ///< planos de la franja (4 bpp)
	eng::u8 bg = 4u;                  ///< color de fondo
	eng::u8 color_name = 1u;          ///< color del nombre
	eng::u8 color_version = 3u;       ///< color de la versión
	eng::u8 color_dynamic = 2u;       ///< color de fps/mensajes
};

/// Franja con dibujo estático + campos dinámicos por **diff de caracteres**.
class VersionStrip {
public:
	static constexpr eng::u32 kClockHz = 7093790u; ///< reloj PAL del contador de ciclos
	static constexpr eng::u8 kFpsCols = 6u;        ///< "FPS nn" (nn hasta 99)
	static constexpr eng::u8 kMsgCols = 32u;       ///< ancho del campo de mensajes
	static constexpr eng::s16 kMsgX = 140;         ///< x del campo de mensajes
	static constexpr eng::s16 kFpsX = 268;         ///< x del campo de fps

	/// Dibuja fondo + `name` + versión + placeholder del fps en el buffer dado (UNA vez).
	void init(const StripTarget& t, const char* name, Version v) noexcept {
		m_t[0] = t;
		m_buffers = 1u;
		draw_static(t, name, v);
	}

	/// Segundo buffer (display doble-buffer): mismo contenido estático; los parches
	/// posteriores (fps/mensajes) se aplican a **ambos**.
	void init_second(const StripTarget& t, const char* name, Version v) noexcept {
		m_t[1] = t;
		m_buffers = 2u;
		draw_static(t, name, v);
	}

	/// Mensaje de depuración (campo fijo, x = `kMsgX`, fila superior de la franja).
	/// Redibuja SOLO los caracteres que cambian; `""` borra el campo. Llamar a demanda
	/// (cuando el mensaje cambie), **nunca** por frame.
	void message(const char* text) noexcept { set_field(m_msg, text, kMsgCols, kMsgX, 1); }

	/// Estimación de fps: como mucho **1 actualización por segundo** (y solo caracteres
	/// cambiados). `cycle_now` = contador de ciclos del periférico (`prof_clock()`); en host
	/// o sin periférico, pasar valores sintéticos/0 (queda inactivo). Llamar por frame.
	void tick(eng::u32 frame_index, eng::u32 cycle_now) noexcept {
		if (cycle_now == 0u) {
			return; // sin contador (host): el campo queda en el placeholder
		}
		if (!m_fps_armed) {
			m_fps_armed = true;
			m_cycle_ref = cycle_now;
			m_frame_ref = frame_index;
			return;
		}
		const eng::u32 elapsed = cycle_now - m_cycle_ref;
		if (elapsed < kClockHz) {
			return; // aún no ha pasado 1 segundo
		}
		const eng::u32 frames = frame_index - m_frame_ref;
		// `frames * kClockHz` cabe en u32: la ventana es ~1 s (≤ ~600 frames a 7 MHz).
		const eng::u32 fps = (frames * kClockHz) / elapsed;
		m_cycle_ref = cycle_now;
		m_frame_ref = frame_index;
		char buf[kFpsCols + 1];
		buf[0] = 'F';
		buf[1] = 'P';
		buf[2] = 'S';
		buf[3] = ' ';
		if (fps > 99u) {
			buf[4] = '9';
			buf[5] = '9';
		} else {
			buf[4] = static_cast<char>('0' + (fps / 10u));
			buf[5] = static_cast<char>('0' + (fps % 10u));
		}
		buf[6] = '\0';
		set_field(m_fps, buf, kFpsCols, kFpsX, 9);
	}

private:
	// --- primitivas de dibujo (una vez / por carácter cambiado) -----------------

	/// Escribe el píxel `(x, y)` de la franja del buffer `buf` con el color `value` (índice
	/// 0..15): pone a 1/0 el bit de cada plano usado. Fuera de la franja, no toca nada.
	void put(eng::u8 buf, eng::s16 x, eng::s16 y, eng::u8 value) noexcept {
		const StripTarget& t = m_t[buf];
		if (t.planes == nullptr || x < 0 || y < static_cast<eng::s16>(t.first_line) ||
		    y >= static_cast<eng::s16>(t.first_line + t.height) ||
		    x >= static_cast<eng::s16>(t.words_per_row * 16u)) {
			return;
		}
		eng::u16* const row0 = t.planes + static_cast<eng::u32>(y - t.first_line) *
						       t.words_per_row;
		const eng::u16 word = static_cast<eng::u16>(x >> 4u);
		const eng::u16 bit = static_cast<eng::u16>(0x8000u >> (x & 15));
		for (eng::u8 p = 0u; p < t.planes_used; ++p) {
			eng::u16* const cell = row0 + static_cast<eng::u32>(p) * t.plane_stride_words +
					       word;
			if ((value & (1u << p)) != 0u) {
				*cell = static_cast<eng::u16>(*cell | bit);
			} else {
				*cell = static_cast<eng::u16>(*cell & static_cast<eng::u16>(~bit));
			}
		}
	}

	/// Rellena la franja entera del buffer `buf` con el color `value` (palabra completa por
	/// plano y fila: es la operación de `init`, nunca por frame).
	void fill(eng::u8 buf, eng::u8 value) noexcept {
		const StripTarget& t = m_t[buf];
		if (t.planes == nullptr) {
			return;
		}
		for (eng::u8 p = 0u; p < t.planes_used; ++p) {
			const eng::u16 word = (value & (1u << p)) != 0u ? 0xffffu : 0x0000u;
			for (eng::u16 r = 0u; r < t.height; ++r) {
				eng::u16* const row = t.planes +
						     static_cast<eng::u32>(p) * t.plane_stride_words +
						     static_cast<eng::u32>(r) * t.words_per_row;
				for (eng::u16 i = 0u; i < t.words_per_row; ++i) {
					row[i] = word;
				}
			}
		}
	}

	/// Celda de un carácter: borra 5x7 con el fondo y dibuja los bits del glifo.
	void glyph(eng::u8 buf, eng::s16 x, eng::s16 y, eng::u32 cp, eng::u8 color) noexcept {
		for (eng::u8 r = 0u; r < eng::Font5x7::kRows; ++r) {
			const eng::u8 bits = cp >= 32u ? eng::Font5x7::row(static_cast<eng::u16>(cp), r)
						       : 0u;
			for (eng::u8 c = 0u; c < 5u; ++c) {
				const bool on = (bits & (0x10u >> c)) != 0u;
				put(buf, static_cast<eng::s16>(x + c), static_cast<eng::s16>(y + r),
				    on ? color : m_t[buf].bg);
			}
		}
	}

	/// Escribe un texto UTF-8 con la fuente 5x7 del engine (avance 5 px) en la fila `y` del
	/// buffer `buf`, borrando el fondo de cada celda de carácter.
	void text(eng::u8 buf, eng::s16 x, eng::s16 y, const char* s, eng::u8 color) noexcept {
		const eng::u8* p = reinterpret_cast<const eng::u8*>(s);
		for (;;) {
			const eng::u32 cp = eng::utf8::decode(p);
			if (cp == 0u) {
				break;
			}
			glyph(buf, x, y, cp, color);
			x = static_cast<eng::s16>(x + 5);
		}
	}

	/// Dibuja el contenido **estático** del buffer recién añadido (`m_buffers − 1`): fondo,
	/// nombre, etiqueta de versión y placeholder del fps. Solo se llama desde `init`/`init_second`.
	void draw_static(const StripTarget& t, const char* name, Version v) noexcept {
		const eng::u8 buf = (m_buffers > 0u) ? static_cast<eng::u8>(m_buffers - 1u) : 0u;
		(void)t;
		fill(buf, m_t[buf].bg);
		text(buf, 4, static_cast<eng::s16>(m_t[buf].first_line + 1), name,
		     m_t[buf].color_name);
		char label[16];
		(void)version_label(v, label, static_cast<eng::u8>(sizeof(label)));
		text(buf, 4, static_cast<eng::s16>(m_t[buf].first_line + 9), label,
		     m_t[buf].color_version);
		text(buf, kFpsX, static_cast<eng::s16>(m_t[buf].first_line + 9), "FPS --",
		     m_t[buf].color_dynamic);
	}

	/// Campo dinámico por diff: solo redibuja los caracteres distintos del anterior (y el
	/// sobrante si el texto se acorta).
	void set_field(char* store, const char* text, eng::u8 cols, eng::s16 x,
		       eng::s16 y_offset) noexcept {
		eng::u8 n = 0u;
		while (n < cols && text[n] != '\0') {
			++n;
		}
		eng::u8 prev = 0u;
		while (prev < cols && store[prev] != '\0') {
			++prev;
		}
		const eng::u8 upto = n > prev ? n : prev;
		for (eng::u8 i = 0u; i < upto; ++i) {
			const char want = i < n ? text[i] : '\0';
			const char have = i < prev ? store[i] : '\0';
			if (want == have) {
				continue;
			}
			for (eng::u8 b = 0u; b < m_buffers; ++b) {
				const eng::s16 gx = static_cast<eng::s16>(x + i * 5);
				const eng::s16 gy = static_cast<eng::s16>(m_t[b].first_line + y_offset);
				if (want == '\0') {
					// borrar la celda completa (5x7 con fondo)
					glyph(b, gx, gy, 0u, m_t[b].color_dynamic);
				} else {
					glyph(b, gx, gy, static_cast<eng::u32>(static_cast<eng::u8>(want)),
					      m_t[b].color_dynamic);
				}
			}
		}
		for (eng::u8 i = 0u; i < n; ++i) {
			store[i] = text[i];
		}
		store[n] = '\0';
	}

	StripTarget m_t[2] {};
	eng::u8 m_buffers = 0u;
	char m_msg[kMsgCols + 1] {};
	char m_fps[kFpsCols + 1] {};
	eng::u32 m_cycle_ref = 0u;
	eng::u32 m_frame_ref = 0u;
	bool m_fps_armed = false;
};

} // namespace eng::debug
