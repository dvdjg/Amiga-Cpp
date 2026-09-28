#pragma once

/// \file object3d.hpp
/// Modelo de objeto/malla 3D del demoscene (`lib3d`) portado **1:1**: formato
/// empaquetado que genera `obj2c` y recorrido de vértices/aristas/caras con flags.
///
/// Fidelidad de layout (clave): los índices de los grupos son **offsets en bytes**
/// sobre `objdat`, y los structs conservan la disposición del original, porque el
/// efecto recorre los grupos con macros (`NODE3D(i) = objdat + i - 2`, etc.). Las
/// primitivas matemáticas (matrices 4.12, `normfx`, `div_wide`) vienen de `math2d`/
/// `math3d`, ya validadas por HOST-010/011.
///
/// Uso:
///   Object3D obj {};
///   new_object3d(obj, pilka);          // enlaza el mesh (sin alloc dinámica)
///   obj.rotate.x = obj.rotate.y = obj.rotate.z = eng::retro::turns(static_cast<eng::u16>(frame * 8));
///   update_object_transformation(obj);
///
/// **Por qué los structs siguen en `s16`.** `Point3D`/`Node3D`/`Edge`/`Face` son el
/// layout empaquetado del `objdat` (los grupos se indexan por offset de byte), así que
/// retiparlos a `Fixed` cambiaría el binario del mesh generado y rompería los macros del
/// original. La capa de cálculo que los consume **sí** es tipada y genérica:
/// `math3d::Affine3<>`, `Vec<3,eng::coord>` (`P3<>`), `math3d::load_rotate`/`scale` (con
/// el ángulo en radianes). La crudeza vive solo en el almacenamiento, no en la aritmética.

#include <eng/core/math/arith.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_q.hpp>

namespace eng::object3d {

using eng::s16;
using eng::s32;
using eng::s8;
using eng::u8;
using eng::u16;
using eng::math::div_wide;
using eng::retro::normfx;

/// Punto/vector **tipado** para el estado de runtime del `Object3D` (posiciones, escala,
/// camara). NO es el layout empaquetado del `objdat` (ese sigue en `Point3D` crudo porque
/// lo genera `obj2c` y se indexa por offset de byte). `S` fija escalar y exponente: el
/// compilador **rechaza mezclar** tipos distintos (no hace falta `static_cast`).
template <class S>
struct Point3S {
	S x {};
	S y {};
	S z {};
};

/// Posiciones en el mundo (escalar de coordenada `eng::coord`).
using Point3C = Point3S<eng::retro::q0>;
/// Escala/ratios normalizados (escalar `eng::real`). El caller puede instanciar
/// `Point3S<S>` con otro fixed (p. ej. `Fixed<s32,E>`); el compilador rechaza mezclas.
using Point3R = Point3S<eng::retro::q12>;

/// Ángulo de rotación en **vueltas** (el índice del original): cada eje es un
/// `eng::retro::Turns`. Mismo layout que antes (3×2 B). El álgebra 3D recibe `Angle`.
using Angle3 = Point3S<eng::retro::Turns>;

/// Punto/vector 3D (mismo layout que `Point3D`). Coordenada LONGITUD (`q0`): un entero
/// tipado de 16 bits, así el campo dice su escala y el layout sigue siendo 3×2 bytes.
struct Point3D {
	eng::retro::q0 x {};
	eng::retro::q0 y {};
	eng::retro::q0 z {};
};

/// Nodo de vértice: flags de visibilidad + punto original + punto transformado.
/// Layout exacto: `flags` (1 byte) + pad, `point` en offset 2, `vertex` en offset 8.
struct Node3D {
	s8 flags = 0;
	Point3D point {};
	Point3D vertex {};
};

/// Arista: flags (0/-1 o color) + 2 índices de vértice (offsets de byte).
struct Edge {
	s8 flags = 0;
	s8 pad = 0;
	s16 point[2] = {0, 0};
};

/// Par (vértice, arista) de una cara.
struct FaceIndex {
	s16 vertex = 0;
	s16 edge = 0;
};

/// Cara: normal (para culling/iluminación) + flags/material + nº de índices.
/// La normal es un RATIO (`q12`); los `FaceIndex` van a continuación (offset 10); se
/// accede vía `face_indices`.
struct Face {
	eng::retro::q12 normal[3] = {};
	s8 flags = 0;
	s8 material = 0;
	s16 count = 0;
};

/// **Offset de byte** dentro del blob `obj2c`. Tipo fuerte: un offset no es "un `s16` cualquiera".
struct ObjOffset {
	s16 bytes;
};
/// **Referencias fuertes** a las piezas del blob: el compilador impide mezclar un `VertexRef` con
/// un `EdgeRef`/`FaceRef`, y un offset no se confunde con un dato.
struct VertexRef {
	ObjOffset o;
};
struct EdgeRef {
	ObjOffset o;
};
struct FaceRef {
	ObjOffset o;
};

/// **Vista tipada del `objdat` empaquetado** (formato de `obj2c`). Es el **único** sitio donde se
/// convierte el blob de bytes a los structs del formato: aquí viven el `reinterpret_cast`
/// byte->struct y la aritmética de offsets, con el tamaño del blob a la vista. El layout es fijo
/// (lo lee `flatshade_asm.s` y el original lo indexa por offset de byte), así que el blob **sigue
/// siendo bytes** — lo que pasa a estar tipado es el **acceso**, no el almacenamiento. El resto del
/// engine y los efectos usan `MeshBlob::point/vertex/face/...`, nunca bytes crudos.
class MeshBlob {
public:
	constexpr MeshBlob() noexcept = default;
	constexpr MeshBlob(eng::u8* data, eng::u32 size) noexcept
		: m_bytes(eng::Span<eng::u8> {data, size}) {}
	/// Implícita desde una vista de bytes: los descriptores de mesh (`Mesh3D`) la usan tal cual.
	MeshBlob(eng::Span<eng::u8> bytes) noexcept : m_bytes(bytes) {}

