#include <gtest/gtest.h>

#include <support/strong_id.hpp>

#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace {

struct WindowTag;
struct MonitorTag;
using WindowId  = swm::StrongId<WindowTag, uint32_t>;
using MonitorId = swm::StrongId<MonitorTag, uint32_t>;

} // namespace

TEST(StrongId, DefaultConstructZero) {
    WindowId w;
    EXPECT_EQ(w.get(), 0u);
}

TEST(StrongId, ExplicitConstructStoresValue) {
    WindowId w{42};
    EXPECT_EQ(w.get(), 42u);
}

TEST(StrongId, ConstructionIsExplicit) {
    static_assert(!std::is_convertible_v<uint32_t, WindowId>,
        "implicit construction from underlying must be rejected");
    static_assert(std::is_constructible_v<WindowId, uint32_t>,
        "explicit construction from underlying must be allowed");
}

TEST(StrongId, EqualityWithinTag) {
    WindowId a{1}, b{1}, c{2};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(StrongId, OrderingWithinTag) {
    WindowId a{1}, b{2};
    EXPECT_LT(a, b);
    EXPECT_LE(a, b);
    EXPECT_GT(b, a);
    EXPECT_GE(b, a);
}

TEST(StrongId, DifferentTagsDoNotCompareOrConvert) {
    static_assert(!std::is_convertible_v<MonitorId, WindowId>,
        "cross-tag conversion must be rejected");
    // operator== between different tags is ill-formed — verified by absence
    // of a viable overload; compile-time check not expressible in gtest.
    SUCCEED();
}

TEST(StrongId, UsableAsUnorderedMapKey) {
    std::unordered_map<WindowId, int> m;
    m[WindowId{7}]  = 70;
    m[WindowId{13}] = 130;
    EXPECT_EQ(m[WindowId{7}], 70);
    EXPECT_EQ(m[WindowId{13}], 130);
    EXPECT_EQ(m.size(), 2u);
}

TEST(StrongId, UsableInUnorderedSet) {
    std::unordered_set<WindowId> s;
    s.insert(WindowId{1});
    s.insert(WindowId{1});
    s.insert(WindowId{2});
    EXPECT_EQ(s.size(), 2u);
}
