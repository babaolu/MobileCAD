#pragma once
#include <stdexcept>
#include <string>
#include <system_error>

namespace utils {

// Simple result type for error handling (inspired by Expected)
template<typename T>
class Result {
    T m_value;
    std::exception_ptr m_error;

public:
    // Success
    template<typename U>
    constexpr Result(U&& value) noexcept
        : m_value(std::forward<U>(value)), m_error() {}

    // Failure
    constexpr Result(std::exception_ptr error) noexcept
        : m_value(), m_error(error) {}

    // Factory methods
    template<typename U>
    static Result<U> ok(U&& value) {
        return Result<U>(std::forward<U>(value));
    }

    static Result<T> err(std::exception_ptr error) {
        return Result<T>(error);
    }

    static Result<T> err(const std::exception& e) {
        return Result<T>(std::make_exception_ptr(e));
    }

    static Result<T> err(const std::string& msg) {
        return Result<T>(std::make_exception_ptr(std::runtime_error(msg)));
    }

    // Query
    constexpr bool isOk() const noexcept { return !m_error; }
    constexpr bool isErr() const noexcept { return static_cast<bool>(m_error); }

    // Accessors
    T& value() & {
        if (m_error) std::rethrow_exception(m_error);
        return m_value;
    }

    const T& value() const& {
        if (m_error) std::rethrow_exception(m_error);
        return m_value;
    }

    T&& value() && {
        if (m_error) std::rethrow_exception(m_error);
        return std::move(m_value);
    }

    std::exception_ptr error() const noexcept {
        return m_error;
    }

    // Throw on error
    T& get() & {
        if (m_error) std::rethrow_exception(m_error);
        return m_value;
    }

    const T& get() const& {
        if (m_error) std::rethrow_exception(m_error);
        return m_value;
    }
};

// Convenience macros for error propagation
#define TRY(expr) ({ \
    auto&& _res = (expr); \
    if (!_res.isOk()) return _res; \
    std::forward<decltype(_res)>(_res).value(); \
})

} // namespace utils
