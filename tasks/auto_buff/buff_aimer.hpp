#ifndef AUTO_BUFF__AIMER_HPP
#define AUTO_BUFF__AIMER_HPP

#include <yaml-cpp/yaml.h>

#include <Eigen/Dense>
#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

#include "../auto_aim/planner/planner.hpp"
#include "buff_target.hpp"
#include "buff_type.hpp"
#include "io/command.hpp"
#include "io/gimbal/gimbal.hpp"

namespace auto_buff
{
class Aimer
{
public:
  struct HotParams
  {
    double yaw_offset_deg;
    double pitch_offset_deg;
    double fire_gap_time;
    double predict_time;
  };

  Aimer(const std::string & config_path);

  io::Command aim(
    Target & target, std::chrono::steady_clock::time_point & timestamp, double bullet_speed,
    bool to_now = true);

  auto_aim::Plan mpc_aim(
    Target & target, std::chrono::steady_clock::time_point & timestamp, io::GimbalState gs,
    bool to_now = true);

  double angle;      ///
  double t_gap = 0;  ///
  HotParams get_hot_params() const;
  bool apply_hot_param(const std::string & key, double value);

private:
  mutable std::mutex params_mutex_;
  SmallTarget target_;
  double yaw_offset_;
  double pitch_offset_;

  double fire_gap_time_;
  double predict_time_;

  int mistake_count_ = 0;
  bool switch_fanblade_;

  double last_yaw_ = 0;
  double last_pitch_ = 0;

  // for mpc
  bool first_in_aimer_ = true;

  std::chrono::steady_clock::time_point last_fire_t_;

  bool get_send_angle(
    auto_buff::Target & target, const double predict_time, const double bullet_speed,
    const bool to_now, double & yaw, double & pitch);
};
}  // namespace auto_buff
#endif  // AUTO_AIM__AIMER_HPP
