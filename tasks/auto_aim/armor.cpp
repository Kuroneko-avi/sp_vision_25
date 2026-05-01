#include "armor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/opencv.hpp>

namespace auto_aim
{
Lightbar::Lightbar(const cv::RotatedRect & rotated_rect, std::size_t id)
: id(id), rotated_rect(rotated_rect)
{
  std::vector<cv::Point2f> corners(4);
  rotated_rect.points(&corners[0]);
  std::sort(corners.begin(), corners.end(), [](const cv::Point2f & a, const cv::Point2f & b) {
    return a.y < b.y;
  });

  std::array<cv::Point2f, 2> top_pair = {corners[0], corners[1]};
  std::array<cv::Point2f, 2> bottom_pair = {corners[2], corners[3]};
  std::sort(top_pair.begin(), top_pair.end(), [](const cv::Point2f & a, const cv::Point2f & b) {
    return a.x < b.x;
  });
  std::sort(bottom_pair.begin(), bottom_pair.end(), [](const cv::Point2f & a, const cv::Point2f & b) {
    return a.x < b.x;
  });

  left_top = top_pair[0];
  right_top = top_pair[1];
  right_bottom = bottom_pair[1];
  left_bottom = bottom_pair[0];

  center = rotated_rect.center;
  top = (left_top + right_top) / 2;
  bottom = (left_bottom + right_bottom) / 2;
  top2bottom = bottom - top;

  points.emplace_back(left_top);
  points.emplace_back(right_top);
  points.emplace_back(right_bottom);
  points.emplace_back(left_bottom);

  width = cv::norm(top_pair[0] - top_pair[1]);
  angle = std::atan2(top2bottom.y, top2bottom.x);
  angle_error = std::abs(angle - CV_PI / 2);
  length = cv::norm(top2bottom);
  ratio = length / width;
}

//传统构造函数
Armor::Armor(const Lightbar & left, const Lightbar & right)
: left(left), right(right), duplicated(false)
{
  color = left.color;
  center = (left.center + right.center) / 2;

  const bool use_left_edges = left.width < right.width;
  if (use_left_edges) {
    point_layout = ArmorPointLayout::left_edge_pair;
    points.emplace_back(left.left_top);
    points.emplace_back(right.left_top);
    points.emplace_back(right.left_bottom);
    points.emplace_back(left.left_bottom);
  } else {
    point_layout = ArmorPointLayout::right_edge_pair;
    points.emplace_back(left.right_top);
    points.emplace_back(right.right_top);
    points.emplace_back(right.right_bottom);
    points.emplace_back(left.right_bottom);
  }

  auto left2right = right.center - left.center;
  auto width = cv::norm(left2right);
  auto max_lightbar_length = std::max(left.length, right.length);
  auto min_lightbar_length = std::min(left.length, right.length);
  ratio = width / max_lightbar_length;
  side_ratio = max_lightbar_length / min_lightbar_length;

  auto roll = std::atan2(left2right.y, left2right.x);
  auto left_rectangular_error = std::abs(left.angle - roll - CV_PI / 2);
  auto right_rectangular_error = std::abs(right.angle - roll - CV_PI / 2);
  rectangular_error = std::max(left_rectangular_error, right_rectangular_error);
}

//神经网络构造函数
Armor::Armor(
  int class_id, float confidence, const cv::Rect & box, std::vector<cv::Point2f> armor_keypoints)
: class_id(class_id), confidence(confidence), box(box), points(armor_keypoints)
{
  point_layout = ArmorPointLayout::outer;
  center = (armor_keypoints[0] + armor_keypoints[1] + armor_keypoints[2] + armor_keypoints[3]) / 4;
  auto left_width = cv::norm(armor_keypoints[0] - armor_keypoints[3]);
  auto right_width = cv::norm(armor_keypoints[1] - armor_keypoints[2]);
  auto max_width = std::max(left_width, right_width);
  auto top_length = cv::norm(armor_keypoints[0] - armor_keypoints[1]);
  auto bottom_length = cv::norm(armor_keypoints[3] - armor_keypoints[2]);
  auto max_length = std::max(top_length, bottom_length);
  auto left_center = (armor_keypoints[0] + armor_keypoints[3]) / 2;
  auto right_center = (armor_keypoints[1] + armor_keypoints[2]) / 2;
  auto left2right = right_center - left_center;
  auto roll = std::atan2(left2right.y, left2right.x);
  auto left_rectangular_error = std::abs(
    std::atan2(
      (armor_keypoints[3] - armor_keypoints[0]).y, (armor_keypoints[3] - armor_keypoints[0]).x) -
    roll - CV_PI / 2);
  auto right_rectangular_error = std::abs(
    std::atan2(
      (armor_keypoints[2] - armor_keypoints[1]).y, (armor_keypoints[2] - armor_keypoints[1]).x) -
    roll - CV_PI / 2);
  rectangular_error = std::max(left_rectangular_error, right_rectangular_error);

  ratio = max_length / max_width;
  // color = class_id == 0 ? Color::blue : Color::red;

  if (class_id >= 0 && class_id < armor_properties.size()) {
    auto [color, name, type] = armor_properties[class_id];
    this->color = color;
    this->name = name;
    this->type = type;
  } else {
    this->color = blue;      // Default
    this->name = not_armor;  // Default
    this->type = small;      // Default
  }
}

//神经网络ROI构造函数
Armor::Armor(
  int class_id, float confidence, const cv::Rect & box, std::vector<cv::Point2f> armor_keypoints,
  cv::Point2f offset)
: class_id(class_id), confidence(confidence), box(box), points(armor_keypoints)
{
  point_layout = ArmorPointLayout::outer;
  std::transform(
    armor_keypoints.begin(), armor_keypoints.end(), armor_keypoints.begin(),
    [&offset](const cv::Point2f & point) { return point + offset; });
  std::transform(
    points.begin(), points.end(), points.begin(),
    [&offset](const cv::Point2f & point) { return point + offset; });
  center = (armor_keypoints[0] + armor_keypoints[1] + armor_keypoints[2] + armor_keypoints[3]) / 4;
  auto left_width = cv::norm(armor_keypoints[0] - armor_keypoints[3]);
  auto right_width = cv::norm(armor_keypoints[1] - armor_keypoints[2]);
  auto max_width = std::max(left_width, right_width);
  auto top_length = cv::norm(armor_keypoints[0] - armor_keypoints[1]);
  auto bottom_length = cv::norm(armor_keypoints[3] - armor_keypoints[2]);
  auto max_length = std::max(top_length, bottom_length);
  auto left_center = (armor_keypoints[0] + armor_keypoints[3]) / 2;
  auto right_center = (armor_keypoints[1] + armor_keypoints[2]) / 2;
  auto left2right = right_center - left_center;
  auto roll = std::atan2(left2right.y, left2right.x);
  auto left_rectangular_error = std::abs(
    std::atan2(
      (armor_keypoints[3] - armor_keypoints[0]).y, (armor_keypoints[3] - armor_keypoints[0]).x) -
    roll - CV_PI / 2);
  auto right_rectangular_error = std::abs(
    std::atan2(
      (armor_keypoints[2] - armor_keypoints[1]).y, (armor_keypoints[2] - armor_keypoints[1]).x) -
    roll - CV_PI / 2);
  rectangular_error = std::max(left_rectangular_error, right_rectangular_error);

  ratio = max_length / max_width;
  // color = class_id == 0 ? Color::blue : Color::red;

  if (class_id >= 0 && class_id < armor_properties.size()) {
    auto [color, name, type] = armor_properties[class_id];
    this->color = color;
    this->name = name;
    this->type = type;
  } else {
    this->color = blue;      // Default
    this->name = not_armor;  // Default
    this->type = small;      // Default
  }
}

// YOLOV5构造函数
Armor::Armor(
  int color_id, int num_id, float confidence, const cv::Rect & box,
  std::vector<cv::Point2f> armor_keypoints)
: confidence(confidence), box(box), points(armor_keypoints)
{
  point_layout = ArmorPointLayout::outer;
  center = (armor_keypoints[0] + armor_keypoints[1] + armor_keypoints[2] + armor_keypoints[3]) / 4;
  auto left_width = cv::norm(armor_keypoints[0] - armor_keypoints[3]);
  auto right_width = cv::norm(armor_keypoints[1] - armor_keypoints[2]);
  auto max_width = std::max(left_width, right_width);
  auto top_length = cv::norm(armor_keypoints[0] - armor_keypoints[1]);
  auto bottom_length = cv::norm(armor_keypoints[3] - armor_keypoints[2]);
  auto max_length = std::max(top_length, bottom_length);
  auto left_center = (armor_keypoints[0] + armor_keypoints[3]) / 2;
  auto right_center = (armor_keypoints[1] + armor_keypoints[2]) / 2;
  auto left2right = right_center - left_center;
  auto roll = std::atan2(left2right.y, left2right.x);
  auto left_rectangular_error = std::abs(
    std::atan2(
      (armor_keypoints[3] - armor_keypoints[0]).y, (armor_keypoints[3] - armor_keypoints[0]).x) -
    roll - CV_PI / 2);
  auto right_rectangular_error = std::abs(
    std::atan2(
      (armor_keypoints[2] - armor_keypoints[1]).y, (armor_keypoints[2] - armor_keypoints[1]).x) -
    roll - CV_PI / 2);
  rectangular_error = std::max(left_rectangular_error, right_rectangular_error);

  ratio = max_length / max_width;
  color = color_id == 0 ? Color::blue : color_id == 1 ? Color::red : Color::extinguish;
  name = num_id == 0  ? ArmorName::sentry
         : num_id > 5 ? ArmorName(num_id)
                      : ArmorName(num_id - 1);  //TODO 考虑Bb
  type = num_id == 1 ? ArmorType::big : ArmorType::small;
}

// YOLOV5+ROI构造函数
Armor::Armor(
  int color_id, int num_id, float confidence, const cv::Rect & box,
  std::vector<cv::Point2f> armor_keypoints, cv::Point2f offset)
: confidence(confidence), box(box), points(armor_keypoints)
{
  point_layout = ArmorPointLayout::outer;
  std::transform(
    armor_keypoints.begin(), armor_keypoints.end(), armor_keypoints.begin(),
    [&offset](const cv::Point2f & point) { return point + offset; });
  std::transform(
    points.begin(), points.end(), points.begin(),
    [&offset](const cv::Point2f & point) { return point + offset; });
  center = (armor_keypoints[0] + armor_keypoints[1] + armor_keypoints[2] + armor_keypoints[3]) / 4;
  auto left_width = cv::norm(armor_keypoints[0] - armor_keypoints[3]);
  auto right_width = cv::norm(armor_keypoints[1] - armor_keypoints[2]);
  auto max_width = std::max(left_width, right_width);
  auto top_length = cv::norm(armor_keypoints[0] - armor_keypoints[1]);
  auto bottom_length = cv::norm(armor_keypoints[3] - armor_keypoints[2]);
  auto max_length = std::max(top_length, bottom_length);
  auto left_center = (armor_keypoints[0] + armor_keypoints[3]) / 2;
  auto right_center = (armor_keypoints[1] + armor_keypoints[2]) / 2;
  auto left2right = right_center - left_center;
  auto roll = std::atan2(left2right.y, left2right.x);
  auto left_rectangular_error = std::abs(
    std::atan2(
      (armor_keypoints[3] - armor_keypoints[0]).y, (armor_keypoints[3] - armor_keypoints[0]).x) -
    roll - CV_PI / 2);
  auto right_rectangular_error = std::abs(
    std::atan2(
      (armor_keypoints[2] - armor_keypoints[1]).y, (armor_keypoints[2] - armor_keypoints[1]).x) -
    roll - CV_PI / 2);
  rectangular_error = std::max(left_rectangular_error, right_rectangular_error);

  ratio = max_length / max_width;
  color = color_id == 0 ? Color::blue : color_id == 1 ? Color::red : Color::extinguish;
  name = num_id == 0 ? ArmorName::sentry : num_id > 5 ? ArmorName(num_id) : ArmorName(num_id - 1);
  type = num_id == 1 ? ArmorType::big : ArmorType::small;
}

}  // namespace auto_aim
