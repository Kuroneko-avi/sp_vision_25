#include "target.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

#include "tools/logger.hpp"
#include "tools/math_tools.hpp"

namespace auto_aim_eskf
{
namespace
{
constexpr double MIN_RADIUS = 0.05;
constexpr double MAX_RADIUS = 0.5;
constexpr double MAX_HEIGHT_DELTA = 0.5;
constexpr double MAX_VYAW = 20.0;
}  // namespace

Config Target::load_config(const std::string & config_path)
{
  Config config;
  auto yaml = YAML::LoadFile(config_path);
  if (!yaml["auto_aim_eskf"]) return config;

  const auto & node = yaml["auto_aim_eskf"];
  config.match_gate_at_1m = node["match_gate_at_1m"].as<double>();
  config.match_gate_not_all_init_at_1m = node["match_gate_not_all_init_at_1m"].as<double>();
  config.qxyz_common = Eigen::Vector3d(node["qxyz_common"][0].as<double>(), node["qxyz_common"][1].as<double>(), node["qxyz_common"][2].as<double>());
  config.qyaw_common = node["qyaw_common"].as<double>();
  config.qxyz_output = Eigen::Vector3d(node["qxyz_output"][0].as<double>(), node["qxyz_output"][1].as<double>(), node["qxyz_output"][2].as<double>());
  config.qyaw_output = node["qyaw_output"].as<double>();
  config.q_r = node["q_r"].as<double>();
  config.q_l = node["q_l"].as<double>();
  config.q_h = node["q_h"].as<double>();
  config.q_outpost_dz = node["q_outpost_dz"].as<double>();
  config.r_uv_at_1m = node["r_uv_at_1m"].as<double>();
  config.r_uv_min = node["r_uv_min"].as<double>();
  config.esekf_iter_num = node["esekf_iter_num"].as<int>();
  return config;
}

Target::Target(
  const auto_aim::Armor & armor, std::chrono::steady_clock::time_point t, const Config & config,
  const auto_aim::Solver & solver)
: name(armor.name),
  armor_type(armor.type),
  priority(armor.priority),
  jumped(false),
  last_id(0),
  config_(config),
  armor_num_(model::armor_num_by_name(armor.name)),
  update_count_(1),
  is_converged_(false),
  outpost_observed_mask_(0),
  outpost_seen_{false, false, false},
  solver_(&solver),
  timestamp_(t)
{
  measure_ctx_.armor_num = armor_num_;
  measure_ctx_.id = 0;
  measure_ctx_.armor_type = armor.type;
  measure_ctx_.armor_name = armor.name;
  measure_ctx_.solver = &solver;

  const double yaw = armor.ypr_in_world[0];
  const double r = initial_radius();
  state_.x.setZero();
  state_.x << armor.xyz_in_world[0] + r * std::cos(yaw), 0.0, armor.xyz_in_world[1] + r * std::sin(yaw),
    0.0, armor.xyz_in_world[2], 0.0, yaw, 0.0, r, 0.0, 0.0;

  auto q_func = [this]() {
    Eigen::Matrix<double, model::X_N, model::X_N> q;
    q.setZero();
    return q;
  };
  auto r_func = [this](const Eigen::Matrix<double, model::Z_N, 1> & z) {
    return measurement_covariance(z);
  };

  model::Predict predict_model;
  predict_model.dt = 0.005;
  predict_model.armor_name = name;
  model::Measure measure_model;
  measure_model.ctx = measure_ctx_;
  filter_ = Filter(predict_model, measure_model, q_func, r_func, initial_covariance());
  filter_.setResidualFunc(
    [](const Eigen::Matrix<double, model::Z_N, 1> & z_pred,
       const Eigen::Matrix<double, model::Z_N, 1> & z_meas) { return z_meas - z_pred; });
  filter_.setInjectFunc(
    [](const Eigen::Matrix<double, model::X_N, 1> & delta, Eigen::Matrix<double, model::X_N, 1> & nominal) {
      nominal += delta;
      nominal[model::idx::YAW] = tools::limit_rad(nominal[model::idx::YAW]);
    });
  filter_.setIterationNum(config_.esekf_iter_num);
  filter_.setState(state_.x);

  if (is_outpost_target()) {
    mark_outpost_seen(0);
  }
}

bool Target::is_balance_target(auto_aim::ArmorType type, auto_aim::ArmorName name)
{
  return model::is_balance_target(type, name);
}

bool Target::is_outpost_target() const { return name == auto_aim::ArmorName::outpost; }

double Target::initial_radius() const
{
  if (is_outpost_target()) return 0.2765;
  if (name == auto_aim::ArmorName::base) return 0.3205;
  if (is_balance_target(armor_type, name)) return 0.2;
  return 0.26;
}

Eigen::DiagonalMatrix<double, model::X_N> Target::initial_covariance() const
{
  Eigen::DiagonalMatrix<double, model::X_N> p0;
  if (is_outpost_target()) {
    p0.diagonal() << 1, 64, 1, 64, 1, 81, 0.4, 100, 1e-4, 0.1, 0.1;
  } else if (name == auto_aim::ArmorName::base) {
    p0.diagonal() << 1, 64, 1, 64, 1, 64, 0.4, 100, 1e-4, 0, 0;
  } else {
    p0.diagonal() << 1, 64, 1, 64, 1, 64, 0.4, 100, 1, 1, 1;
  }
  return p0;
}

Eigen::Matrix<double, model::Z_N, 1> Target::measurement_vector(const std::vector<cv::Point2f> & points)
{
  Eigen::Matrix<double, model::Z_N, 1> z;
  z.setZero();
  if (points.size() != 4) return z;

  for (int i = 0; i < 4; ++i) {
    z[2 * i] = points[i].x;
    z[2 * i + 1] = points[i].y;
  }
  return z;
}

Eigen::Matrix<double, model::Z_N, model::Z_N> Target::measurement_covariance(const VecZ & z) const
{
  (void)z;
  const double u_r =
    std::max(config_.r_uv_at_1m * std::log((1.0 / std::max(state_.pos().norm(), 1e-6)) + 1.0), config_.r_uv_min);
  return Eigen::Matrix<double, model::Z_N, model::Z_N>::Identity() * u_r;
}

Eigen::Matrix<double, model::X_N, model::X_N> Target::process_noise(double dt) const
{
  Eigen::Matrix<double, model::X_N, model::X_N> q;
  q.setZero();

  const bool outpost = is_outpost_target();
  const Eigen::Vector3d qxyz = outpost ? config_.qxyz_output : config_.qxyz_common;
  const double qyaw = outpost ? config_.qyaw_output : config_.qyaw_common;
  const double q_l = outpost ? config_.q_outpost_dz : config_.q_l;
  const double q_h = outpost ? config_.q_outpost_dz : config_.q_h;

  const double a = std::pow(dt, 4) / 4.0;
  const double b = std::pow(dt, 3) / 2.0;
  const double c = std::pow(dt, 2);

  q(model::idx::CX, model::idx::CX) = a * qxyz.x();
  q(model::idx::CX, model::idx::VCX) = b * qxyz.x();
  q(model::idx::VCX, model::idx::CX) = b * qxyz.x();
  q(model::idx::VCX, model::idx::VCX) = c * qxyz.x();

  q(model::idx::CY, model::idx::CY) = a * qxyz.y();
  q(model::idx::CY, model::idx::VCY) = b * qxyz.y();
  q(model::idx::VCY, model::idx::CY) = b * qxyz.y();
  q(model::idx::VCY, model::idx::VCY) = c * qxyz.y();

  q(model::idx::CZ, model::idx::CZ) = a * qxyz.z();
  q(model::idx::CZ, model::idx::VCZ) = b * qxyz.z();
  q(model::idx::VCZ, model::idx::CZ) = b * qxyz.z();
  q(model::idx::VCZ, model::idx::VCZ) = c * qxyz.z();

  q(model::idx::YAW, model::idx::YAW) = a * qyaw;
  q(model::idx::YAW, model::idx::VYAW) = b * qyaw;
  q(model::idx::VYAW, model::idx::YAW) = b * qyaw;
  q(model::idx::VYAW, model::idx::VYAW) = c * qyaw;

  q(model::idx::R, model::idx::R) = config_.q_r;
  q(model::idx::L, model::idx::L) = q_l;
  q(model::idx::H, model::idx::H) = q_h;

  return q;
}

void Target::predict(std::chrono::steady_clock::time_point t)
{
  const auto dt = tools::delta_time(t, timestamp_);
  predict(dt);
  timestamp_ = t;
}

void Target::predict(double dt)
{
  model::Predict predict_model;
  predict_model.dt = dt;
  predict_model.armor_name = name;
  filter_.setPredictFunc(predict_model);
  filter_.setUpdateQ([this, dt]() { return process_noise(dt); });
  state_.x = filter_.predict();

  if (is_outpost_target() && std::abs(state_.x[model::idx::VYAW]) > 2.0) {
    state_.x[model::idx::VYAW] = state_.x[model::idx::VYAW] > 0 ? 2.51 : -2.51;
  }
}

std::vector<std::pair<int, auto_aim::Armor>> Target::match(std::vector<auto_aim::Armor> & armors) const
{
  std::vector<std::pair<int, auto_aim::Armor>> result;
  if (armors.empty()) return result;

  const bool all_init = is_outpost_target() ? ((outpost_observed_mask_ & 0x7) == 0x7) : jumped;
  const double gate = all_init ? config_.match_gate_at_1m : config_.match_gate_not_all_init_at_1m;

  const int n_obs = static_cast<int>(armors.size());
  const double max_cost = 1e9;
  std::vector<std::vector<double>> cost(n_obs, std::vector<double>(armor_num_, max_cost + 1.0));
  std::vector<VecZ> meas_list(n_obs);
  for (int j = 0; j < n_obs; ++j) {
    meas_list[j] = measurement_vector(armors[j].points);
  }

  for (int j = 0; j < n_obs; ++j) {
    for (int id = 0; id < armor_num_; ++id) {
      model::Measure::Ctx ctx;
      ctx.armor_num = armor_num_;
      ctx.id = id;
      ctx.armor_type = armor_type;
      ctx.armor_name = name;
      ctx.solver = solver_;
      model::Measure measure;
      measure.ctx = ctx;
      VecZ z_pred;
      measure.h(state_.x, z_pred);
      const VecZ nu = meas_list[j] - z_pred;
      const auto r = measurement_covariance(z_pred);
      const double d2 = nu.transpose() * r.ldlt().solve(nu);
      if (std::isfinite(d2) && d2 < gate) {
        cost[j][id] = d2;
      }
    }
  }

  std::vector<bool> used_obs(n_obs, false);
  std::vector<bool> used_id(armor_num_, false);
  while (true) {
    double best = max_cost;
    int best_j = -1;
    int best_id = -1;

    for (int j = 0; j < n_obs; ++j) {
      if (used_obs[j]) continue;
      for (int id = 0; id < armor_num_; ++id) {
        if (used_id[id]) continue;
        if (cost[j][id] < best) {
          best = cost[j][id];
          best_j = j;
          best_id = id;
        }
      }
    }

    if (best_j < 0 || best_id < 0) break;
    used_obs[best_j] = true;
    used_id[best_id] = true;
    result.push_back({best_id, armors[best_j]});
  }

  return result;
}

void Target::mark_outpost_seen(int id)
{
  if (!is_outpost_target() || id < 0 || id >= OUTPOST_ARMOR_COUNT) return;
  outpost_seen_[id] = true;
  outpost_observed_mask_ |= (1 << id);
}

void Target::update(std::vector<auto_aim::Armor> & armors)
{
  auto matches = match(armors);
  if (matches.empty()) return;

  const int id = matches.front().first;
  if (id != 0) jumped = true;
  last_id = id;
  mark_outpost_seen(id);

  measure_ctx_.id = id;
  model::Measure measure;
  measure.ctx = measure_ctx_;
  VecZ z_pred;
  measure.h(state_.x, z_pred);
  const auto z = measurement_vector(matches.front().second.points);

  filter_.setMeasureFunc(measure);
  filter_.setUpdateR([this](const VecZ & z_meas) { return measurement_covariance(z_meas); });
  state_.x = filter_.update(z);
  state_.x[model::idx::YAW] = tools::limit_rad(state_.x[model::idx::YAW]);

  const VecZ residual = z - z_pred;
  const auto r = measurement_covariance(z_pred);
  const double nis = residual.transpose() * r.ldlt().solve(residual);
  recent_nis_failures_.push_back(nis > config_.match_gate_at_1m ? 1 : 0);
  if (recent_nis_failures_.size() > nis_window_size_) {
    recent_nis_failures_.pop_front();
  }

  ++update_count_;
}

Target::VecX Target::ekf_x() const { return state_.x; }

std::vector<Eigen::Vector4d> Target::armor_xyza_list() const
{
  return state_.get_armors_xyza(armor_num_, armor_type, name, *solver_);
}

bool Target::diverged() const
{
  const double r = state_.x[model::idx::R];
  const double l = state_.x[model::idx::L];
  const double h = state_.x[model::idx::H];
  const bool radius_ok = r > MIN_RADIUS && r < MAX_RADIUS && r + l > MIN_RADIUS && r + l < MAX_RADIUS;
  const bool height_ok = std::abs(h) < MAX_HEIGHT_DELTA;
  const bool vyaw_ok = std::abs(state_.x[model::idx::VYAW]) < MAX_VYAW;
  return !(radius_ok && height_ok && vyaw_ok);
}

bool Target::convergened()
{
  const int min_updates = is_outpost_target() ? 10 : 3;
  if (update_count_ > min_updates && !diverged()) {
    is_converged_ = true;
  }
  return is_converged_;
}

const Target::Filter & Target::filter() const { return filter_; }

}  // namespace auto_aim_eskf
