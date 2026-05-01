#include <fmt/core.h>

#include <chrono>
#include <opencv2/opencv.hpp>

#include "io/camera.hpp"
#include "io/gimbal/gimbal.hpp"
#include "tasks/auto_aim/detector.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tools/exiter.hpp"
#include "tools/img_tools.hpp"
#include "tools/logger.hpp"
#include "tools/math_tools.hpp"

const std::string keys =
  "{help h usage ? |                         | 输出命令行参数说明 }"
  "{imu-delay-ms d | 1                       | 四元数相对图像时间戳的补偿，单位ms }"
  "{display-scale s| 0.5                     | 显示缩放比例 }"
  "{@config-path   | configs/standard3.yaml  | 位置参数，yaml配置文件路径 }";

int main(int argc, char * argv[])
{
  cv::CommandLineParser cli(argc, argv, keys);
  auto config_path = cli.get<std::string>(0);
  auto imu_delay_ms = cli.get<int>("imu-delay-ms");
  auto display_scale = cli.get<double>("display-scale");
  if (cli.has("help") || config_path.empty()) {
    cli.printMessage();
    return 0;
  }

  tools::Exiter exiter;

  io::Camera camera(config_path);
  io::Gimbal gimbal(config_path);

  auto_aim::Detector detector(config_path, false);
  auto_aim::Solver solver(config_path);

  std::chrono::steady_clock::time_point timestamp;
  std::size_t frame_count = 0;

  while (!exiter.exit()) {
    cv::Mat img;
    camera.read(img, timestamp);
    if (img.empty()) break;

    auto q = gimbal.q(timestamp - std::chrono::milliseconds(imu_delay_ms));
    auto gimbal_state = gimbal.state();
    solver.set_R_gimbal2world(q);

    auto start = std::chrono::steady_clock::now();
    auto armors = detector.detect(img, static_cast<int>(frame_count));
    auto finish = std::chrono::steady_clock::now();
    auto dt = tools::delta_time(finish, start);
    auto fps = dt > 1e-6 ? 1.0 / dt : 0.0;

    auto detection = img.clone();
    auto gimbal_ypr = tools::eulers(q, 2, 1, 0) * 57.3;
    tools::draw_text(
      detection,
      fmt::format(
        "frame:{} fps:{:.1f} yaw:{:.2f} pitch:{:.2f} roll:{:.2f}", frame_count, fps,
        gimbal_ypr[0], gimbal_ypr[1], gimbal_ypr[2]),
      {10, 30}, {255, 255, 255}, 0.9, 2);
    tools::draw_text(
      detection, fmt::format("armors:{} bullet_speed:{:.2f}", armors.size(), gimbal_state.bullet_speed),
      {10, 60}, {255, 255, 255}, 0.9, 2);

    for (auto & armor : armors) {
      solver.solve(armor);

      tools::draw_points(detection, armor.points, {0, 255, 0}, 2);
      auto reprojection =
        solver.reproject_armor(armor.xyz_in_world, armor.ypr_in_world[0], armor.type, armor.name);
      tools::draw_points(detection, reprojection, {0, 0, 255}, 2);

      auto info = fmt::format(
        "{} {} d:{:.2f} yaw:{:.1f}",
        auto_aim::ARMOR_NAMES[armor.name], auto_aim::ARMOR_TYPES[armor.type], armor.ypd_in_world[2],
        armor.ypr_in_world[0] * 57.3);
      tools::draw_text(
        detection, info, cv::Point(static_cast<int>(armor.center.x), static_cast<int>(armor.center.y)),
        {0, 255, 255}, 0.8, 2);
    }

    if (display_scale != 1.0) {
      cv::resize(detection, detection, {}, display_scale, display_scale);
    }

    cv::imshow("better_classic_detect_test", detection);
    auto key = cv::waitKey(1);
    if (key == 'q') break;

    frame_count += 1;
  }

  return 0;
}
