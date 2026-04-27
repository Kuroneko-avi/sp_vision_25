#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <optional>
#include <thread>
#include <vector>

#include "io/camera.hpp"
#include "io/dm_imu/dm_imu.hpp"
#include "tasks/auto_aim/aimer.hpp"
#include "tasks/auto_aim/multithread/commandgener.hpp"
#include "tasks/auto_aim/multithread/mt_detector.hpp"
#include "tasks/auto_aim/shooter.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tasks/auto_aim/tracker.hpp"
#include "tasks/auto_buff/buff_aimer.hpp"
#include "tasks/auto_buff/buff_detector.hpp"
#include "tasks/auto_buff/buff_solver.hpp"
#include "tasks/auto_buff/buff_target.hpp"
#include "tasks/auto_buff/buff_type.hpp"
#include "tools/exiter.hpp"
#include "tools/dashboard_config.hpp"
#include "tools/img_tools.hpp"
#include "tools/logger.hpp"
#include "tools/math_tools.hpp"
#include "tools/plotter.hpp"
#include "tools/recorder.hpp"

#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
#include "tools/dashboard_params.hpp"
#include "tools/mqtt_bridge.hpp"
#endif

const std::string keys =
  "{help h usage ? | | 输出命令行参数说明}"
  "{dashboard      |                        | 启用 MQTT Dashboard}"
  "{robot-id       | myrobot                | MQTT Dashboard robot id}"
  "{mqtt-host      | tcp://127.0.0.1:1883   | MQTT broker URI}"
  "{@config-path   | configs/standard3.yaml | yaml配置文件路径 }";

using namespace std::chrono_literals;

