#pragma once

/// \file vfs.hpp
/// **Fachada de sistema de archivos del VFS** (`ROADMAP_RESOURCES.md` R6.1): resuelve **paths
/// normalizados** (`normalize_path`: colapsa separadores, `.`/`..`, sin escapar de la raíz) y expone
/// operaciones de **alto nivel** (`exists`/`size`/`read`/`read_all`) sobre un **backend** que aporta
/// el SO (`dos.library`/`trackdisk`) o, en tests, un backend **simulado en memoria**.
///
/// El backend es un **concepto** (plantilla, no `void*`): debe ofrecer `exists`/`size`/`read` con
/// `const char*` (path ya normalizado). Errores **normalizados** (`VfsError`) y `read_all` para
/// pantallas de carga (lectura completa, sin obligar a bloquear el frame principal). Es la puerta
/// que el loader de assets/librerías usará; los requests asíncronos con generación son R6.2.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/os/path.hpp>

namespace eng::os {

/// Error normalizado del VFS.
enum class VfsError : eng::u8 {
	InvalidPath,
	NotFound,
	Permission,
	Busy,
	Io,
	Unsupported,
	Cancelled,
	OutOfMemory,
	Corrupt,
};

/// Tamaño máximo de un path normalizado (búfer de trabajo del VFS).
inline constexpr eng::usize kVfsPathMax = eng::u16(kMaxPathComponents) * 4u;

/// **Fachada VFS** sobre un `Backend` (concepto). Ver doc del fichero.
template <class Backend>
class Vfs {
public:
	constexpr explicit Vfs(Backend& backend) noexcept : m_backend(backend) {}

	/// Normaliza `path` en `m_scratch` (terminado en `\0`). `false` si es inválido.
	[[nodiscard]] bool normalize(eng::util::StringView path) noexcept {
		const auto r = normalize_path(path, eng::Span<char> {m_scratch, kVfsPathMax - 1u});
		if (!r.has_value()) {
			m_path_ok = false;
			return false;
		}
		m_scratch[*r] = '\0';
		m_path_ok = true;
		return true;
	}

	/// ¿Existe el recurso? (path normalizado; `false` si el path es inválido).
	[[nodiscard]] bool exists(eng::util::StringView path) noexcept {
		return normalize(path) && m_backend.exists(m_scratch);
	}

	/// Tamaño en bytes (`0` si no existe o el path es inválido).
	[[nodiscard]] eng::u32 size(eng::util::StringView path) noexcept {
		return normalize(path) ? m_backend.size(m_scratch) : 0u;
	}

	/// Lee `dst.size()` bytes desde `offset`. Devuelve los bytes leídos, o `-1`.
	[[nodiscard]] eng::s32 read(eng::util::StringView path, eng::Span<eng::u8> dst,
				    eng::u32 offset = 0u) noexcept {
		if (!normalize(path)) {
			return -1;
		}
		return m_backend.read(m_scratch, dst, offset);
	}

	/// **Lectura completa** (para load screens): reserva implícita en `dst`; exige que `dst` quepa
	/// todo el recurso. Devuelve los bytes o el error normalizado.
	[[nodiscard]] eng::util::Expected<eng::u32, VfsError>
	read_all(eng::util::StringView path, eng::Span<eng::u8> dst) noexcept {
		if (!normalize(path)) {
			return eng::util::unexpected(VfsError::InvalidPath);
		}
		const eng::u32 n = m_backend.size(m_scratch);
		if (n == 0u) {
			return eng::util::unexpected(VfsError::NotFound);
		}
		if (dst.size() < n) {
			return eng::util::unexpected(VfsError::OutOfMemory);
		}
		const eng::s32 got = m_backend.read(m_scratch, dst.subspan(0u, n), 0u);
		if (got < 0 || static_cast<eng::u32>(got) != n) {
			return eng::util::unexpected(VfsError::Io);
		}
		return n;
	}

	/// El path de la última llamada normalizada (para depuración).
	[[nodiscard]] const char* last_path() const noexcept { return m_path_ok ? m_scratch : ""; }

private:
	Backend& m_backend;
	char m_scratch[kVfsPathMax] {};
	bool m_path_ok = false;
};

} // namespace eng::os
