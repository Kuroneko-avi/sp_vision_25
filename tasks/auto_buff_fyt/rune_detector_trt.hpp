#ifndef AUTO_BUFF_FYT__RUNE_DETECTOR_TRT_HPP
#define AUTO_BUFF_FYT__RUNE_DETECTOR_TRT_HPP

#include <yaml-cpp/yaml.h>

#include <Eigen/Dense>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <tuple>
#include <vector>

#include "types.hpp"

namespace auto_buff_fyt
{
class RuneDetectorTRT
{
public:
  struct GridAndStride
  {
    int grid0;
    int grid1;
    int stride;
  };

  explicit RuneDetectorTRT(const std::string & config_path);
  ~RuneDetectorTRT();

  std::vector<RuneObject> detect(const cv::Mat & rgb_img);

  std::tuple<cv::Point2f, cv::Mat> detect_r_tag(
    const cv::Mat & bgr_img, const cv::Point2f & prior) const;

  bool use_r_tag() const { return detect_r_tag_; }

private:
  struct Impl;

  void init();

  std::string model_path_;
  float conf_threshold_;
  int top_k_;
  float nms_threshold_;
  bool detect_r_tag_;
  int binary_thresh_;
  int r_tag_roi_half_size_;
  float r_tag_max_distance_;
  float r_tag_min_area_;

  std::mutex mtx_;
  std::vector<int> strides_;
  std::vector<GridAndStride> grid_strides_;
  std::unique_ptr<Impl> impl_;
};
}  // namespace auto_buff_fyt

#endif  // AUTO_BUFF_FYT__RUNE_DETECTOR_TRT_HPP
