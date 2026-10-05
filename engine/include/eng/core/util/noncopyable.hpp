#pragma once

/// \file noncopyable.hpp
/// **Bases de ciclo de vida** del engine (`eng::util`): marcan que un tipo **no se
/// copia** —o ni copia ni mueve— sin que cada clase repita el par de `= delete`.
///
/// Un recurso con identidad (un buffer de arena, un `Ref`, un handle de fichero, una
/// lista enlazada que guarda punteros a sus propios miembros) no debe copiarse: dos
/// copias apuntarían al mismo recurso y una lo liberaría por debajo de la otra. La
/// forma explícita es escribir en cada clase
///
///   X(const X&) = delete;
///   X& operator=(const X&) = delete;
///
/// El engine repite ese bloque en decenas de tipos; `Noncopyable` lo centraliza y
/// documenta la intención en la propia firma: `class X : public Noncopyable`.
///
/// Matices importantes:
/// - `Noncopyable` **solo** borra la copia. Un tipo que necesite moverse declara su
///   propio constructor/`operator=` de movimiento (el cuerpo del miembro y la base se
///   construye por defecto, no se mueve). Si no lo declara, tampoco se mueve: la base
///   no tiene movimiento implícito, así que el movimiento implícito del derivado queda
///   borrado (mismo comportamiento que `boost::noncopyable`).
/// - `NonMovable` borra además el movimiento: para tipos que guardan punteros a sus
///   propios datos (p. ej. una lista por índices que referencia sus arrays internos),
///   donde ni copiar ni mover es seguro.
///
/// Uso:
///   class Resource : public eng::util::Noncopyable {
///     ...
///   };

namespace eng::util {

/// Prohíbe la copia; permite que el derivado defina movimiento explícito.
struct Noncopyable {
	Noncopyable() = default;
	Noncopyable(const Noncopyable&) = delete;
	Noncopyable& operator=(const Noncopyable&) = delete;
};

/// Prohíbe copia y movimiento (para tipos con punteros a sus propios miembros).
struct NonMovable {
	NonMovable() = default;
	NonMovable(const NonMovable&) = delete;
	NonMovable& operator=(const NonMovable&) = delete;
	NonMovable(NonMovable&&) = delete;
	NonMovable& operator=(NonMovable&&) = delete;
};

} // namespace eng::util
