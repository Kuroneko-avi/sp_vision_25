#ifndef AUTO_AIM_ESKF__TARGET_HPP
#define AUTO_AIM_ESKF__TARGET_HPP

#include <Eigen/Dense>

#include <array>
#include <chrono>
#include <deque>
#include <optional>
#include <vector>

#include "motion_model_point.hpp"
#include "tasks/auto_aim/armor.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tools/error_state_extended_kalman_filter.hpp"

namespace auto_aim_eskf
{
struct Config
{
  double match_gate_at_1m = 700.0;
  double match_gate_not_all_init_at_1m = 7000.0;
  Eigen::Vector3d qxyz_common {20.0, 20.0, 1.0};
  double qyaw_common = 10.0;
  Eigen::Vector3d qxyz_output {1.0, 1.0, 0.5};
  double qyaw_output = 0.01;
  double q_r = 1e-7;
  double q_l = 1e-7;
  double q_h = 1e-7;
  double q_outpost_dz = 0.5;
  double r_uv_at_1m = 70.0;
  double r_uv_min = 30.0;
  int esekf_iter_num = 5;
};

class Target
{
public:
  using VecX = model::VecX;
  using VecZ = model::VecZ;
  using Filter = tools::ErrorStateExtendedKalmanFilter<model::X_N, model::Z_N, model::Predict, model::Measure>;

  auto_aim::ArmorName name;
  auto_aim::ArmorType armor_type;
  auto_aim::ArmorPriority priority;
  bool jumped;
  int last_id;

  Target() = default;
  Target(
    const auto_aim::Armor & armor, std::chrono::steady_clock::time_point t, const Config & config,
    const auto_aim::Solver & solver);

  void predict(std::chrono::steady_clock::time_point t);
  void predict(double dt);
  void update(std::vector<auto_aim::Armor> & armors);

  VecX ekf_x() const;
  std::vector<Eigen::Vector4d> armor_xyza_list() const;
  bool diverged() const;
  bool convergened();

  const Filter & filter() const;
  static Config load_config(const std::string & config_path);

private:
  static constexpr int OUTPOST_ARMOR_COUNT = 3;

  Config config_;
  int armor_num_;
  int update_count_;
  bool is_converged_;
  int outpost_observed_mask_;
  std::array<bool, OUTPOST_ARMOR_COUNT> outpost_seen_;

  const auto_aim::Solver * solver_;
  model::State state_;
  std::chrono::steady_clock::time_point timestamp_;
  Filter filter_;
  model::Measure::Ctx measure_ctx_;

  std::deque<int> recent_nis_failures_{0};
  std::size_t nis_window_size_ = 100;

  static Eigen::Matrix<double, model::Z_N, 1> measurement_vector(
    const std::vector<cv::Point2f> & points);

  static bool is_balance_target(auto_aim::ArmorType type, auto_aim::ArmorName name);
  bool is_outpost_target() const;
  double initial_radius() const;
  Eigen::DiagonalMatrix<double, model::X_N> initial_covariance() const;
  Eigen::Matrix<double, model::Z_N, model::Z_N> measurement_covariance(const VecZ & z) const;
  Eigen::Matrix<double, model::X_N, model::X_N> process_noise(double dt) const;
  std::vector<std::pair<int, auto_aim::Armor>> match(std::vector<auto_aim::Armor> & armors) const;
  void mark_outpost_seen(int id);
};

}  // namespace auto_aim_eskf

#endif  // AUTO_AIM_ESKF__TARGET_HPP
