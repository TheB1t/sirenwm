#pragma once

#include <cmath>
#include <exception>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace testing {

struct AssertionFailure : public std::exception {
    const char* what() const noexcept override { return "assertion failure"; }
};

struct TestCase {
    const char* suite;
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* suite, const char* name, void (*fn)()) {
        registry().push_back({ suite, name, fn });
    }
};

struct Context {
    int failures = 0;
};

inline thread_local Context* current_context = nullptr;

template<typename T>
concept Streamable = requires(std::ostream& os, const T& value) {
    os << value;
};

inline std::string repr(std::nullptr_t) {
    return "nullptr";
}

inline std::string repr(const char* value) {
    return value ? std::string(value) : "nullptr";
}

inline std::string repr(char* value) {
    return repr(static_cast<const char*>(value));
}

template<typename T>
std::string repr(const T& value) {
    if constexpr (Streamable<T>) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return "<unprintable>";
    }
}

inline void record_failure(const char* file, int line, const std::string& message, bool fatal) {
    if (current_context)
        current_context->failures++;

    std::cerr << file << ":" << line << ": Failure\n"
              << message << "\n";

    if (fatal)
        throw AssertionFailure();
}

template<typename L, typename R>
void expect_eq(const L& lhs, const R& rhs,
               const char* lhs_expr, const char* rhs_expr,
               const char* file, int line, bool fatal) {
    if (!(lhs == rhs)) {
        std::ostringstream os;
        os << "Expected equality of these values:\n"
           << "  " << lhs_expr << "\n"
           << "    Which is: " << repr(lhs) << "\n"
           << "  " << rhs_expr << "\n"
           << "    Which is: " << repr(rhs);
        record_failure(file, line, os.str(), fatal);
    }
}

template<typename L, typename R>
void expect_ne(const L& lhs, const R& rhs,
               const char* lhs_expr, const char* rhs_expr,
               const char* file, int line, bool fatal) {
    if (lhs == rhs) {
        std::ostringstream os;
        os << "Expected inequality of these values:\n"
           << "  " << lhs_expr << "\n"
           << "  " << rhs_expr << "\n"
           << "Both are: " << repr(lhs);
        record_failure(file, line, os.str(), fatal);
    }
}

template<typename L, typename R>
void expect_ge(const L& lhs, const R& rhs,
               const char* lhs_expr, const char* rhs_expr,
               const char* file, int line, bool fatal) {
    if (!(lhs >= rhs)) {
        std::ostringstream os;
        os << "Expected: " << lhs_expr << " >= " << rhs_expr << "\n"
           << "  Actual: " << repr(lhs) << " vs " << repr(rhs);
        record_failure(file, line, os.str(), fatal);
    }
}

template<typename L, typename R>
void expect_gt(const L& lhs, const R& rhs,
               const char* lhs_expr, const char* rhs_expr,
               const char* file, int line, bool fatal) {
    if (!(lhs > rhs)) {
        std::ostringstream os;
        os << "Expected: " << lhs_expr << " > " << rhs_expr << "\n"
           << "  Actual: " << repr(lhs) << " vs " << repr(rhs);
        record_failure(file, line, os.str(), fatal);
    }
}

template<typename L, typename R>
void expect_le(const L& lhs, const R& rhs,
               const char* lhs_expr, const char* rhs_expr,
               const char* file, int line, bool fatal) {
    if (!(lhs <= rhs)) {
        std::ostringstream os;
        os << "Expected: " << lhs_expr << " <= " << rhs_expr << "\n"
           << "  Actual: " << repr(lhs) << " vs " << repr(rhs);
        record_failure(file, line, os.str(), fatal);
    }
}

template<typename L, typename R, typename A>
void expect_near(const L& lhs, const R& rhs, const A& abs_error,
                 const char* lhs_expr, const char* rhs_expr, const char* abs_expr,
                 const char* file, int line) {
    double diff = std::fabs(static_cast<double>(lhs) - static_cast<double>(rhs));
    if (diff > static_cast<double>(abs_error)) {
        std::ostringstream os;
        os << "The difference between " << lhs_expr << " and " << rhs_expr
           << " is " << diff << ", which exceeds " << abs_expr
           << ", where\n"
           << lhs_expr << " evaluates to " << repr(lhs) << ",\n"
           << rhs_expr << " evaluates to " << repr(rhs) << ", and\n"
           << abs_expr << " evaluates to " << repr(abs_error);
        record_failure(file, line, os.str(), false);
    }
}

inline void expect_true(bool val, const char* expr, const char* file, int line, bool fatal) {
    if (!val) {
        std::ostringstream os;
        os << "Expected: " << expr << " is true\n"
           << "  Actual: false";
        record_failure(file, line, os.str(), fatal);
    }
}

inline void InitGoogleTest(int*, char**) {}