std::vector<std::string> normalize_cli_args(int argc, char * argv[])
{
  std::vector<std::string> normalized;
  normalized.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "--robot-id" || arg == "--mqtt-host") && i + 1 < argc) {
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

std::optional<std::string> cli_option_value(
  const std::vector<std::string> & args, const std::string & option)
{
  const auto prefix = option + "=";
  for (const auto & arg : args) {
    if (arg.rfind(prefix, 0) == 0) {
      return arg.substr(prefix.size());
    }
  }
  return std::nullopt;
}

tools::dashboard::DashboardConfigOverrides make_dashboard_overrides(
  const std::vector<std::string> & args, bool force_enabled)
{
  tools::dashboard::DashboardConfigOverrides overrides;
  overrides.force_enabled = force_enabled;
  overrides.robot_id = cli_option_value(args, "--robot-id");
  overrides.mqtt_host = cli_option_value(args, "--mqtt-host");
  return overrides;
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
  auto normalized_args = normalize_cli_args(argc, argv);
  auto normalized_argv = make_cli_argv(normalized_args);
  cv::CommandLineParser cli(
    static_cast<int>(normalized_argv.size()), normalized_argv.data(), keys);
  auto config_path = cli.get<std::string>("@config-path");
  if (cli.has("help") || !cli.has("@config-path")) {
    cli.printMessage();
    return 0;
  }
  const auto dashboard_config = tools::dashboard::load_dashboard_config(
    config_path, make_dashboard_overrides(normalized_args, cli.has("dashboard")));

  tools::Exiter exiter;
  tools::Plotter plotter;
  tools::Recorder recorder;

  io::Gimbal gimbal(config_path);
  io::Camera camera(config_path);

  auto_aim::YOLO yolo(config_path, true);
  auto_aim::Solver solver(config_path);
  auto_aim::Tracker tracker(config_path, solver);
  auto_aim::Planner planner(config_path);

  tools::ThreadSafeQueue<std::optional<auto_aim::Target>, true> target_queue(1);
  target_queue.push(std::nullopt);

  auto_buff::Buff_Detector buff_detector(config_path);
  auto_buff::Solver buff_solver(config_path);
  auto_buff::SmallTarget buff_small_target;
  auto_buff::BigTarget buff_big_target;
  auto_buff::Aimer buff_aimer(config_path);

#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
  const auto dashboard_enabled = dashboard_config.enabled;
  std::unique_ptr<tools::MqttBridge> dashboard_bridge;
  std::unique_ptr<tools::dashboard::DashboardParams> dashboard_params;
  std::atomic<bool> dashboard_telemetry_enabled{dashboard_enabled};
  if (dashboard_enabled) {
    try {
      tools::MqttBridgeOptions options;
      options.server_uri = dashboard_config.mqtt_host;
      options.robot_id = dashboard_config.robot_id;
      options.client_id = options.robot_id + "_standard_mpc";

      auto next_params = std::make_unique<tools::dashboard::DashboardParams>(planner, buff_aimer);
      auto next_bridge = std::make_unique<tools::MqttBridge>(options);
      next_bridge->start();
      publish_dashboard_params(*next_bridge, *next_params);
      dashboard_params = std::move(next_params);
      dashboard_bridge = std::move(next_bridge);
      tools::logger()->info(
        "MQTT Dashboard enabled for {} at {}", options.robot_id, options.server_uri);
    } catch (const std::exception & e) {
      dashboard_telemetry_enabled.store(false);
      tools::logger()->warn("MQTT Dashboard disabled: {}", e.what());
    } catch (...) {
      dashboard_telemetry_enabled.store(false);
      tools::logger()->warn("MQTT Dashboard disabled: unknown initialization error");
    }
  }
#else
  if (dashboard_config.enabled) {
    tools::logger()->warn("MQTT Dashboard requested but mqtt_bridge was not built");
  }
#endif

  cv::Mat img;
  Eigen::Quaterniond q;
  std::chrono::steady_clock::time_point t;

  std::atomic<bool> quit = false;

  std::atomic<io::GimbalMode> mode{io::GimbalMode::IDLE};
  auto last_mode{io::GimbalMode::IDLE};

  auto plan_thread = std::thread([&]() {
    auto t0 = std::chrono::steady_clock::now();
    uint16_t last_bullet_count = 0;

    while (!quit) {
      if (!target_queue.empty() && mode == io::GimbalMode::AUTO_AIM) {
        auto target = target_queue.front();
        auto gs = gimbal.state();
        auto plan = planner.plan(target, gs.bullet_speed);

        gimbal.send(
          plan.control, plan.fire, plan.yaw, plan.yaw_vel, plan.yaw_acc, plan.pitch, plan.pitch_vel,
          plan.pitch_acc);

#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
        if (dashboard_bridge && dashboard_telemetry_enabled.load()) {
          dashboard_bridge->push_data(nlohmann::json{
            {"mode", "auto_aim"},
            {"target_found", target.has_value()},
            {"gimbal_yaw", gs.yaw},
            {"gimbal_yaw_vel", gs.yaw_vel},
            {"gimbal_pitch", gs.pitch},
            {"gimbal_pitch_vel", gs.pitch_vel},
            {"target_yaw", plan.target_yaw},
            {"target_pitch", plan.target_pitch},
            {"plan_yaw", plan.yaw},
            {"plan_yaw_vel", plan.yaw_vel},
            {"plan_yaw_acc", plan.yaw_acc},
            {"plan_pitch", plan.pitch},
            {"plan_pitch_vel", plan.pitch_vel},
            {"plan_pitch_acc", plan.pitch_acc},
            {"fire", plan.fire}});
        }
#endif

        std::this_thread::sleep_for(10ms);
      } else
        std::this_thread::sleep_for(200ms);
    }
  });

  while (!exiter.exit()) {
#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
    if (dashboard_bridge && dashboard_params) {
      handle_dashboard_commands(*dashboard_bridge, *dashboard_params, dashboard_telemetry_enabled);
    }
#endif

    mode = gimbal.mode();

    if (last_mode != mode) {
      tools::logger()->info("Switch to {}", gimbal.str(mode));
      last_mode = mode.load();
    }

    camera.read(img, t);
    auto q = gimbal.q(t-std::chrono::milliseconds(6));
    auto gs = gimbal.state();
    recorder.record(img, q, t);
    solver.set_R_gimbal2world(q);

    /// 自瞄
    if (mode.load() == io::GimbalMode::AUTO_AIM) {
      auto armors = yolo.detect(img);
      auto targets = tracker.track(armors, t);
      if (!targets.empty())
        target_queue.push(targets.front());
      else
        target_queue.push(std::nullopt);
    }

    /// 打符
    else if (mode.load() == io::GimbalMode::SMALL_BUFF || mode.load() == io::GimbalMode::BIG_BUFF) {
      buff_solver.set_R_gimbal2world(q);

      auto power_runes = buff_detector.detect(img);

      buff_solver.solve(power_runes);

      auto_aim::Plan buff_plan;
      if (mode.load() == io::GimbalMode::SMALL_BUFF) {
        buff_small_target.get_target(power_runes, t);
        auto target_copy = buff_small_target;
        buff_plan = buff_aimer.mpc_aim(target_copy, t, gs, true);
      } else if (mode.load() == io::GimbalMode::BIG_BUFF) {
        buff_big_target.get_target(power_runes, t);
        auto target_copy = buff_big_target;
        buff_plan = buff_aimer.mpc_aim(target_copy, t, gs, true);
      }
      gimbal.send(
        buff_plan.control, buff_plan.fire, buff_plan.yaw, buff_plan.yaw_vel, buff_plan.yaw_acc,
        buff_plan.pitch, buff_plan.pitch_vel, buff_plan.pitch_acc);

#ifdef SP_VISION_ENABLE_DASHBOARD_MQTT
      if (dashboard_bridge && dashboard_telemetry_enabled.load()) {
        dashboard_bridge->push_data(nlohmann::json{
          {"mode", mode.load() == io::GimbalMode::SMALL_BUFF ? "small_buff" : "big_buff"},
          {"gimbal_yaw", gs.yaw},
          {"gimbal_yaw_vel", gs.yaw_vel},
          {"gimbal_pitch", gs.pitch},
          {"gimbal_pitch_vel", gs.pitch_vel},
          {"plan_yaw", buff_plan.yaw},
          {"plan_yaw_vel", buff_plan.yaw_vel},
          {"plan_yaw_acc", buff_plan.yaw_acc},
          {"plan_pitch", buff_plan.pitch},
          {"plan_pitch_vel", buff_plan.pitch_vel},
          {"plan_pitch_acc", buff_plan.pitch_acc},
          {"fire", buff_plan.fire}});
      }
#endif

    } else
      gimbal.send(false, false, 0, 0, 0, 0, 0, 0);
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
