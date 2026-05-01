#include <fmt/core.h>

#include <chrono>
#include <cmath>
#include <opencv2/opencv.hpp>

#include "io/camera.hpp"
#include "io/gimbal/gimbal.hpp"
#include "tasks/auto_aim/detector.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tasks/auto_aim/tracker.hpp"
#include "tools/exiter.hpp"
#include "tools/img_tools.hpp"
#include "tools/logger.hpp"
#include "tools/math_tools.hpp"
#include "tools/plotter.hpp"

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
  tools::Plotter plotter;

  io::Camera camera(config_path);
  io::Gimbal gimbal(config_path);

  auto_aim::Detector detector(config_path, false);
  auto_aim::Solver solver(config_path);
  auto_aim::Tracker tracker(config_path, solver);

  std::chrono::steady_clock::time_point timestamp;
  std::size_t frame_count = 0;
  auto t0 = std::chrono::steady_clock::now();

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
    auto tracked_armors = armors;
    auto targets = tracker.track(tracked_armors, timestamp);

    auto detection = img.clone();
    auto gimbal_ypr = tools::eulers(q, 2, 1, 0) * 57.3;
    tools::draw_text(
      detection,
      fmt::format(
        "frame:{} fps:{:.1f} yaw:{:.2f} pitch:{:.2f} roll:{:.2f}", frame_count, fps,
        gimbal_ypr[0], gimbal_ypr[1], gimbal_ypr[2]),
      {10, 30}, {255, 255, 255}, 0.9, 2);
    tools::draw_text(
      detection,
      fmt::format(
        "armors:{} targets:{} bullet_speed:{:.2f} [{}]", armors.size(), targets.size(),
        gimbal_state.bullet_speed, tracker.state()),
      {10, 60}, {255, 255, 255}, 0.9, 2);

    nlohmann::json data;
    data["t"] = tools::delta_time(std::chrono::steady_clock::now(), t0);
    data["fps"] = fps;
    data["armor_num"] = armors.size();
    data["target_num"] = targets.size();
    data["gimbal_yaw"] = gimbal_ypr[0];
    data["gimbal_pitch"] = gimbal_ypr[1];
    data["gimbal_roll"] = gimbal_ypr[2];
    data["gimbal_yaw_state"] = gimbal_state.yaw * 57.3;
    data["gimbal_pitch_state"] = gimbal_state.pitch * 57.3;
    data["bullet_speed"] = gimbal_state.bullet_speed;

    for (auto & armor : armors) {
      solver.solve(armor);

      tools::draw_points(detection, armor.points, {0, 255, 0}, 2);
      auto reprojection = solver.reproject_armor(armor);
      tools::draw_points(detection, reprojection, {0, 0, 255}, 2);

      auto info = fmt::format(
        "{} {} d:{:.2f} yaw:{:.1f}",
        auto_aim::ARMOR_NAMES[armor.name], auto_aim::ARMOR_TYPES[armor.type], armor.ypd_in_world[2],
        armor.ypr_in_world[0] * 57.3);
      tools::draw_text(
        detection, info, cv::Point(static_cast<int>(armor.center.x), static_cast<int>(armor.center.y)),
        {0, 255, 255}, 0.8, 2);
    }

    if (!armors.empty()) {
      const auto & armor = armors.front();
      data["armor_x"] = armor.xyz_in_world[0];
      data["armor_y"] = armor.xyz_in_world[1];
      data["armor_z"] = armor.xyz_in_world[2];
      data["armor_yaw"] = armor.ypr_in_world[0] * 57.3;
      data["armor_yaw_raw"] = armor.yaw_raw * 57.3;
      data["armor_center_x"] = armor.center_norm.x;
      data["armor_center_y"] = armor.center_norm.y;
    }

    if (!targets.empty()) {
      const auto & target = targets.front();

      for (const Eigen::Vector4d & xyza : target.armor_xyza_list()) {
        auto image_points =
          solver.reproject_armor(xyza.head(3), xyza[3], target.armor_type, target.name);
        tools::draw_points(detection, image_points, {255, 0, 0}, 2);
      }

      Eigen::VectorXd x = target.ekf_x();
      data["x"] = x[0];
      data["vx"] = x[1];
      data["y"] = x[2];
      data["vy"] = x[3];
      data["z"] = x[4];
      data["vz"] = x[5];
      data["a"] = x[6] * 57.3;
      data["w"] = x[7];
      data["r"] = x[8];
      data["l"] = x[9];
      data["h"] = x[10];
      data["last_id"] = target.last_id;
      data["distance"] = std::sqrt(x[0] * x[0] + x[2] * x[2] + x[4] * x[4]);

      data["residual_yaw"] = target.ekf().data.at("residual_yaw");
      data["residual_pitch"] = target.ekf().data.at("residual_pitch");
      data["residual_distance"] = target.ekf().data.at("residual_distance");
      data["residual_angle"] = target.ekf().data.at("residual_angle");
      data["nis"] = target.ekf().data.at("nis");
      data["nees"] = target.ekf().data.at("nees");
      data["nis_fail"] = target.ekf().data.at("nis_fail");
      data["nees_fail"] = target.ekf().data.at("nees_fail");
      data["recent_nis_failures"] = target.ekf().data.at("recent_nis_failures");
    }

    plotter.plot(data);

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
