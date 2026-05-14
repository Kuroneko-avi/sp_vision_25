#include <fmt/core.h>

#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "io/camera.hpp"
#include "io/gimbal/gimbal.hpp"
#include "src/auto_aim_debug_dashboard.hpp"
#include "tasks/auto_aim/planner/planner.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tasks/auto_aim/yolo.hpp"
#include "tasks/auto_aim_eskf/tracker.hpp"
#include "tools/dashboard_cli.hpp"
#include "tools/dashboard_config.hpp"
#include "tools/exiter.hpp"
#include "tools/img_tools.hpp"
#include "tools/math_tools.hpp"
#include "tools/plotter.hpp"
#include "tools/thread_safe_queue.hpp"

using namespace std::chrono_literals;

const std::string keys =
  "{help h usage ? |                        | output command line help }"
  "{dashboard      |                        | enable MQTT Dashboard}"
  "{robot-id       | myrobot                | MQTT Dashboard robot id}"
  "{mqtt-host      | tcp://127.0.0.1:1883   | MQTT broker URI}"
  "{@config-path   | configs/standard3.yaml | yaml config path }"
  "{imu-delay-ms   | 6.0                    | IMU delay in milliseconds }";

int main(int argc, char * argv[])
{
  tools::Exiter exiter;

  auto normalized_args = tools::dashboard::cli::normalize_cli_args(argc, argv);
  auto normalized_argv = tools::dashboard::cli::make_cli_argv(normalized_args);
  cv::CommandLineParser cli(
    static_cast<int>(normalized_argv.size()), normalized_argv.data(), keys);
  auto config_path = cli.get<std::string>(0);
  auto imu_delay_ms = cli.get<double>("imu-delay-ms");
  if (cli.has("help") || config_path.empty()) {
    cli.printMessage();
    return 0;
  }
  const auto dashboard_config = tools::dashboard::load_dashboard_config(
    config_path,
    tools::dashboard::cli::make_dashboard_overrides(normalized_args, cli.has("dashboard")));
  auto plotter = tools::Plotter::from_config(config_path);

  io::Gimbal gimbal(config_path);
  io::Camera camera(config_path);

  auto_aim::YOLO yolo(config_path, true);
  auto_aim::Solver solver(config_path);
  auto_aim_eskf::Tracker tracker(config_path, solver);
  auto_aim::Planner planner(config_path);
  AutoAimDebugDashboard dashboard(dashboard_config, config_path, planner);

  tools::ThreadSafeQueue<std::optional<auto_aim_eskf::Target>, true> target_queue(1);
  target_queue.push(std::nullopt);

  std::atomic<bool> quit = false;
  auto plan_thread = std::thread([&]() {
    while (!quit) {
      std::optional<auto_aim_eskf::Target> target;
      if (!target_queue.front(target)) break;
      auto gs = gimbal.state();
      auto plan = planner.plan(target, gs.bullet_speed);
      gimbal.send(
        plan.control, plan.fire, plan.yaw, plan.yaw_vel, plan.yaw_acc, plan.pitch, plan.pitch_vel,
        plan.pitch_acc);
      std::this_thread::sleep_for(10ms);
    }
  });

  cv::Mat img;
  std::chrono::steady_clock::time_point t;
  auto imu_delay = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double, std::milli>(imu_delay_ms));

  while (!exiter.exit()) {
    dashboard.handle_commands();

    camera.read(img, t);
    Eigen::Quaterniond q = gimbal.q(t - imu_delay);

    solver.set_R_gimbal2world(q);
    auto armors = yolo.detect(img);
    auto targets = tracker.track(armors, t);
    if (!targets.empty()) {
      target_queue.push(targets.front());
    } else {
      target_queue.push(std::nullopt);
    }

    nlohmann::json data;
    data["armor_num"] = armors.size();
    data["imu_delay_ms"] = imu_delay_ms;
    plotter.plot(data);
    dashboard.push_data(data);

    if (!targets.empty()) {
      auto target = targets.front();
      for (const auto & xyza : target.armor_xyza_list()) {
        auto image_points =
          solver.reproject_armor(xyza.head(3), xyza[3], target.armor_type, target.name);
        tools::draw_points(img, image_points, {0, 255, 0});
      }

      Eigen::Vector4d aim_xyza = planner.debug_xyza;
      auto image_points =
        solver.reproject_armor(aim_xyza.head(3), aim_xyza[3], target.armor_type, target.name);
      tools::draw_points(img, image_points, {0, 0, 255});
    }

    tools::draw_text(
      img, fmt::format("imu delay {:.2f} ms | eskf", imu_delay_ms), cv::Point(30, 40));
    cv::resize(img, img, {}, 0.5, 0.5);
    cv::imshow("reprojection_eskf", img);
    auto key = cv::waitKey(1);
    if (key == 'q') break;
  }

  quit = true;
  if (plan_thread.joinable()) plan_thread.join();
  dashboard.stop();
  gimbal.send(false, false, 0, 0, 0, 0, 0, 0);

  return 0;
}
