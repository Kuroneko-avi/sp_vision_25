#include <fmt/core.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>

#include "io/camera.hpp"
#include "io/gimbal/gimbal.hpp"
#include "src/auto_aim_debug_utils.hpp"
#include "tasks/auto_aim/planner/planner.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tasks/auto_aim/tracker.hpp"
#include "tasks/auto_aim/yolos/yolov5_trt.hpp"
#include "tools/exiter.hpp"
#include "tools/img_tools.hpp"
#include "tools/logger.hpp"
#include "tools/math_tools.hpp"
#include "tools/plotter.hpp"
#include "tools/thread_safe_queue.hpp"

using namespace std::chrono_literals;

namespace
{
constexpr auto kDefaultImuDelay = std::chrono::milliseconds(8);
constexpr auto kStaleTargetTimeout = std::chrono::milliseconds(120);

struct TimedTarget
{
  std::optional<auto_aim::Target> target;
  std::chrono::steady_clock::time_point timestamp;
};
}  // namespace

const std::string keys =
  "{help h usage ? |                        | 输出命令行参数说明}"
  "{@config-path   | configs/standard3_trt.yaml | 位置参数，yaml配置文件路径 }";

int main(int argc, char * argv[])
{
  tools::Exiter exiter;

  cv::CommandLineParser cli(argc, argv, keys);
  auto config_path = cli.get<std::string>(0);
  if (cli.has("help") || config_path.empty()) {
    cli.printMessage();
    return 0;
  }

  auto plotter = tools::Plotter::from_config(config_path);
  io::Gimbal gimbal(config_path);
  io::Camera camera(config_path);

  auto_aim::YOLOV5TRT yolo(config_path, true);
  auto_aim::Solver solver(config_path);
  auto_aim::Tracker tracker(config_path, solver);
  auto_aim::Planner planner(config_path);

  tools::ThreadSafeQueue<TimedTarget, true> target_queue(1);
  target_queue.push({std::nullopt, std::chrono::steady_clock::now()});
  std::mutex aim_mutex;
  std::optional<Eigen::Vector4d> latest_aim_xyza;

  std::atomic<bool> quit = false;
  auto plan_thread = std::thread([&]() {
    auto t0 = std::chrono::steady_clock::now();
    uint16_t last_bullet_count = 0;

    while (!quit) {
      TimedTarget timed_target;
      if (!target_queue.front(timed_target)) {
        break;
      }

      auto target = timed_target.target;
      const auto now = std::chrono::steady_clock::now();
      const auto target_age =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - timed_target.timestamp);
      if (target_age > kStaleTargetTimeout) {
        target = std::nullopt;
      }

      auto gs = gimbal.state();
      auto plan = planner.plan(target, gs.bullet_speed);

      gimbal.send(
        plan.control, plan.fire, plan.yaw, plan.yaw_vel, plan.yaw_acc, plan.pitch, plan.pitch_vel,
        plan.pitch_acc);

      {
        std::lock_guard<std::mutex> lock(aim_mutex);
        if (plan.control && target.has_value()) {
          latest_aim_xyza = planner.debug_xyza;
        } else {
          latest_aim_xyza.reset();
        }
      }

      auto fired = gs.bullet_count > last_bullet_count;
      last_bullet_count = gs.bullet_count;

      nlohmann::json data;
      data["t"] = tools::delta_time(now, t0);

      data["gimbal_yaw"] = gs.yaw;
      data["gimbal_yaw_vel"] = gs.yaw_vel;
      data["gimbal_pitch"] = gs.pitch;
      data["gimbal_pitch_vel"] = gs.pitch_vel;

      data["target_yaw"] = plan.target_yaw;
      data["target_pitch_raw"] = plan.target_pitch;
      data["target_pitch"] = auto_aim::debug::plot_pitch(plan.target_pitch);

      data["plan_yaw"] = plan.yaw;
      data["plan_yaw_vel"] = plan.yaw_vel;
      data["plan_yaw_acc"] = plan.yaw_acc;

      data["plan_pitch_raw"] = plan.pitch;
      data["plan_pitch"] = auto_aim::debug::plot_pitch(plan.pitch);
      data["plan_pitch_vel_raw"] = plan.pitch_vel;
      data["plan_pitch_vel"] = auto_aim::debug::plot_pitch(plan.pitch_vel);
      data["plan_pitch_acc_raw"] = plan.pitch_acc;
      data["plan_pitch_acc"] = auto_aim::debug::plot_pitch(plan.pitch_acc);

      data["yaw_error"] = gs.yaw - plan.target_yaw;
      data["pitch_error"] = auto_aim::debug::plot_pitch_error(gs.pitch, plan.target_pitch);

      data["fire"] = plan.fire ? 1 : 0;
      data["fired"] = fired ? 1 : 0;
      data["target_age_ms"] = target_age.count();

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

      std::this_thread::sleep_for(10ms);
    }
  });

  cv::Mat img;
  std::chrono::steady_clock::time_point t;
  auto last_loop_time = std::chrono::steady_clock::now();

  while (!exiter.exit()) {
    camera.read(img, t);
    auto q = gimbal.q(t - kDefaultImuDelay);
    const auto loop_now = std::chrono::steady_clock::now();
    const double frame_age_ms = tools::delta_time(loop_now, t) * 1e3;
    const double loop_dt_ms = tools::delta_time(loop_now, last_loop_time) * 1e3;
    last_loop_time = loop_now;

    solver.set_R_gimbal2world(q);
    auto armors = yolo.detect(img, -1);
    const auto & profile = yolo.last_profile();
    auto targets = tracker.track(armors, t);
    if (!targets.empty())
      target_queue.push({targets.front(), t});
    else
      target_queue.push({std::nullopt, t});

    nlohmann::json data;
    // 装甲板原始观测数据
    data["armor_num"] = armors.size();
    data["frame_age_ms"] = frame_age_ms;
    data["loop_dt_ms"] = loop_dt_ms;
    if (profile.valid) {
      data["trt_preprocess_ms"] = profile.preprocess_ms;
      data["trt_pack_ms"] = profile.input_pack_ms;
      data["trt_h2d_ms"] = profile.h2d_ms;
      data["trt_gpu_ms"] = profile.gpu_compute_ms;
      data["trt_d2h_ms"] = profile.d2h_ms;
      data["trt_infer_ms"] = profile.infer_ms;
      data["trt_postprocess_ms"] = profile.postprocess_ms;
      data["trt_total_ms"] = profile.total_ms;
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
    plotter.plot(data);

    if (!targets.empty()) {
      auto target = targets.front();

      // 当前帧target更新后
      std::vector<Eigen::Vector4d> armor_xyza_list = target.armor_xyza_list();
      for (const Eigen::Vector4d & xyza : armor_xyza_list) {
        auto image_points =
          solver.reproject_armor(xyza.head(3), xyza[3], target.armor_type, target.name);
        tools::draw_points(img, image_points, {0, 255, 0});
      }

      std::optional<Eigen::Vector4d> aim_xyza;
      {
        std::lock_guard<std::mutex> lock(aim_mutex);
        aim_xyza = latest_aim_xyza;
      }
      if (aim_xyza.has_value()) {
        auto image_points = solver.reproject_armor(
          aim_xyza->head(3), (*aim_xyza)[3], target.armor_type, target.name);
        tools::draw_points(img, image_points, {0, 0, 255});
      }
    }

    tools::draw_text(
      img, fmt::format("[{}] age {:.1f} ms loop {:.1f} ms", tracker.state(), frame_age_ms, loop_dt_ms),
      {10, 30}, {255, 255, 255});
    if (profile.valid) {
      tools::draw_text(
        img,
        fmt::format(
          "trt total {:.1f} ms gpu {:.1f} ms armors {}", profile.total_ms,
          profile.gpu_compute_ms, armors.size()),
        {10, 60}, {255, 255, 0});
    }

    cv::resize(img, img, {}, 0.5, 0.5);  // 显示时缩小图片尺寸
    cv::imshow("reprojection", img);
    auto key = cv::waitKey(1);
    if (key == 'q') break;
  }

  quit = true;
  if (plan_thread.joinable()) plan_thread.join();
  gimbal.send(false, false, 0, 0, 0, 0, 0, 0);

  return 0;
}
