/// @file
/// @brief Offline tests for the command-string builders (no device needed).
///
/// These pin the exact wire strings against the device web UI's grammar
/// (web/src/esp-antenna-switcher.ts: `_startAuto`, `_runPlan`, `set:N`, `stop`).

#include <antenna_switcher/client.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace {

using antenna_switcher::PlanStep;
using antenna_switcher::TimeUnit;
using namespace antenna_switcher::detail;

TEST(Commands, SetInput) {
    EXPECT_EQ(build_set_input(5), "set:5");
    EXPECT_EQ(build_set_input(10), "set:10");
}

TEST(Commands, Stop) {
    EXPECT_EQ(build_stop(), "stop");
}

TEST(Commands, StartAutoMilliseconds) {
    // A partial selection (1..9 inputs) becomes an explicit cycle order.
    EXPECT_EQ(build_start_auto(100, TimeUnit::Ms, {1, 2, 3}), "auto:100:1,2,3");
}

TEST(Commands, StartAutoMicrosecondsNoSelection) {
    EXPECT_EQ(build_start_auto(5000, TimeUnit::Us, {}), "auto:5000u");
}

TEST(Commands, StartAutoFullSelectionDropsCsv) {
    // An empty or full (10) selection cycles every input — no CSV suffix.
    EXPECT_EQ(build_start_auto(250, TimeUnit::Ms, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), "auto:250");
}

TEST(Commands, StartAutoMicrosecondsSingleInput) {
    EXPECT_EQ(build_start_auto(1000, TimeUnit::Us, {7}), "auto:1000u:7");
}

TEST(Commands, RunPlanMixedSteps) {
    const std::vector<PlanStep> steps{
        PlanStep::input_step(1),
        PlanStep::delay_step(100, TimeUnit::Ms),
        PlanStep::input_step(2),
        PlanStep::delay_step(50, TimeUnit::Us),
    };
    EXPECT_EQ(build_run_plan(steps, /*repeat=*/true), "plan:1,s100,2,s50u,r");
    EXPECT_EQ(build_run_plan(steps, /*repeat=*/false), "plan:1,s100,2,s50u");
}

}  // namespace