	/// ¿Blob vacío (sin base o tamaño 0)?
	[[nodiscard]] constexpr bool empty() const noexcept { return m_bytes.empty(); }
	/// Base del blob (para validación/procedencia).
	[[nodiscard]] constexpr eng::u8* data() const noexcept { return m_bytes.data(); }
	/// Tamaño del blob en bytes (mismo tipo que `Span::size()`, `usize` — sin cast).
	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_bytes.size(); }

	/// Acceso tipado por offset de byte (como las macros del original). La conversión vive AQUÍ.
	[[nodiscard]] Node3D* node(s16 off) const noexcept {
		return reinterpret_cast<Node3D*>(m_bytes.data() + (off - 2));
	}
	[[nodiscard]] Point3D* point(s16 off) const noexcept {
		return reinterpret_cast<Point3D*>(m_bytes.data() + off);
	}
	[[nodiscard]] Point3D* vertex(s16 off) const noexcept {
		return reinterpret_cast<Point3D*>(m_bytes.data() + (off + 6));
	}
	[[nodiscard]] Edge* edge(s16 off) const noexcept {
		return reinterpret_cast<Edge*>(m_bytes.data() + off);
	}
	[[nodiscard]] Face* face(s16 off) const noexcept {
		return reinterpret_cast<Face*>(m_bytes.data() + off);
	}
	/// Índices (vértice, arista) de una cara: van tras el cuerpo fijo (`offset 10` de `Face`).
	/// Devuelve una **vista** (`Span<FaceIndex>`, `count` entradas) — los llamadores indexan
	/// `fi[k].vertex`/`fi[k].edge`, sin `reinterpret_cast<s16*>` intercalado.
	[[nodiscard]] static eng::Span<FaceIndex> face_indices(Face* f) noexcept {
		return eng::Span<FaceIndex> {
			reinterpret_cast<FaceIndex*>(reinterpret_cast<eng::u8*>(f) + 10),
			static_cast<eng::usize>(f->count)};
	}

	// --- Acceso por REFERENCIAS FUERTES (camino nuevo): devuelven `Ref<T>`, no `T*` ---
	[[nodiscard]] eng::Ref<Node3D> node(VertexRef v) const noexcept { return node(v.o.bytes); }
	[[nodiscard]] eng::Ref<Point3D> point(VertexRef v) const noexcept { return point(v.o.bytes); }
	[[nodiscard]] eng::Ref<Point3D> vertex(VertexRef v) const noexcept { return vertex(v.o.bytes); }
	[[nodiscard]] eng::Ref<Edge> edge(EdgeRef v) const noexcept { return edge(v.o.bytes); }
	[[nodiscard]] eng::Ref<Face> face(FaceRef v) const noexcept { return face(v.o.bytes); }
	/// Índices de una cara por referencia fuerte (mismo `Span` que `face_indices`).
	[[nodiscard]] eng::Span<FaceIndex> indices(FaceRef v) const noexcept {
		return face_indices(face(v.o.bytes));
	}

