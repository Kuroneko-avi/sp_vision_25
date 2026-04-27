#include <fmt/core.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "io/camera.hpp"
#include "io/gimbal/gimbal.hpp"
#include "tasks/auto_aim/planner/planner.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tasks/auto_aim/tracker.hpp"
#include "tasks/auto_aim/yolo.hpp"
#include "tools/exiter.hpp"
#include "tools/img_tools.hpp"
#include "tools/logger.hpp"
#include "tools/math_tools.hpp"
#include "tools/plotter.hpp"
#include "tools/thread_safe_queue.hpp"

#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
#include "tools/dashboard_params.hpp"
#include "tools/mqtt_bridge.hpp"
#endif

using namespace std::chrono_literals;

const std::string keys =
  "{help h usage ? |                        | 输出命令行参数说明}"
  "{dashboard      |                        | 启用 MQTT Dashboard}"
  "{robot-id       | myrobot                | MQTT Dashboard robot id}"
  "{mqtt-host      | tcp://127.0.0.1:1883   | MQTT broker URI}"
  "{video-source   |                        | 使用视频文件替代真实相机输入}"
  "{video-loop     |                        | 视频结束后回到第一帧}"
  "{mock-runtime   |                        | 无硬件 smoke runtime 模式}"
  "{@config-path   | configs/standard3.yaml | 位置参数，yaml配置文件路径 }";

class VideoFrameSource
{
public:
  explicit VideoFrameSource(const std::string & video_path, bool loop)
  : video_path_(video_path), text_path_(make_text_path(video_path)), loop_(loop), t0_(std::chrono::steady_clock::now())
  {
    video_.open(video_path_);
    if (!video_.isOpened()) {
      throw std::runtime_error("Failed to open video source: " + video_path_);
    }

    text_.open(text_path_);
    has_text_ = text_.is_open();
  }

  bool read(cv::Mat & img, std::chrono::steady_clock::time_point & timestamp, Eigen::Quaterniond & q)
  {
    while (true) {
      video_.read(img);
      if (!img.empty()) break;
      if (!loop_) return false;

      video_.set(cv::CAP_PROP_POS_FRAMES, 0);
      if (has_text_) {
        text_.clear();
        text_.seekg(0);
      }
    }

    double t = frame_index_ / 30.0;
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (has_text_ && !(text_ >> t >> w >> x >> y >> z)) {
      text_.clear();
      if (loop_) text_.seekg(0);
    }

    timestamp = t0_ + std::chrono::microseconds(static_cast<int64_t>(t * 1e6));
    q = Eigen::Quaterniond(w, x, y, z).normalized();
    ++frame_index_;
    return true;
  }

private:
  static std::string make_text_path(const std::string & video_path)
  {
    const auto dot = video_path.find_last_of('.');
    if (dot == std::string::npos) return video_path + ".txt";
    return video_path.substr(0, dot) + ".txt";
  }

  std::string video_path_;
  std::string text_path_;
  bool loop_;
  cv::VideoCapture video_;
  std::ifstream text_;
  bool has_text_{false};
  int frame_index_{0};
  std::chrono::steady_clock::time_point t0_;
};

std::vector<std::string> normalize_cli_args(int argc, char * argv[])
{
  std::vector<std::string> normalized;
  normalized.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "--robot-id" || arg == "--mqtt-host" || arg == "--video-source") &&
        i + 1 < argc) {
      normalized.push_back(arg + "=" + argv[++i]);
    } else {
      normalized.push_back(arg);
    }
  }
  return normalized;
}

std::vector<char *> make_cli_argv(std::vector<std::string> & args)
{
  std::vector<char *> argv;
  argv.reserve(args.size());
  for (auto & arg : args) {
    argv.push_back(arg.data());
  }
  return argv;
}

#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
void publish_dashboard_params(
  tools::MqttBridge & bridge, const tools::dashboard::DashboardParams & dashboard_params)
{
  const auto timestamp = tools::dashboard_unix_timestamp_ms();
  bridge.publish_params_schema_payload(dashboard_params.make_schema());
  bridge.publish_params_current_payload(dashboard_params.make_current(timestamp));
}

