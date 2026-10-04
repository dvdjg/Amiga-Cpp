#pragma once

/// \file vfs.hpp
/// **Fachada de sistema de archivos del VFS** (`ROADMAP_RESOURCES.md` R6.1): resuelve **paths
/// normalizados** (`normalize_path`: colapsa separadores, `.`/`..`, sin escapar de la raíz) y expone
/// operaciones de **alto nivel** (`exists`/`size`/`read`/`read_all`/`list`/`open`) sobre un
/// **backend** que aporta el SO (`dos.library`/`trackdisk`) o, en tests, un backend **simulado en
/// memoria**.
///
/// El backend es un **concepto** (plantilla, no `void*`): debe ofrecer `exists`/`size`/`read` con
/// `const char*` (path ya normalizado). Opcionalmente, `list` (enumeración) y
/// `open_file`/`read_file`/`close_file` (handles), que se detectan con `requires`.
///
/// La `Vfs` aporta además **montajes** (prefijo lógico → raíz del backend) y una **raíz** por
/// defecto, de modo que el juego usa paths **lógicos** ("data/code/x") sin codificar el volumen.
/// Errores **normalizados** (`VfsError`). Es la puerta que el loader de assets/librerías usa; los
/// requests asíncronos con generación son R6.2.
///
/// Los `static_cast` a `u8`/`u32` son de **frontera**: longitudes acotadas por los propios límites
/// del VFS (`kMountPrefixMax`, `kVfsPathMax`) y resultados de E/S de ancho **fijo** (`s32`/`u32`),
/// mientras `eng::usize` es el tamaño de la plataforma (`__SIZE_TYPE__`).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/core/util/noncopyable.hpp>
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

/// **Entrada de directorio** que reporta la enumeración (`Vfs::list`).
struct DirEntry {
	const char* name = nullptr; ///< nombre del hijo (sin el directorio padre)
	eng::u32 size = 0u;         ///< tamaño en bytes (`0` para directorios)
	bool is_dir = false;
};

// --- utilidades de cadena internas (sin heap, acotadas) -----------------------------------
namespace detail {
[[nodiscard]] constexpr eng::usize str_len(const char* s) noexcept {
	eng::usize n = 0u;
	while (s[n] != '\0') {
		++n;
	}
	return n;
}
/// Copia `src` (hasta `cap - 1`) en `dst` + `'\0'`. `false` si no cabe.
[[nodiscard]] inline bool copy_str(eng::util::StringView src, char* dst, eng::usize cap) noexcept {
	if (src.size() + 1u > cap) {
		return false;
	}
	for (eng::usize i = 0u; i < src.size(); ++i) {
		dst[i] = src[i];
	}
	dst[src.size()] = '\0';
	return true;
}
/// `path` empieza por el componente `prefix` seguido de `'/'` o fin.
[[nodiscard]] constexpr bool starts_component(const char* path, const char* prefix,
					      eng::u8 prefix_len) noexcept {
	for (eng::u8 i = 0u; i < prefix_len; ++i) {
		if (path[i] != prefix[i]) {
			return false;
		}
	}
	return path[prefix_len] == '\0' || path[prefix_len] == '/';
}
} // namespace detail

/// **Fachada VFS** sobre un `Backend` (concepto). Ver doc del fichero.
template <class Backend>
class Vfs {
public:
	static constexpr eng::usize kMountMax = 8u;
	static constexpr eng::usize kMountPrefixMax = 24u;

	constexpr explicit Vfs(Backend& backend) noexcept : m_backend(backend) {}

	/// Fija la **raíz** del backend para paths relativos (p. ej. `"DH1:game"`). Vacío = sin raíz.
	void set_root(eng::util::StringView root) noexcept {
		(void)detail::copy_str(root, m_root, sizeof(m_root));
	}

	/// **Monta** un prefijo lógico en una raíz del backend (p. ej. `"data"` → `"DH1:game/data"`).
	/// Usa **el prefijo más largo** que encaje. `false` si no cabe o no hay hueco.
	[[nodiscard]] bool mount(eng::util::StringView prefix, eng::util::StringView root) noexcept {
		if (m_mount_count >= kMountMax || prefix.empty()) {
			return false;
		}
		Mount& m = m_mounts[m_mount_count];
		if (!detail::copy_str(prefix, m.prefix, sizeof(m.prefix))) {
			return false;
		}
		if (!detail::copy_str(root, m.root, sizeof(m.root))) {
			return false;
		}
		m.prefix_len = static_cast<eng::u8>(detail::str_len(m.prefix));
		++m_mount_count;
		return true;
	}
	/// Desmonta el prefijo (primer match exacto). `false` si no estaba.
	[[nodiscard]] bool umount(eng::util::StringView prefix) noexcept {
		for (eng::usize i = 0u; i < m_mount_count; ++i) {
			if (prefix.size() == m_mounts[i].prefix_len &&
			    detail::starts_component(m_mounts[i].prefix, prefix.data(),
						     static_cast<eng::u8>(prefix.size()))) {
				for (eng::usize j = i + 1u; j < m_mount_count; ++j) {
					m_mounts[j - 1u] = m_mounts[j];
				}
				--m_mount_count;
				return true;
			}
		}
		return false;
	}
	[[nodiscard]] eng::usize mount_count() const noexcept { return m_mount_count; }

