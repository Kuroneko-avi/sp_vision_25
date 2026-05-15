#include <chrono>

#include <fmt/core.h>
#include <opencv2/opencv.hpp>

#include "tasks/auto_aim/armor.hpp"
#include "tasks/auto_aim/aimer.hpp"
#include "tasks/auto_aim/planner/planner.hpp"
#include "tools/math_tools.hpp"

namespace
{
auto_aim::Target make_target(double x, double z)
{
  std::vector<cv::Point2f> armor_keypoints{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  auto_aim::Armor armor(3, 1.0f, cv::Rect(), armor_keypoints);
  armor.type = auto_aim::small;
  armor.name = auto_aim::one;
  armor.priority = auto_aim::first;
  armor.xyz_in_world = {x, 0, z};
  armor.ypr_in_world = Eigen::Vector3d::Zero();
  armor.ypd_in_world = tools::xyz2ypd(armor.xyz_in_world);

  return {
    armor, std::chrono::steady_clock::now(), 0.2, 4, Eigen::VectorXd::Zero(11)};
}

int check_pitch_sign(auto_aim::Planner & planner, double z, const char * label)
{
  auto plan = planner.plan(make_target(3.0, z), 22.5);
  if (!plan.control) {
    fmt::print(stderr, "{}: planner returned an invalid plan\n", label);
    return 1;
  }

  auto expected_positive = z > 0;
  auto target_pitch_ok = expected_positive ? plan.target_pitch > 0 : plan.target_pitch < 0;
  auto plan_pitch_ok = expected_positive ? plan.pitch > 0 : plan.pitch < 0;
  if (target_pitch_ok && plan_pitch_ok) return 0;

  fmt::print(
    stderr, "{}: target_pitch={}, plan_pitch={}\n", label, plan.target_pitch, plan.pitch);
  return 1;
}

int check_pitch_sign(auto_aim::Aimer & aimer, double z, const char * label)
{
  auto command =
    aimer.aim({make_target(3.0, z)}, std::chrono::steady_clock::now(), 22.5, false);
  if (!command.control) {
    fmt::print(stderr, "{}: aimer returned an invalid command\n", label);
    return 1;
  }

  auto expected_positive = z > 0;
  auto command_pitch_ok = expected_positive ? command.pitch > 0 : command.pitch < 0;
  if (command_pitch_ok) return 0;

  fmt::print(stderr, "{}: command_pitch={}\n", label, command.pitch);
  return 1;
}
}  // namespace

int main()
{
  auto_aim::Aimer aimer("configs/standard3_trt.yaml");
  auto_aim::Planner planner("configs/standard3_trt.yaml");

  if (check_pitch_sign(planner, 1.0, "planner upper target") != 0) return 1;
  if (check_pitch_sign(planner, -1.0, "planner lower target") != 0) return 1;
  if (check_pitch_sign(aimer, 1.0, "aimer upper target") != 0) return 1;
  if (check_pitch_sign(aimer, -1.0, "aimer lower target") != 0) return 1;

  fmt::print("auto aim pitch sign test passed\n");
  return 0;
}
