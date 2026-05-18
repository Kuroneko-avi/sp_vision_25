#include <cassert>
#include <cmath>

#include "src/curve_generator_reference.hpp"

namespace
{
bool close_to(double lhs, double rhs, double eps = 1e-9)
{
  return std::abs(lhs - rhs) < eps;
}
}  // namespace

int main()
{
  constexpr double elapsed_s = 1.25;
  constexpr double yaw_amplitude_rad = 15.0 * M_PI / 180.0;
  constexpr double yaw_frequency_hz = 0.2;
  constexpr double pitch_frequency_hz = 0.5;
  constexpr double pitch_low_rad = -3.0 * M_PI / 180.0;
  constexpr double pitch_high_rad = 4.0 * M_PI / 180.0;

  const auto [yaw0, traj] = curve_generator::build_reference(
    elapsed_s, yaw_amplitude_rad, yaw_frequency_hz, pitch_frequency_hz, pitch_low_rad,
    pitch_high_rad);

  assert(close_to(yaw0, curve_generator::evaluate_target_yaw(
                          elapsed_s, yaw_amplitude_rad, yaw_frequency_hz)));
  assert(traj.rows() == 4);
  assert(traj.cols() == auto_aim::HORIZON);
  assert(close_to(traj(3, 0), 0.0));
  assert(close_to(traj(0, auto_aim::HALF_HORIZON), 0.0));
  assert(close_to(
    traj(2, auto_aim::HALF_HORIZON),
    curve_generator::evaluate_target_pitch(
      elapsed_s, pitch_frequency_hz, pitch_low_rad, pitch_high_rad)));

  return 0;
}
