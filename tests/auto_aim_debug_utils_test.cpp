#include <fmt/core.h>

#include "src/auto_aim_debug_utils.hpp"

int main()
{
  if (auto_aim::debug::plot_pitch(0.35) != -0.35) {
    fmt::print(stderr, "plot_pitch positive case failed\n");
    return 1;
  }

  if (auto_aim::debug::plot_pitch(-0.42) != 0.42) {
    fmt::print(stderr, "plot_pitch negative case failed\n");
    return 1;
  }

  const double gimbal_pitch = 0.30;
  const double target_pitch = -0.25;
  const double expected_error = 0.05;
  const double error = auto_aim::debug::plot_pitch_error(gimbal_pitch, target_pitch);
  if (std::abs(error - expected_error) > 1e-9) {
    fmt::print(stderr, "plot_pitch_error failed: {}\n", error);
    return 1;
  }

  fmt::print("auto aim debug utils test passed\n");
  return 0;
}
