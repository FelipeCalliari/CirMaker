#pragma once

#include <cstdint>

namespace cm::sim {

enum class Logic : std::uint8_t { L0, L1, X, Z };

enum class Strength : std::uint8_t { None, Weak, Strong };

struct Drive {
    Logic value = Logic::Z;
    Strength strength = Strength::None;
    friend bool operator==(const Drive&, const Drive&) = default;
};

inline constexpr Drive strong(Logic v) { return {v, Strength::Strong}; }
inline constexpr Drive weak(Logic v) { return {v, Strength::Weak}; }
inline constexpr Drive released() { return {}; }

inline constexpr Logic fromBool(bool b) { return b ? Logic::L1 : Logic::L0; }
inline constexpr bool isKnown(Logic v) { return v == Logic::L0 || v == Logic::L1; }

inline constexpr Logic logicNot(Logic v)
{
    switch (v) {
    case Logic::L0: return Logic::L1;
    case Logic::L1: return Logic::L0;
    default: return Logic::X;
    }
}

// Combines two values driven at the same strength onto one net.
inline constexpr Logic combine(Logic a, Logic b)
{
    if (a == Logic::Z) return b;
    if (b == Logic::Z) return a;
    return a == b ? a : Logic::X;
}

inline constexpr char toChar(Logic v)
{
    switch (v) {
    case Logic::L0: return '0';
    case Logic::L1: return '1';
    case Logic::X: return 'X';
    case Logic::Z: return 'Z';
    }
    return '?';
}

} // namespace cm::sim