	/// Normaliza `path` **y lo resuelve** (montaje/raíz) en el búfer de trabajo. `false` si es
	/// inválido. Todo el resto de operaciones pasa por aquí.
	[[nodiscard]] bool normalize(eng::util::StringView path) noexcept {
		const auto r = normalize_path(path, eng::Span<char> {m_norm, kVfsPathMax - 1u});
		if (!r.has_value()) {
			m_path_ok = false;
			return false;
		}
		m_norm[*r] = '\0';
		return resolve();
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

	/// **Enumeración de directorio**: llama `fn(const DirEntry&)` por cada hijo. `false` si el
	/// backend no soporta `list` o el path es inválido. `fn` puede devolver `bool` (`false` = parar).
	template <class Fn>
	[[nodiscard]] bool list(eng::util::StringView path, Fn&& fn) noexcept {
		if (!normalize(path)) {
			return false;
		}
		if constexpr (requires(Backend& b, const char* p, Fn&& f) { b.list(p, f); }) {
			return m_backend.list(m_scratch, fn);
		} else {
			(void)m_scratch;
			(void)fn;
			return false;
		}
	}

	/// **Handle propietario** de un fichero (`VfsFile`): abre una vez y lee por offset sin re-abrir,
	/// cerrando el handle en su destructor (RAII). Guarda el **backend por referencia** (no un
	/// observador; `VfsFile` es un objeto de corta vida ligado a un `Vfs` vivo).
	class VfsFile : public eng::util::NonMovable {
	public:
		VfsFile(Backend& backend, eng::u32 handle) noexcept
			: m_backend(backend), m_handle(handle) {}
		~VfsFile() { close(); }

		[[nodiscard]] bool valid() const noexcept { return m_handle != 0u; }
		[[nodiscard]] eng::u32 handle() const noexcept { return m_handle; }

		/// Lee `dst.size()` bytes desde `offset` (handle ya abierto).
		[[nodiscard]] eng::s32 read(eng::Span<eng::u8> dst, eng::u32 offset = 0u) noexcept {
			if (!valid()) {
				return -1;
			}
			return m_backend.read_file(m_handle, dst, offset);
		}
		/// Cierra el handle (idempotente).
		void close() noexcept {
			if (valid()) {
				m_backend.close_file(m_handle);
			}
			m_handle = 0u;
		}

	private:
		Backend& m_backend;
		eng::u32 m_handle = 0u;
	};

	/// **Abre** un fichero como handle propietario (RAII). Inválido si el backend no lo soporta
	/// (`open_file`/`read_file`/`close_file`) o el path es inválido.
	[[nodiscard]] VfsFile open(eng::util::StringView path) noexcept {
		if constexpr (requires(Backend& b, const char* p, eng::u32 h, eng::Span<eng::u8> d) {
				      b.open_file(p);
				      b.read_file(h, d, 0u);
				      b.close_file(h);
			      }) {
			if (!normalize(path)) {
				return VfsFile {m_backend, 0u};
			}
			return VfsFile {m_backend, m_backend.open_file(m_scratch)};
		} else {
			(void)path;
			return VfsFile {m_backend, 0u};
		}
	}

	/// El path ya resuelto de la última llamada (backend), para depuración.
	[[nodiscard]] const char* last_path() const noexcept { return m_path_ok ? m_scratch : ""; }

private:
	struct Mount {
		char prefix[kMountPrefixMax] {};
		char root[kVfsPathMax] {};
		eng::u8 prefix_len = 0u;
	};

	/// Resuelve `m_norm` (lógico) en `m_scratch` (backend): montaje del prefijo más largo, o la
	/// raíz por defecto delante. Sin montajes ni raíz, `m_scratch == m_norm` (comportamiento plano).
	[[nodiscard]] bool resolve() noexcept {
		const char* pn = m_norm;
		if (*pn == '/') {
			++pn; // el path absoluto también se resuelve contra montaje/raíz
		}
		eng::usize best = kMountMax; // sentinel: ningún montaje
		for (eng::usize i = 0u; i < m_mount_count; ++i) {
			const Mount& m = m_mounts[i];
			if (m.prefix_len == 0u || !detail::starts_component(pn, m.prefix, m.prefix_len)) {
				continue;
			}
			if (best == kMountMax || m.prefix_len > m_mounts[best].prefix_len) {
				best = i;
			}
		}
		// **Sin montaje ni raíz**: el path normalizado tal cual (comportamiento plano).
		if (best == kMountMax && m_root[0] == '\0') {
			eng::usize n = 0u;
			while (m_norm[n] != '\0' && n + 1u < kVfsPathMax) {
				m_scratch[n] = m_norm[n];
				++n;
			}
			m_scratch[n] = '\0';
			m_path_ok = true;
			return true;
		}
		const Mount& bm = m_mounts[best == kMountMax ? 0u : best];
		const char* base = (best != kMountMax) ? bm.root : m_root;
		const char* rest = (best != kMountMax) ? (pn + bm.prefix_len) : pn;
		if (*rest == '/') {
			++rest;
		}
		const eng::usize bl = detail::str_len(base);
		const eng::usize rl = detail::str_len(rest);
		const bool sep = (bl != 0u && rl != 0u);
		if (bl + rl + (sep ? 1u : 0u) + 1u > kVfsPathMax) {
			m_path_ok = false;
			return false;
		}
		eng::usize at = 0u;
		for (eng::usize i = 0u; i < bl; ++i) {
			m_scratch[at++] = base[i];
		}
		if (sep) {
			m_scratch[at++] = '/';
		}
		for (eng::usize i = 0u; i < rl; ++i) {
			m_scratch[at++] = rest[i];
		}
		m_scratch[at] = '\0';
		m_path_ok = true;
		return true;
	}

	Backend& m_backend;
	char m_norm[kVfsPathMax] {};    ///< path lógico normalizado
	char m_scratch[kVfsPathMax] {}; ///< path resuelto al backend (lo que ve el backend)
	char m_root[kVfsPathMax] {};    ///< raíz por defecto (puede estar vacía)
	Mount m_mounts[kMountMax] {};
	eng::usize m_mount_count = 0u;
	bool m_path_ok = false;
};

} // namespace eng::os
