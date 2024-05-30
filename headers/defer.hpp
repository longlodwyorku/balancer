#pragma once
#include <utility>
template <typename F>
struct deferer : F
{
    ~deferer() { static_cast<F&>(*this)(); }
};

template <typename F>
constexpr deferer<F> make_deferer(const F& f) {
  return deferer<F>{f};
}

template <typename F>
constexpr deferer<F> make_deferer(F&& f) {
  return deferer<F>{std::move(f)};
}
#define CONCAT(x, y) x ## y
#define RCONCAT(x, y) CONCAT(x, y)
#define defer(...) auto RCONCAT(_, __LINE__) = make_deferer([&] {__VA_ARGS__;})
