#ifndef AUTO_BUFF_FYT__BUFF_DETECTOR_TRT_HPP
#define AUTO_BUFF_FYT__BUFF_DETECTOR_TRT_HPP

#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <vector>

#include "buff_type.hpp"
#include "rune_detector_trt.hpp"

namespace auto_buff_fyt
{
class Buff_DetectorTRT
{
public:
  explicit Buff_DetectorTRT(const std::string & config_path);

  std::optional<PowerRune> detect(cv::Mat & bgr_img);

  const std::vector<RuneObject> & last_objects() const { return last_objects_; }
  const std::vector<RuneObject> & last_filtered_objects() const { return last_filtered_objects_; }
  const cv::Mat & last_binary_roi() const { return last_binary_roi_; }
  const std::vector<RuneObject> & last_candidates() const { return last_candidates_; }
  const std::optional<RuneObject> & locked_candidate() const { return locked_candidate_; }

private:
  static cv::Point2f fanblade_center(const RuneObject & obj);
  static float radius_to_fanblade_center(const RuneObject & obj);
  static float center_angle(const RuneObject & obj);
  static std::optional<PowerRune> to_power_rune(const RuneObject & obj);

  void filter_by_radius_ratio(std::vector<RuneObject> & objects) const;
  std::optional<RuneObject> select_locked_candidate(const std::vector<RuneObject> & candidates);

  RuneDetectorTRT detector_;
  EnemyColor detect_color_;
  int max_candidates_;
  float min_radius_ratio_;
  float lock_angle_thresh_rad_;
  std::vector<RuneObject> last_objects_;
  std::vector<RuneObject> last_filtered_objects_;
  std::vector<RuneObject> last_candidates_;
  cv::Mat last_binary_roi_;
  std::optional<RuneObject> locked_candidate_;
};
}  // namespace auto_buff_fyt

#endif  // AUTO_BUFF_FYT__BUFF_DETECTOR_TRT_HPP