	/// Resultado de **validar** los grupos de un descriptor `obj2c` contra este blob. `Ok` si el
	/// blob y los grupos son coherentes (offsets dentro de rango y alineados a palabra, caras
	/// con `count >= 0` y `FaceIndex` dentro del blob).
	enum class Status : eng::u8 { Ok, Empty, BadGroup, OutOfRange, Misaligned };

	/// **Valida** los grupos del descriptor contra este blob (la única validación; Fase D). Un
	/// asset corrupto devuelve un `Status` != `Ok` sin recorrer memoria fuera del blob.
	[[nodiscard]] Status check(eng::Span<s16> vertexGroups, eng::Span<s16> edgeGroups,
				   eng::Span<s16> faceGroups) const noexcept {
		if (empty()) {
			return Status::Empty;
		}
		const eng::u8* base = data();
		const eng::usize n = size();
		const eng::u8* end = base + n;
		auto valid_group = [&](eng::Span<s16> g) -> Status {
			for (s16 off : g) {
				if (off == 0) {
					continue;
				}
				if (off < 0 || static_cast<eng::usize>(off) >= n) {
					return Status::OutOfRange;
				}
				if ((off & 1) != 0) {
					return Status::Misaligned;
				}
			}
			return Status::Ok;
		};
		const Status sv = valid_group(vertexGroups);
		if (sv != Status::Ok) {
			return sv;
		}
		const Status se = valid_group(edgeGroups);
		if (se != Status::Ok) {
			return se;
		}
		const Status sf = valid_group(faceGroups);
		if (sf != Status::Ok) {
			return sf;
		}
		for (s16 off : faceGroups) {
			if (off == 0) {
				continue;
			}
			const Face* f = reinterpret_cast<const Face*>(base + off);
			if (f->count < 0) {
				return Status::BadGroup;
			}
			if (base + off + 10 + static_cast<eng::usize>(f->count) * 4u > end) {
				return Status::OutOfRange;
			}
			const FaceIndex* fi = reinterpret_cast<const FaceIndex*>(base + off + 10);
			for (s16 k = 0; k < f->count; ++k) {
				if (fi[k].vertex < 0 || static_cast<eng::usize>(fi[k].vertex) >= n) {
					return Status::OutOfRange;
				}
				if (fi[k].edge < 0 || static_cast<eng::usize>(fi[k].edge) >= n) {
					return Status::OutOfRange;
				}
			}
		}
		return Status::Ok;
	}

private:
	/// Vista sobre el blob: `Span` (data + tamaño) es el tipo del engine para "buffer + count"
	/// (CODING_STYLE §"Seguridad de tipos sobre punteros crudos"), en vez de `u8* + u32` sueltos.
	eng::Span<eng::u8> m_bytes {};
};

/// **Rango tipado de un grupo** de `obj2c` (offsets de byte terminados por 0): recorre saltando el
/// **centinela** y devuelve `Ref<T>` (nunca `T*`). `Acc` traduce un offset a la `Ref<T>` de la
/// pieza. Es la Fase B de `OBJECT3D_MESH_VIEW.md`: `for (VertexRef v : obj.points())` en vez de
/// `s16* group; while ((i = *group++))`.
template <class T, class Acc>
class GroupRange {
public:
	/// Iterador de entrada: **salta los separadores 0** entre sub-grupos (como el `while (*group++)`).
	class Iter {
	public:
		constexpr Iter(const s16* g, const s16* end, Acc acc) noexcept
			: m_g(g), m_end(end), m_acc(acc) {
			skip_zero();
		}
		[[nodiscard]] eng::Ref<T> operator*() const noexcept { return m_acc(*m_g); }
		/// Offset de byte de la entrada actual (para quien indexa por offset, p. ej.
		/// `object3d_poly` al mapear `FaceIndex.vertex`).
		[[nodiscard]] constexpr s16 offset() const noexcept { return *m_g; }
		Iter& operator++() noexcept {
			++m_g;
			skip_zero();
			return *this;
		}
		[[nodiscard]] bool operator!=(const Iter& o) const noexcept { return m_g != o.m_g; }

