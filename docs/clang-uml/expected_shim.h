#pragma once

// Parse-time shim for `std::expected` / `std::unexpected`, force-included
// via `.clang-uml`'s `add_compile_flags: [-include, ...]` so it is
// processed before any TU's normal #includes.
//
// Why: clang-uml's bundled libclang (Ubuntu 24.04 PPA build of 0.6.2,
// against libclang 18.1.3) fails to declare `std::expected` from
// libstdc++ 13's `<expected>` header. The header is gated on
// `__cplusplus > 202002L && __cpp_concepts >= 202002L`, and at least one
// of those checks evaluates false during the libclang parse even with
// `-std=c++23`. Every diagram whose TUs transitively include
// `core/project_file_io.h` or `parser/xml_parser.h` then fails to parse.
//
// Strategy: pre-define libstdc++'s `<expected>` include guard
// (`_GLIBCXX_EXPECTED`) so the real header becomes a no-op when later
// included, and provide our own minimal declarations of `std::expected`
// and `std::unexpected`. This is enough for libclang to resolve every
// use of those types in our headers and bodies; clang-uml only cares
// about signatures, not implementations.
//
// Safety: this file lives under `docs/clang-uml/` and is NEVER included
// by the production build. It is only ever pulled in by the docs CI
// (and by anyone running `clang-uml` locally). It is therefore safe to
// declare the shim unconditionally — no guard macro can be wrong.

#ifndef _GLIBCXX_EXPECTED
#define _GLIBCXX_EXPECTED 1

namespace std {

template <class E>
class unexpected {
  public:
    unexpected()                                       = default;
    unexpected(const unexpected&)                      = default;
    unexpected(unexpected&&)                           = default;
    // Non-templated single-arg constructor so CTAD on
    // `std::unexpected("err")` deduces `E` directly from the argument
    // via the deduction guide below. Real libstdc++ uses a templated
    // forwarding constructor plus a separate guide; we collapse both
    // into the simplest form libclang can reason about.
    constexpr explicit unexpected(E) {}
    constexpr const E& error() const& noexcept;
    constexpr E&       error() & noexcept;
};

// CTAD deduction guide. Without this, `std::unexpected(const char*)`
// cannot deduce `E` and every call site in `guidelines_parser.cpp`
// fails to compile during the libclang parse.
template <class E>
unexpected(E) -> unexpected<E>;

template <class T, class E>
class expected {
  public:
    using value_type      = T;
    using error_type      = E;
    using unexpected_type = unexpected<E>;

    constexpr expected()                = default;
    constexpr expected(const expected&) = default;
    constexpr expected(expected&&)      = default;
    constexpr expected(const T&) {}
    constexpr expected(T&&) {}
    template <class G>
    constexpr expected(const unexpected<G>&) {}

    constexpr bool     has_value() const noexcept;
    constexpr explicit operator bool() const noexcept;
    constexpr T&       value() &;
    constexpr const T& value() const&;
    constexpr E&       error() & noexcept;
    constexpr const E& error() const& noexcept;
    constexpr T*       operator->() noexcept;
    constexpr const T* operator->() const noexcept;
    constexpr T&       operator*() & noexcept;
    constexpr const T& operator*() const& noexcept;
};

template <class E>
class expected<void, E> {
  public:
    using value_type      = void;
    using error_type      = E;
    using unexpected_type = unexpected<E>;

    constexpr expected()                = default;
    constexpr expected(const expected&) = default;
    constexpr expected(expected&&)      = default;
    template <class G>
    constexpr expected(const unexpected<G>&) {}

    constexpr bool     has_value() const noexcept;
    constexpr explicit operator bool() const noexcept;
    constexpr void     value() const&;
    constexpr E&       error() & noexcept;
    constexpr const E& error() const& noexcept;
};

} // namespace std

#endif // _GLIBCXX_EXPECTED
