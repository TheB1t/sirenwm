#include <gtest/gtest.h>
#include "test_harness.hpp"
#include "sysinfo/sysinfo_module.hpp"

TEST(Sysinfo, StartsAndStopsWithoutCrash) {
    TestHarness h;
    h.use<SysinfoModule>();
    h.start();
}
