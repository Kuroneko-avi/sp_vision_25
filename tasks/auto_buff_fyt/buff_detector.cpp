#include "buff_detector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace auto_buff_fyt
{
namespace
{
EnemyColor parse_buff_detect_color(const std::string & enemy_color)
{
  return enemy_color == "blue" ? EnemyColor::RED : EnemyColor::BLUE;
}

float normalize_angle(float angle)
{
  while (angle > static_cast<float>(CV_PI)) angle -= static_cast<float>(2.0 * CV_PI);
  while (angle < static_cast<float>(-CV_PI)) angle += static_cast<float>(2.0 * CV_PI);
  return angle;
}

cv::Point2f average_fanblade_center(const RuneObject & obj)
{
  return (
           obj.pts.bottom_left + obj.pts.top_left + obj.pts.top_right + obj.pts.bottom_right) /
         4.0f;
}
}  // namespace

Buff_Detector::Buff_Detector(const std::string & config_path) : detector_(config_path)
{
  auto yaml = YAML::LoadFile(config_path);
  auto node = yaml["buff_fyt_detector"];
  detect_color_ = parse_buff_detect_color(yaml["enemy_color"].as<std::string>());
  max_candidates_ = node && node["max_candidates"] ? node["max_candidates"].as<int>() : 2;
  min_radius_ratio_ =
    node && node["min_radius_ratio"] ? node["min_radius_ratio"].as<float>() : 0.8f;
  lock_angle_thresh_rad_ =
    node && node["lock_angle_thresh_rad"] ? node["lock_angle_thresh_rad"].as<float>() :
                                            static_cast<float>(CV_PI / 6.0);
  if (max_candidates_ < 1) max_candidates_ = 1;
  if (min_radius_ratio_ < 0.0f) min_radius_ratio_ = 0.0f;
  if (lock_angle_thresh_rad_ < 0.0f) lock_angle_thresh_rad_ = 0.0f;
}

std::optional<PowerRune> Buff_Detector::detect(cv::Mat & bgr_img)
{
  last_objects_.clear();
  last_filtered_objects_.clear();
  last_candidates_.clear();
  last_binary_roi_ = cv::Mat::zeros(1, 1, CV_8UC3);

  if (bgr_img.empty()) return std::nullopt;

  cv::Mat rgb_img;
  cv::cvtColor(bgr_img, rgb_img, cv::COLOR_BGR2RGB);

  last_objects_ = detector_.detect(rgb_img);
  last_filtered_objects_ = last_objects_;
  last_filtered_objects_.erase(
    std::remove_if(
      last_filtered_objects_.begin(), last_filtered_objects_.end(),
      [this](const RuneObject & obj) { return obj.color != detect_color_; }),
    last_filtered_objects_.end());

  if (last_filtered_objects_.empty()) return std::nullopt;

  std::sort(
    last_filtered_objects_.begin(), last_filtered_objects_.end(),
    [](const RuneObject & a, const RuneObject & b) { return a.prob > b.prob; });

  const float prob_sum = std::accumulate(
    last_filtered_objects_.begin(), last_filtered_objects_.end(), 0.0f,
    [](float sum, const RuneObject & obj) { return sum + obj.prob; });
  const cv::Point2f r_prior = std::accumulate(
    last_filtered_objects_.begin(), last_filtered_objects_.end(), cv::Point2f(0.0f, 0.0f),
    [prob_sum](const cv::Point2f & p, const RuneObject & obj) {
      const float weight = prob_sum > 1e-6f ? obj.prob / prob_sum : 1.0f;
      return p + obj.pts.r_center * weight;
    });

  cv::Point2f r_center;
  if (detector_.use_r_tag()) {
    std::tie(r_center, last_binary_roi_) = detector_.detect_r_tag(bgr_img, r_prior);
  } else {
    r_center = r_prior;
  }

  std::for_each(last_filtered_objects_.begin(), last_filtered_objects_.end(), [r_center](RuneObject & obj) {
    obj.pts.r_center = r_center;
  });

  filter_by_radius_ratio(last_filtered_objects_);
  if (last_filtered_objects_.empty()) {
    locked_candidate_.reset();
    return std::nullopt;
  }

  std::vector<RuneObject> inactivated_objects;
  for (const auto & obj : last_filtered_objects_) {
    if (obj.type == RuneType::INACTIVATED) inactivated_objects.push_back(obj);
  }

  auto selected_candidate = select_locked_candidate(inactivated_objects);
  if (!selected_candidate.has_value()) return std::nullopt;

  last_candidates_.push_back(selected_candidate.value());
  for (const auto & obj : inactivated_objects) {
    if (static_cast<int>(last_candidates_.size()) >= max_candidates_) break;
    if (cv::norm(fanblade_center(obj) - fanblade_center(selected_candidate.value())) < 1.0f) continue;
    last_candidates_.push_back(obj);
  }

  return to_power_rune(selected_candidate.value());
}

cv::Point2f Buff_Detector::fanblade_center(const RuneObject & obj) { return average_fanblade_center(obj); }

float Buff_Detector::radius_to_fanblade_center(const RuneObject & obj)
{
  return cv::norm(obj.pts.r_center - fanblade_center(obj));
}

float Buff_Detector::center_angle(const RuneObject & obj)
{
  const auto delta = fanblade_center(obj) - obj.pts.r_center;
  return std::atan2(delta.y, delta.x);
}

std::optional<PowerRune> Buff_Detector::to_power_rune(const RuneObject & obj)
{
  std::vector<cv::Point2f> armor_points = {
    obj.pts.bottom_left,
    obj.pts.top_left,
    obj.pts.top_right,
    obj.pts.bottom_right};
  const cv::Point2f center = fanblade_center(obj);

  std::vector<auto_buff::FanBlade> fanblades;
  fanblades.emplace_back(armor_points, center, auto_buff::_light);
  PowerRune power_rune(fanblades, obj.pts.r_center, std::nullopt);
  if (power_rune.is_unsolve()) return std::nullopt;
  return power_rune;
}

void Buff_Detector::filter_by_radius_ratio(std::vector<RuneObject> & objects) const
{
  if (objects.empty()) return;

  float max_radius = 0.0f;
  for (const auto & obj : objects) {
    max_radius = std::max(max_radius, radius_to_fanblade_center(obj));
  }

  if (max_radius <= 1e-6f) return;

  const float min_radius = max_radius * min_radius_ratio_;
  objects.erase(
    std::remove_if(
      objects.begin(), objects.end(),
      [min_radius](const RuneObject & obj) {
        return radius_to_fanblade_center(obj) < min_radius;
      }),
    objects.end());
}

std::optional<RuneObject> Buff_Detector::select_locked_candidate(const std::vector<RuneObject> & candidates)
{
  if (candidates.empty()) {
    locked_candidate_.reset();
    return std::nullopt;
  }

  if (locked_candidate_.has_value()) {
    float best_angle_delta = std::numeric_limits<float>::max();
    auto best_it = candidates.end();
    for (auto it = candidates.begin(); it != candidates.end(); ++it) {
      const float angle_delta = std::abs(normalize_angle(
        center_angle(*it) - center_angle(locked_candidate_.value())));
      if (angle_delta <= lock_angle_thresh_rad_ && angle_delta < best_angle_delta) {
        best_angle_delta = angle_delta;
        best_it = it;
      }
    }
    if (best_it != candidates.end()) {
      locked_candidate_ = *best_it;
      return locked_candidate_;
    }
  }

  locked_candidate_ = candidates.front();
  return locked_candidate_;
}
}  // namespace auto_buff_fyt
