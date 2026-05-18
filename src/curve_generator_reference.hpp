#ifndef SRC__CURVE_GENERATOR_REFERENCE_HPP
#define SRC__CURVE_GENERATOR_REFERENCE_HPP

#include <cmath>
#include <utility>

#include "tasks/auto_aim/planner/planner.hpp"
#include "tools/math_tools.hpp"

namespace curve_generator
{
inline double evaluate_target_yaw(double elapsed_s, double amplitude_rad, double frequency_hz)
{
  auto phase = std::fmod(elapsed_s * frequency_hz, 1.0);
  if (phase < 0) phase += 1.0;
  return -amplitude_rad + 2.0 * amplitude_rad * phase;
}

inline double evaluate_target_pitch(
  double elapsed_s, double frequency_hz, double low_pitch_rad, double high_pitch_rad)
{
  auto phase = std::fmod(elapsed_s * frequency_hz, 1.0);
  if (phase < 0) phase += 1.0;
  return phase < 0.5 ? low_pitch_rad : high_pitch_rad;
}

inline std::pair<double, auto_aim::Trajectory> build_reference(
  double elapsed_s, double yaw_amplitude_rad, double yaw_frequency_hz, double pitch_frequency_hz,
  double pitch_low_rad, double pitch_high_rad)
{
  auto yaw0 = evaluate_target_yaw(elapsed_s, yaw_amplitude_rad, yaw_frequency_hz);
  auto_aim::Trajectory traj = auto_aim::Trajectory::Zero();

  for (int i = 0; i < auto_aim::HORIZON; ++i) {
    auto relative_t = (i - auto_aim::HALF_HORIZON) * auto_aim::DT;
    auto sample_t = elapsed_s + relative_t;
    auto yaw = evaluate_target_yaw(sample_t, yaw_amplitude_rad, yaw_frequency_hz);
    auto yaw_prev = evaluate_target_yaw(sample_t - auto_aim::DT, yaw_amplitude_rad, yaw_frequency_hz);
    auto yaw_next = evaluate_target_yaw(sample_t + auto_aim::DT, yaw_amplitude_rad, yaw_frequency_hz);
    auto yaw_vel = tools::limit_rad(yaw_next - yaw_prev) / (2.0 * auto_aim::DT);
    auto pitch = evaluate_target_pitch(sample_t, pitch_frequency_hz, pitch_low_rad, pitch_high_rad);

    traj.col(i) << tools::limit_rad(yaw - yaw0), yaw_vel, pitch, 0.0;
  }

  return {yaw0, traj};
}

}  // namespace curve_generator

#endif  // SRC__CURVE_GENERATOR_REFERENCE_HPP