	private:
		/// Salta los separadores (`0`) entre sub-grupos.
		constexpr void skip_zero() noexcept {
			while (m_g != m_end && *m_g == 0) {
				++m_g;
			}
		}
		const s16* m_g;
		const s16* m_end;
		Acc m_acc;
	};

	constexpr GroupRange(eng::Span<s16> g, Acc acc) noexcept : m_g(g), m_acc(acc) {}
	/// Inicio del rango (primer offset, saltando los separadores iniciales).
	[[nodiscard]] Iter begin() const noexcept { return Iter {m_g.data(), end_ptr(), m_acc}; }
	/// Fin del rango (tras el último offset del grupo).
	[[nodiscard]] Iter end() const noexcept { return Iter {end_ptr(), end_ptr(), m_acc}; }

private:
	[[nodiscard]] const s16* end_ptr() const noexcept { return m_g.data() + m_g.size(); }
	eng::Span<s16> m_g;
	Acc m_acc;
};

/// **Accesores con nombre** del `MeshBlob` para los rangos de grupo (un offset -> la `Ref<T>`).
struct NodeAcc {
	MeshBlob b {};
	[[nodiscard]] eng::Ref<Node3D> operator()(s16 o) const noexcept {
		return b.node(VertexRef {ObjOffset {o}});
	}
};
struct EdgeAcc {
	MeshBlob b {};
	[[nodiscard]] eng::Ref<Edge> operator()(s16 o) const noexcept {
		return b.edge(EdgeRef {ObjOffset {o}});
	}
};
struct FaceAcc {
	MeshBlob b {};
	[[nodiscard]] eng::Ref<Face> operator()(s16 o) const noexcept {
		return b.face(FaceRef {ObjOffset {o}});
	}
};

/// Cabecera de malla (la que genera `obj2c`; los punteros son offsets absolutos).
struct Mesh3D {
	s16 vertices = 0;
	s16 texcoords = 0;
	s16 edges = 0;
	s16 faces = 0;
	s16 materials = 0;
	/// Blob empaquetado de `obj2c` como vista **tipada** (`MeshBlob`): los grupos lo indexan
	/// por offset de byte y el acceso tipado encapsula la conversión. Sustituye al `Span<u8>`.
	MeshBlob bytes {};
	/// Grupos de índices (offsets de byte) terminados por 0, dentro de `bytes`.
	eng::Span<s16> vertexGroups {};
	eng::Span<s16> edgeGroups {};
	eng::Span<s16> faceGroups {};
	/// Lista de objetos (`#grupos, [vertex edge face]..., 0`): no es un grupo de índices,
	/// así que se queda cruda (y sin validar como grupo).
	s16* objects = nullptr;
};

// Declaraciones para los accesores de `Object3D` (se definen más abajo).
struct Object3D;
[[nodiscard]] inline eng::Span<eng::u8> object_bytes(const Object3D& object);
[[nodiscard]] inline MeshBlob::Status new_object3d_checked(Object3D& object, const Mesh3D& mesh);

// (Los accesores libres `node3d`/`point3d`/`vertex3d`/`edge3d`/`face3d` se han eliminado: eran
// wrappers de una línea sobre `MeshBlob`. Usa los métodos `MeshBlob::node/point/vertex/...`.)

/// Objeto 3D: mesh enlazado + estado de transformación + cámara en espacio objeto.
///
/// **Layout estable**: el asm (`flatshade_asm.s`) lee `objdat`@0, los grupos@4/8/12 y
/// `objectToWorld`@38..; `objdat_size` va **al final** para no mover esos offsets.
struct Object3D {
	// --- ABI de malla (privada) ---
	// Los punteros del `obj2c` los lee `flatshade_asm.s` en estos offsets (objdat@0,
	// vertexGroups@4, edgeGroups@8, faceGroups@12). Desde C++ el acceso es por
	// `mesh()`/`node()`/`points()`/… y por las funciones amigas `object_bytes` y
	// `new_object3d_checked`. Se mantienen como primeros miembros para no mover los offsets.
private:
	eng::u8* objdat = nullptr;
	s16* vertexGroups = nullptr;
	s16* edgeGroups = nullptr;
	s16* faceGroups = nullptr;
	s16* objects = nullptr;

public:
	Angle3 rotate {};    // ángulo en radianes (q12)
	Point3R scale {};    // escala (q12)
	Point3C translate {}; // posicion (q0)