inline int RunAllTests() {
    const auto& tests = registry();
    int failed_tests = 0;

    std::cout << "[==========] Running " << tests.size() << " test(s)\n";
    for (const auto& test : tests) {
        std::cout << "[ RUN      ] " << test.suite << "." << test.name << "\n";
        Context ctx;
        current_context = &ctx;

        try {
            test.fn();
        } catch (const AssertionFailure&) {
            // Fatal assertion already recorded.
        } catch (const std::exception& ex) {
            record_failure(__FILE__, __LINE__, std::string("Unhandled exception: ") + ex.what(), false);
        } catch (...) {
            record_failure(__FILE__, __LINE__, "Unhandled non-standard exception", false);
        }

        current_context = nullptr;
        if (ctx.failures == 0) {
            std::cout << "[       OK ] " << test.suite << "." << test.name << "\n";
        } else {
            failed_tests++;
            std::cout << "[  FAILED  ] " << test.suite << "." << test.name
                      << " (" << ctx.failures << " failure(s))\n";
        }
    }

    std::cout << "[==========] " << tests.size() << " test(s) ran\n";
    if (failed_tests == 0)
        std::cout << "[  PASSED  ] " << tests.size() << " test(s)\n";
    else
        std::cout << "[  FAILED  ] " << failed_tests << " test(s)\n";

    return failed_tests == 0 ? 0 : 1;
}

} // namespace testing

#define TEST(Suite, Name) \
    static void Suite##_##Name##_Test(); \
    static ::testing::Registrar Suite##_##Name##_registrar(#Suite, #Name, &Suite##_##Name##_Test); \
    static void Suite##_##Name##_Test()

#define EXPECT_EQ(lhs, rhs) \
    do { \
        ::testing::expect_eq((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, false); \
    } while (0)

#define EXPECT_NE(lhs, rhs) \
    do { \
        ::testing::expect_ne((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, false); \
    } while (0)

#define EXPECT_GE(lhs, rhs) \
    do { \
        ::testing::expect_ge((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, false); \
    } while (0)

#define EXPECT_GT(lhs, rhs) \
    do { \
        ::testing::expect_gt((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, false); \
    } while (0)

#define EXPECT_LE(lhs, rhs) \
    do { \
        ::testing::expect_le((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, false); \
    } while (0)

#define EXPECT_NEAR(lhs, rhs, abs_error) \
    do { \
        ::testing::expect_near((lhs), (rhs), (abs_error), #lhs, #rhs, #abs_error, __FILE__, __LINE__); \
    } while (0)

#define ASSERT_EQ(lhs, rhs) \
    do { \
        ::testing::expect_eq((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, true); \
    } while (0)

#define ASSERT_NE(lhs, rhs) \
    do { \
        ::testing::expect_ne((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, true); \
    } while (0)

#define ASSERT_GE(lhs, rhs) \
    do { \
        ::testing::expect_ge((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, true); \
    } while (0)

#define ASSERT_GT(lhs, rhs) \
    do { \
        ::testing::expect_gt((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, true); \
    } while (0)

#define ASSERT_LE(lhs, rhs) \
    do { \
        ::testing::expect_le((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__, true); \
    } while (0)

#define EXPECT_LT(lhs, rhs) \
    do { \
        ::testing::expect_gt((rhs), (lhs), #rhs, #lhs, __FILE__, __LINE__, false); \
    } while (0)

#define ASSERT_LT(lhs, rhs) \
    do { \
        ::testing::expect_gt((rhs), (lhs), #rhs, #lhs, __FILE__, __LINE__, true); \
    } while (0)

#define EXPECT_TRUE(expr) \
    do { ::testing::expect_true(!!(expr), #expr, __FILE__, __LINE__, false); } while (0)

#define EXPECT_FALSE(expr) \
    do { ::testing::expect_true(!(expr), #expr, __FILE__, __LINE__, false); } while (0)

#define ASSERT_TRUE(expr) \
    do { ::testing::expect_true(!!(expr), #expr, __FILE__, __LINE__, true); } while (0)

#define ASSERT_FALSE(expr) \
    do { ::testing::expect_true(!(expr), #expr, __FILE__, __LINE__, true); } while (0)

#define EXPECT_NO_FATAL_FAILURE(statement) \
    do { \
        try { \
            statement; \
        } catch (const ::testing::AssertionFailure&) { \
            ::testing::record_failure(__FILE__, __LINE__, "Expected no fatal failure: " #statement, false); \
        } catch (const std::exception& ex) { \
            ::testing::record_failure(__FILE__, __LINE__, std::string("Unexpected exception: ") + ex.what(), false); \
        } catch (...) { \
            ::testing::record_failure(__FILE__, __LINE__, "Unexpected non-standard exception", false); \
        } \
    } while (0)

#define RUN_ALL_TESTS() ::testing::RunAllTests()
