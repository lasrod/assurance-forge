#pragma once

// clang-uml parse-time shim for std::expected / std::unexpected.
//
// clang-uml uses libclang to parse our translation units. On some
// distributions (notably the Ubuntu Noble 24.04 PPA build of clang-uml
// 0.6.2 used by the docs CI), libclang fails to resolve `std::expected`
// from libstdc++ 13's `<expected>` header even when `-std=c++23` is
// applied. The exact cause appears to be that the `__cplusplus > 202002L`
// gate inside `<expected>` evaluates false during the libclang parse,
// which leaves `std::expected` undeclared and breaks every diagram whose
// TUs include `core/project_file_io.h` or `parser/xml_parser.h`.
//
// This header is force-included via `.clang-uml`'s `add_compile_flags:
// [-include, docs/clang-uml/expected_shim.h]`. The `__clanguml__` guard
// keeps it a strict no-op during normal builds, so it can never affect
// the production binary or test suite. The additional `__cplusplus`
// guard makes it a no-op even during clang-uml parsing when libclang
// has correctly applied `-std=c++23`, so libstdc++'s real `<expected>`
// header (which only declares `std::expected` when
// `__cplusplus > 202002L`) takes precedence and there is no
// redeclaration conflict.

#if defined(__clanguml__) && !(__cplusplus > 202002L)

namespace std {

template <class E>
class unexpected {
  public:
    unexpected()                                = default;
    unexpected(const unexpected&)               = default;
    unexpected(unexpected&&)                    = default;
    template <class Err = E>
    constexpr explicit unexpected(Err&&) {}
    constexpr const E& error() const& noexcept;
    constexpr E&       error() & noexcept;
};

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

    constexpr bool          has_value() const noexcept;
    constexpr explicit      operator bool() const noexcept;
    constexpr T&            value() &;
    constexpr const T&      value() const&;
    constexpr E&            error() & noexcept;
    constexpr const E&      error() const& noexcept;
    constexpr T*            operator->() noexcept;
    constexpr const T*      operator->() const noexcept;
    constexpr T&            operator*() & noexcept;
    constexpr const T&      operator*() const& noexcept;
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

    constexpr bool          has_value() const noexcept;
    constexpr explicit      operator bool() const noexcept;
    constexpr void          value() const&;
    constexpr E&            error() & noexcept;
    constexpr const E&      error() const& noexcept;
};

} // namespace std

#endif // __clanguml__ && pre-C++23