	math3d::Affine3<> objectToWorld {}; // objeto -> mundo (RATIO 4.12 + LONGITUD)
	math3d::Affine3<> worldToObject {}; // mundo -> objeto

	Point3C camera {}; // posicion de camara en espacio objeto (q0)

private:
	eng::usize objdat_size = 0; // tamaño del blob (mismo tipo que `Span::size()`)

	/// Tamaños (nº de `s16`) de cada grupo. **No** los lee el asm: van al final del struct, tras
	/// los campos que sí usa (`Object3D::objdat`@0, grupos@4/8/12, `objectToWorld`@38). Permiten
	/// que los **rangos tipados** (`points()`/`edges()`/`faces()`) conozcan el fin del grupo.
	eng::u16 vertex_group_count = 0;
	eng::u16 edge_group_count = 0;
	eng::u16 face_group_count = 0;

	// Amigas que sí manejan la ABI cruda (la vista de bytes y el enlace del mesh).
	friend eng::Span<eng::u8> object_bytes(const Object3D&);
	friend MeshBlob::Status new_object3d_checked(Object3D&, const Mesh3D&);
	// Amiga que fija los offsets del bloque que lee `flatshade_asm.s` (el `offsetof` de
	// miembros privados necesita la amistad y un tipo ya completo).
	friend struct Object3dLayout;

public:
	/// Accesores tipados al blob empaquetado (evitan manejar el `Span` a mano en los
	/// efectos). Reenvían a `point3d`/`face`… con la vista del propio objeto.
	[[nodiscard]] Node3D* node(s16 i) { return MeshBlob {object_bytes(*this)}.node(i); }
	[[nodiscard]] Point3D* point(s16 i) { return MeshBlob {object_bytes(*this)}.point(i); }
	[[nodiscard]] Point3D* vertex(s16 i) { return MeshBlob {object_bytes(*this)}.vertex(i); }
	[[nodiscard]] Edge* edge(s16 i) { return MeshBlob {object_bytes(*this)}.edge(i); }
	[[nodiscard]] Face* face(s16 i) { return MeshBlob {object_bytes(*this)}.face(i); }

	/// Vista tipada del blob del objeto (la procedencia del acceso tipado).
	[[nodiscard]] MeshBlob mesh() const noexcept { return MeshBlob {object_bytes(*this)}; }

	/// **Recorrido tipado de los grupos** (Fase B): saltan el centinela y devuelven `Ref<T>`.
	/// `for (VertexRef v : obj.points())`, `for (FaceRef f : obj.faces())`, …
	[[nodiscard]] GroupRange<Node3D, NodeAcc> points() const noexcept {
		return {eng::Span<s16> {vertexGroups, vertex_group_count}, NodeAcc {mesh()}};
	}
	[[nodiscard]] GroupRange<Edge, EdgeAcc> edges() const noexcept {
		return {eng::Span<s16> {edgeGroups, edge_group_count}, EdgeAcc {mesh()}};
	}
	[[nodiscard]] GroupRange<Face, FaceAcc> faces() const noexcept {
		return {eng::Span<s16> {faceGroups, face_group_count}, FaceAcc {mesh()}};
	}
};

/// Verificación del **bloque de malla** que lee `flatshade_asm.s` (solo ABI de 32 bits m68k):
/// fija que `objdat`@0, los grupos@4/8/12 y `objectToWorld`@38 no se muevan. Es un struct
/// amigo de `Object3D` porque el `offsetof` de los miembros privados lo exige, y un tipo
/// ya completo.
struct Object3dLayout {
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 4
	// `Object3D` no es *standard-layout* (mezcla accesos para encapsular la malla), así que
	// `offsetof` es "conditionally-supported"; el `static_assert` es justo lo que fija que
	// g++ conserva el orden. Se silencia el aviso a propósito.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
	static_assert(__builtin_offsetof(Object3D, objdat) == 0 &&
			      __builtin_offsetof(Object3D, vertexGroups) == 4 &&
			      __builtin_offsetof(Object3D, edgeGroups) == 8 &&
			      __builtin_offsetof(Object3D, faceGroups) == 12 &&
			      __builtin_offsetof(Object3D, objectToWorld) == 38,
		      "Object3D: offsets leidos por flatshade_asm.s");
#pragma GCC diagnostic pop
#endif
};