void handle_dashboard_commands(
  tools::MqttBridge & bridge, const tools::dashboard::DashboardParams & dashboard_params,
  std::atomic<bool> & telemetry_enabled)
{
  tools::MqttCommand command;
  while (bridge.try_pop_command(command)) {
    if (command.type == tools::MqttCommandType::Param) {
      const auto result = dashboard_params.apply(command.key, command.value);
      if (result.ok) {
        bridge.publish_params_current_payload(
          dashboard_params.make_current(tools::dashboard_unix_timestamp_ms()));
      }
      bridge.publish_ack(command.request_id, result.ok, result.message, result.applied);
      continue;
    }

    if (command.command == "stop_dashboard") {
      telemetry_enabled.store(false);
      bridge.publish_ack(
        command.request_id, true, "dashboard telemetry stopped",
        nlohmann::json{{"command", command.command}});
    } else if (command.command == "start_dashboard") {
      telemetry_enabled.store(true);
      bridge.publish_ack(
        command.request_id, true, "dashboard telemetry started",
        nlohmann::json{{"command", command.command}});
    } else if (command.command == "republish_params") {
      publish_dashboard_params(bridge, dashboard_params);
      bridge.publish_ack(
        command.request_id, true, "dashboard parameters republished",
        nlohmann::json{{"command", command.command}});
    } else {
      bridge.publish_ack(
        command.request_id, false, "unknown dashboard command", nlohmann::json::object());
    }
  }
}
#endif

