#pragma once

/// \file task.hpp
/// **Tareas secuenciales sin corrutinas** (`eng::util`): el patrón «`switch` + estado en
/// un `struct`» que usan los juegos retro para scripting y secuencias. Una tarea es una
/// función que devuelve `TaskStatus`; si devuelve `Running` «espera» (se vuelve a llamar
/// el tick siguiente), `Success` avanza y `Failure` aborta. Sin heap ni pila propia: el
/// estado de cada paso vive en su propio objeto (una `Delay`, una animación, etc.).
///
/// Las corrutinas de C++20 son demasiado pesadas para el A500; este patrón da lo mismo
/// (esperar N ticks, encadenar pasos, abortar) con coste mínimo y determinista.
///
/// Uso:
///   eng::util::Delay espera {30};             // 30 ticks
///   eng::util::TaskSequence<4> seq;
///   seq.add(abrir_puerta);                     // pasos instantaneos o que esperan
///   seq.add(espera);                           // el Delay es el paso
///   seq.add(entrar);
///   // una vez por frame:
///   const eng::util::TaskStatus st = seq.tick();
///
/// Verificación: HOST-128.

#include <eng/core/types.hpp>
#include <eng/core/util/function_ref.hpp>

namespace eng::util {

enum class TaskStatus : eng::u8 {
	Running = 0, ///< aún en curso; reintentar el tick siguiente
	Success = 1, ///< paso completado; avanzar al siguiente
	Failure = 2, ///< abortar la secuencia
};

using TaskStep = FunctionRef<TaskStatus()>;

/// Secuencia de pasos `FunctionRef<TaskStatus()>` de capacidad fija. `tick()` ejecuta
/// pasos encadenados hasta que uno devuelve `Running` (espera) o la secuencia termina
/// (todas `Success`) o falla. El estado de cada paso lo lleva el propio objeto del paso.
template <eng::u16 MaxSteps>
class TaskSequence {
	static_assert(MaxSteps > 0u, "TaskSequence: MaxSteps debe ser mayor que 0");

public:
	[[nodiscard]] constexpr bool add(TaskStep step) noexcept {
		if (m_count >= MaxSteps) {
			return false;
		}
		m_steps[m_count++] = step;
		return true;
	}

	[[nodiscard]] constexpr eng::u16 step_count() const noexcept { return m_count; }
	[[nodiscard]] constexpr eng::u16 current() const noexcept { return m_index; }
	[[nodiscard]] constexpr bool failed() const noexcept { return m_failed; }
	[[nodiscard]] constexpr bool done() const noexcept {
		return !m_failed && m_index >= m_count;
	}

	constexpr void reset() noexcept {
		m_index = 0u;
		m_failed = false;
	}

	/// Avanza la secuencia. `Success` cuando todos los pasos terminan, `Failure` si uno
	/// falla (no se ejecutan los siguientes), `Running` si el paso actual espera.
	[[nodiscard]] constexpr TaskStatus tick() noexcept {
		if (m_failed) {
			return TaskStatus::Failure;
		}
		while (m_index < m_count) {
			const TaskStatus status = m_steps[m_index]();
			if (status == TaskStatus::Running) {
				return TaskStatus::Running;
			}
			if (status == TaskStatus::Failure) {
				m_failed = true;
				return TaskStatus::Failure;
			}
			++m_index; // Success -> siguiente paso
		}
		return TaskStatus::Success;
	}

private:
	TaskStep m_steps[MaxSteps] {};
	eng::u16 m_count = 0u;
	eng::u16 m_index = 0u;
	bool m_failed = false;
};

/// Paso de espera: `Running` durante `ticks` y luego `Success`. Es un callable const (el
/// contador es `mutable`), de modo que vale como `TaskStep` sin dueño de memoria.
class Delay {
public:
	explicit constexpr Delay(eng::u16 ticks) noexcept
		: m_ticks(ticks), m_remaining(ticks) {}

	/// Reinicia la espera (por si la secuencia se reutiliza).
	constexpr void reset() noexcept { m_remaining = m_ticks; }

	[[nodiscard]] constexpr TaskStatus operator()() const noexcept {
		if (m_remaining == 0u) {
			return TaskStatus::Success;
		}
		--m_remaining;
		return m_remaining == 0u ? TaskStatus::Success : TaskStatus::Running;
	}

private:
	eng::u16 m_ticks = 0u;
	mutable eng::u16 m_remaining = 0u;
};

} // namespace eng::util