// Invariante de layout: los tipos con escala (`q0`/`q12`) describen el `objdat` empaquetado
// de `obj2c` sin cambiar ni el tamano ni los offsets de campo.
static_assert(sizeof(Point3D) == 6, "objdat: Point3D = 3x s16 (LONGITUD)");
static_assert(sizeof(Node3D) == 14 && __builtin_offsetof(Node3D, point) == 2 &&
		      __builtin_offsetof(Node3D, vertex) == 8,
	      "objdat: Node3D flags+pad, point@2, vertex@8");
static_assert(sizeof(Edge) == 6 && sizeof(FaceIndex) == 4, "objdat: Edge 6 B, FaceIndex 4 B");
static_assert(sizeof(Face) == 10 && __builtin_offsetof(Face, count) == 8,
	      "objdat: Face normal(6)+flags+material+count@8");
static_assert(__builtin_offsetof(Mesh3D, materials) == 8, "objdat: cabecera 5x s16");

/// Diagnóstico: el descriptor de malla no cuadra con su blob. `illegal` en m68k.
[[noreturn]] inline void mesh_invalid() { __builtin_trap(); }

/// Enlaza el mesh al objeto validando primero (sin detener la CPU): devuelve el `Status` de
/// `MeshBlob::check` si el descriptor no cuadra. Úsalo cuando quieras gestionar el asset corrupto.
[[nodiscard]] inline MeshBlob::Status new_object3d_checked(Object3D& object, const Mesh3D& mesh) {
	const MeshBlob::Status status = mesh.bytes.check(mesh.vertexGroups, mesh.edgeGroups, mesh.faceGroups);
	if (status != MeshBlob::Status::Ok) {
		return status;
	}
	object.objdat = mesh.bytes.data();
	object.objdat_size = mesh.bytes.size();
	object.vertexGroups = mesh.vertexGroups.data();
	object.edgeGroups = mesh.edgeGroups.data();
	object.faceGroups = mesh.faceGroups.data();
	object.objects = mesh.objects;
	object.vertex_group_count = static_cast<eng::u16>(mesh.vertexGroups.size());
	object.edge_group_count = static_cast<eng::u16>(mesh.edgeGroups.size());
	object.face_group_count = static_cast<eng::u16>(mesh.faceGroups.size());
	object.scale = Point3R {eng::retro::q12 {1 << 12}, eng::retro::q12 {1 << 12}, eng::retro::q12 {1 << 12}};
	return MeshBlob::Status::Ok;
}

/// Enlaza el mesh al objeto (equivalente a `NewObject3D` sin reservar memoria: el
/// `Object3D` es del llamador). `scale` queda a 1.0 (4.12). Detiene la CPU si el
/// descriptor no valida (usa `new_object3d_checked` para gestionarlo sin trampa).
inline void new_object3d(Object3D& object, const Mesh3D& mesh) {
	if (new_object3d_checked(object, mesh) != MeshBlob::Status::Ok) {
		mesh_invalid();
	}
}

/// Vista de bytes del blob de un `Object3D` (mutable): la consume `MeshBlob`.
[[nodiscard]] inline eng::Span<eng::u8> object_bytes(const Object3D& object) {
	return eng::Span<eng::u8> {object.objdat, object.objdat_size};
}

/// Índices (vértice, arista) de una cara — acceso tipado del `MeshBlob` (sin bytes crudos).
[[nodiscard]] inline eng::Span<FaceIndex> face_indices(Face* face) {
	return MeshBlob::face_indices(face);
}