int main(int argc, char * argv[])
{
  tools::Exiter exiter;
  tools::Plotter plotter;

  auto normalized_args = normalize_cli_args(argc, argv);
  auto normalized_argv = make_cli_argv(normalized_args);
  cv::CommandLineParser cli(
    static_cast<int>(normalized_argv.size()), normalized_argv.data(), keys);
  auto config_path = cli.get<std::string>(0);
  if (cli.has("help") || config_path.empty()) {
    cli.printMessage();
    return 0;
  }
  
  const auto video_source = cli.get<std::string>("video-source");
  const auto video_loop = cli.has("video-loop");
  const auto mock_runtime = cli.has("mock-runtime");

  io::Gimbal gimbal(config_path);
  std::unique_ptr<io::Camera> camera;
  std::unique_ptr<VideoFrameSource> video;
  if (!video_source.empty()) {
    video = std::make_unique<VideoFrameSource>(video_source, video_loop);
    tools::logger()->info("Using video source {}{}", video_source, video_loop ? " with loop" : "");
  } else {
    camera = std::make_unique<io::Camera>(config_path);
  }

  auto_aim::YOLO yolo(config_path, !mock_runtime);
  auto_aim::Solver solver(config_path);
  auto_aim::Tracker tracker(config_path, solver);
  auto_aim::Planner planner(config_path);

#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
  const auto dashboard_enabled = cli.has("dashboard");
  std::unique_ptr<tools::MqttBridge> dashboard_bridge;
  std::unique_ptr<tools::dashboard::DashboardParams> dashboard_params;
  std::atomic<bool> dashboard_telemetry_enabled{dashboard_enabled};
  if (dashboard_enabled) {
    tools::MqttBridgeOptions options;
    options.server_uri = cli.get<std::string>("mqtt-host");
    options.robot_id = cli.get<std::string>("robot-id");
    options.client_id = options.robot_id + "_auto_aim_debug_mpc";

    dashboard_params = std::make_unique<tools::dashboard::DashboardParams>(planner);
    dashboard_bridge = std::make_unique<tools::MqttBridge>(options);
    dashboard_bridge->start();
    publish_dashboard_params(*dashboard_bridge, *dashboard_params);
    tools::logger()->info(
      "MQTT Dashboard enabled for {} at {}", options.robot_id, options.server_uri);
  }
#else
  if (cli.has("dashboard")) {
    tools::logger()->warn("MQTT Dashboard requested but mqtt_bridge was not built");
  }
#endif

  tools::ThreadSafeQueue<std::optional<auto_aim::Target>, true> target_queue(1);
  target_queue.push(std::nullopt);

  std::atomic<bool> quit = false;
  auto plan_thread = std::thread([&]() {
    auto t0 = std::chrono::steady_clock::now();
    uint16_t last_bullet_count = 0;

    while (!quit) {
      auto target = target_queue.front();
      auto gs = gimbal.state();
      auto plan = planner.plan(target, gs.bullet_speed);

      gimbal.send(
        plan.control, plan.fire, plan.yaw, plan.yaw_vel, plan.yaw_acc, plan.pitch, plan.pitch_vel,
        plan.pitch_acc);

      auto fired = gs.bullet_count > last_bullet_count;
      last_bullet_count = gs.bullet_count;

      nlohmann::json data;
      data["t"] = tools::delta_time(std::chrono::steady_clock::now(), t0);

      data["gimbal_yaw"] = gs.yaw;
      data["gimbal_yaw_vel"] = gs.yaw_vel;
      data["gimbal_pitch"] = gs.pitch;
      data["gimbal_pitch_vel"] = gs.pitch_vel;

      data["target_yaw"] = plan.target_yaw;
      data["target_pitch"] = plan.target_pitch;

      data["plan_yaw"] = plan.yaw;
      data["plan_yaw_vel"] = plan.yaw_vel;
      data["plan_yaw_acc"] = plan.yaw_acc;

      data["plan_pitch"] = plan.pitch;
      data["plan_pitch_vel"] = plan.pitch_vel;
      data["plan_pitch_acc"] = plan.pitch_acc;

      data["yaw_error"] = gs.yaw - plan.target_yaw;
      data["pitch_error"] = gs.pitch - plan.target_pitch;

      data["fire"] = plan.fire ? 1 : 0;
      data["fired"] = fired ? 1 : 0;

      if (target.has_value()) {
        data["target_z"] = target->ekf_x()[4];   //z
        data["target_vz"] = target->ekf_x()[5];  //vz
      }

      if (target.has_value()) {
        data["w"] = target->ekf_x()[7];
      } else {
        data["w"] = 0.0;
      }

      plotter.plot(data);
#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
      if (dashboard_bridge && dashboard_telemetry_enabled.load()) {
        dashboard_bridge->push_data(data);
      }
#endif

      std::this_thread::sleep_for(10ms);
    }
  });

  cv::Mat img;
  std::chrono::steady_clock::time_point t;

  while (!exiter.exit()) {
#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
    if (dashboard_bridge && dashboard_params) {
      handle_dashboard_commands(*dashboard_bridge, *dashboard_params, dashboard_telemetry_enabled);
    }
#endif

    Eigen::Quaterniond q;
    if (video) {
      if (!video->read(img, t, q)) break;
    } else {
      camera->read(img, t);
      q = gimbal.q(t-std::chrono::milliseconds(6));
    }

    solver.set_R_gimbal2world(q);
    auto armors = yolo.detect(img);
    auto targets = tracker.track(armors, t);
    if (!targets.empty())
      target_queue.push(targets.front());
    else
      target_queue.push(std::nullopt);

    nlohmann::json data;
    // 装甲板原始观测数据
    data["armor_num"] = armors.size();
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
    plotter.plot(data);
#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
    if (dashboard_bridge && dashboard_telemetry_enabled.load()) {
      dashboard_bridge->push_data(data);
    }
#endif

    if (!targets.empty()) {
      auto target = targets.front();

      // 当前帧target更新后
      std::vector<Eigen::Vector4d> armor_xyza_list = target.armor_xyza_list();
      for (const Eigen::Vector4d & xyza : armor_xyza_list) {
        auto image_points =
          solver.reproject_armor(xyza.head(3), xyza[3], target.armor_type, target.name);
        tools::draw_points(img, image_points, {0, 255, 0});
      }

      Eigen::Vector4d aim_xyza = planner.debug_xyza;
      auto image_points =
        solver.reproject_armor(aim_xyza.head(3), aim_xyza[3], target.armor_type, target.name);
      tools::draw_points(img, image_points, {0, 0, 255});
    }

    if (!mock_runtime) {
      cv::resize(img, img, {}, 0.5, 0.5);  // 显示时缩小图片尺寸
      cv::imshow("reprojection", img);
      auto key = cv::waitKey(1);
      if (key == 'q') break;
    }
  }

  quit = true;
  if (plan_thread.joinable()) plan_thread.join();
#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
  if (dashboard_bridge) {
    dashboard_bridge->stop();
  }
#endif
  gimbal.send(false, false, 0, 0, 0, 0, 0, 0);

  return 0;
}
