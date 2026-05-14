#include "tracker.hpp"

#include <numeric>

#include <yaml-cpp/yaml.h>

#include "tools/logger.hpp"
#include "tools/math_tools.hpp"

namespace auto_aim_eskf
{
Tracker::Tracker(const std::string & config_path, auto_aim::Solver & solver)
: config_path_(config_path),
  solver_(solver),
  detect_count_(0),
  temp_lost_count_(0),
  state_("lost"),
  pre_state_("lost"),
  last_timestamp_(std::chrono::steady_clock::now()),
  omni_target_priority_(auto_aim::ArmorPriority::fifth)
{
  auto yaml = YAML::LoadFile(config_path);
  enemy_color_ =
    (yaml["enemy_color"].as<std::string>() == "red") ? auto_aim::Color::red : auto_aim::Color::blue;
  min_detect_count_ = yaml["min_detect_count"].as<int>();
  max_temp_lost_count_ = yaml["max_temp_lost_count"].as<int>();
  outpost_max_temp_lost_count_ = yaml["outpost_max_temp_lost_count"].as<int>();
  normal_temp_lost_count_ = max_temp_lost_count_;
}

std::string Tracker::state() const { return state_; }

std::list<Target> Tracker::track(
  std::list<auto_aim::Armor> & armors, std::chrono::steady_clock::time_point t, bool use_enemy_color)
{
  (void)use_enemy_color;

  const auto dt = tools::delta_time(t, last_timestamp_);
  last_timestamp_ = t;

  if (state_ != "lost" && dt > 0.1) {
    tools::logger()->warn("[ESKF Tracker] Large dt: {:.3f}s", dt);
    state_ = "lost";
  }

  armors.remove_if([&](const auto_aim::Armor & a) { return a.color != enemy_color_; });

  armors.sort([](const auto_aim::Armor & a, const auto_aim::Armor & b) {
    const cv::Point2f img_center(0.5f, 0.5f);
    return cv::norm(a.center_norm - img_center) < cv::norm(b.center_norm - img_center);
  });

  armors.sort([](const auto_aim::Armor & a, const auto_aim::Armor & b) {
    return a.priority < b.priority;
  });

  bool found = false;
  if (state_ == "lost") {
    found = set_target(armors, t);
  } else {
    found = update_target(armors, t);
  }

  state_machine(found);

  if (state_ != "lost" && target_.diverged()) {
    tools::logger()->debug("[ESKF Tracker] Target diverged!");
    state_ = "lost";
    return {};
  }

  if (state_ == "lost") return {};
  return {target_};
}

std::tuple<omniperception::DetectionResult, std::list<Target>> Tracker::track(
  const std::vector<omniperception::DetectionResult> & detection_queue,
  std::list<auto_aim::Armor> & armors, std::chrono::steady_clock::time_point t, bool use_enemy_color)
{
  (void)detection_queue;
  return {omniperception::DetectionResult{std::list<auto_aim::Armor>(), t, 0, 0}, track(armors, t, use_enemy_color)};
}

void Tracker::state_machine(bool found)
{
  if (state_ == "lost") {
    if (!found) return;
    state_ = "detecting";
    detect_count_ = 1;
    return;
  }

  if (state_ == "detecting") {
    if (found) {
      ++detect_count_;
      if (detect_count_ >= min_detect_count_) state_ = "tracking";
    } else {
      detect_count_ = 0;
      state_ = "lost";
    }
    return;
  }

  if (state_ == "tracking") {
    if (found) return;
    temp_lost_count_ = 1;
    state_ = "temp_lost";
    return;
  }

  if (state_ == "temp_lost") {
    if (found) {
      state_ = "tracking";
    } else {
      ++temp_lost_count_;
      max_temp_lost_count_ = (target_.name == auto_aim::ArmorName::outpost)
                               ? outpost_max_temp_lost_count_
                               : normal_temp_lost_count_;
      if (temp_lost_count_ > max_temp_lost_count_) state_ = "lost";
    }
  }
}

bool Tracker::set_target(std::list<auto_aim::Armor> & armors, std::chrono::steady_clock::time_point t)
{
  if (armors.empty()) return false;

  auto & armor = armors.front();
  solver_.solve(armor);
  target_ = Target(armor, t, Target::load_config(config_path_), solver_);
  return true;
}

bool Tracker::update_target(std::list<auto_aim::Armor> & armors, std::chrono::steady_clock::time_point t)
{
  target_.predict(t);

  int found_count = 0;
  for (const auto & armor : armors) {
    if (armor.name != target_.name || armor.type != target_.armor_type) continue;
    ++found_count;
  }
  if (found_count == 0) return false;

  std::vector<auto_aim::Armor> candidates;
  for (auto & armor : armors) {
    if (armor.name != target_.name || armor.type != target_.armor_type) continue;
    solver_.solve(armor);
    candidates.push_back(armor);
  }

  if (candidates.empty()) return false;
  target_.update(candidates);
  return true;
}

}  // namespace auto_aim_eskf