/// Actualiza `objectToWorld`/`worldToObject` y la cámara en espacio objeto.
/// Port 1:1 de `UpdateObjectTransformation` (lib3d).
inline void update_object_transformation(Object3D& object) {
	const Angle3& r = object.rotate;
	const Point3R& s = object.scale;
	const Point3C& t = object.translate;

	// `(sin, cos)` de los ejes UNA vez: la matriz directa y la inversa los comparten.
	const auto sc = math3d::sincos3(r.x, r.y, r.z);

	// objeto -> mundo: Rx * Ry * Rz * S * T
	{
		math3d::Affine3<>& a = object.objectToWorld;
		math3d::load_rotate_from_sincos(a.m, sc);
		math3d::scale(a.m, s.x, s.y, s.z);
		a.t = eng::math::Vec<3, eng::retro::q0> {{t.x, t.y, t.z}};
	}

	// mundo -> objeto: T * S * Rz * Ry * Rx
	//
	// OJO — port 1:1 del original: la parte LINEAL sí se invierte (`S⁻¹Rᵀ`, con
	// `load_reverse_rotate` y `1/s`), pero la traslación queda en `-T`, no en
	// `-S⁻¹Rᵀ·T`. NO es una inversa completa: `compose(objectToWorld, worldToObject)`
	// no da la identidad en la traslación (medido: `.t = (-9965,-800,-1924)` en un caso
	// rotación+escala+traslación). La cámara en espacio objeto de 079/116 depende de este
	// comportamiento y HOST-014 lo fija, así que NO se corrige aquí. Para una inversa
	// completa de una transformación rígida usa `math3d::inverse_rigid` (HOST-055).
	{
		math3d::Affine3<> m_scale {};
		m_scale.m = math3d::Mat3<>::identity();
		m_scale.t = eng::math::Vec<3, eng::retro::q0> {{-t.x, -t.y, -t.z}};
		// 1/s en 4.12: numerador 1.0 en 8.24 (`kOne8_24`) para que el cociente de
		// `div_wide` (16 bits) quede ya en 4.12 sin normalizar.
		m_scale.m.m[0][0] = eng::retro::q12 {div_wide(eng::retro::kOne8_24, s.x.v)};
		m_scale.m.m[1][1] = eng::retro::q12 {div_wide(eng::retro::kOne8_24, s.y.v)};
		m_scale.m.m[2][2] = eng::retro::q12 {div_wide(eng::retro::kOne8_24, s.z.v)};

		math3d::Mat3<> m_rotate = math3d::Mat3<>::identity();
		// `sincos(-a)` = `(-sin a, cos a)` (la tabla es impar/par exacta).
		const decltype(sc) scn {-sc.sinX, sc.cosX, -sc.sinY, sc.cosY, -sc.sinZ, sc.cosZ};
		math3d::load_reverse_rotate_from_sincos(m_rotate, scn);
		object.worldToObject = eng::math::compose(m_scale, math3d::Affine3<> {m_rotate, {}});
	}

	// cámara en espacio objeto (la cámara está en (0,0,0) del mundo)
	{
		const math3d::Affine3<>& M = object.worldToObject;
		const math3d::P3<> t = M.t;
		// `dot_fixed_row` devuelve ya un `q0` (rescale<0>().cast<s16>()): asignacion directa, sin
		// `q0{...}` ni `static_cast<s16>` (eran ruido, CODING_STYLE §233).
		object.camera.x = eng::math::dot_fixed_row<3>(M.m.row(0), t);
		object.camera.y = eng::math::dot_fixed_row<3>(M.m.row(1), t);
		object.camera.z = eng::math::dot_fixed_row<3>(M.m.row(2), t);
	}
}

/// Actualiza **solo** `objectToWorld` (la matriz directa), **sin** la inversa
/// `worldToObject` ni `camera`. Para efectos que unicamente proyectan vertices (p. ej.
/// `bobs3d`, que dibuja un BOB por vertice y no usa culling ni luz), ahorra el calculo
/// de `S⁻¹Rᵀ`, su `compose` y los 3 productos de la camara (~1/3 del
/// `UpdateObjectTransformation` original). La salida de `objectToWorld` es identica a la
/// de `update_object_transformation`.
inline void update_object_transformation_forward(Object3D& object) {
	const Angle3& r = object.rotate;
	const Point3R& s = object.scale;
	const Point3C& t = object.translate;
	math3d::Affine3<>& a = object.objectToWorld;
	math3d::load_rotate(a.m, r.x, r.y, r.z);
	// La mayoria de efectos (p. ej. bobs3d) usan escala 1.0 (4.12: 4096), asi que el
	// `scale` seria una identidad de 9 `muls.w`. Se salta cuando no aporta nada.
	if (s.x.v != (1 << 12) || s.y.v != (1 << 12) || s.z.v != (1 << 12)) {
		math3d::scale(a.m, s.x, s.y, s.z);
	}
	a.t = eng::math::Vec<3, eng::retro::q0> {{t.x, t.y, t.z}};
}

} // namespace eng::object3d
