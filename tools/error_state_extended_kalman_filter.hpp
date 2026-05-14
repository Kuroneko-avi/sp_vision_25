#ifndef TOOLS__ERROR_STATE_EXTENDED_KALMAN_FILTER_HPP
#define TOOLS__ERROR_STATE_EXTENDED_KALMAN_FILTER_HPP

#include <Eigen/Dense>
#include <ceres/jet.h>

#include <cmath>
#include <functional>
#include <limits>

namespace tools
{
template<int N_X, int N_Z, class PredictFunc, class MeasureFunc>
class ErrorStateExtendedKalmanFilter
{
public:
  using MatrixXX = Eigen::Matrix<double, N_X, N_X>;
  using MatrixZX = Eigen::Matrix<double, N_Z, N_X>;
  using MatrixXZ = Eigen::Matrix<double, N_X, N_Z>;
  using MatrixZZ = Eigen::Matrix<double, N_Z, N_Z>;
  using MatrixX1 = Eigen::Matrix<double, N_X, 1>;
  using MatrixZ1 = Eigen::Matrix<double, N_Z, 1>;

  using UpdateQFunc = std::function<MatrixXX()>;
  using UpdateRFunc = std::function<MatrixZZ(const MatrixZ1 &)>;
  using InjectFunc = std::function<void(const MatrixX1 &, MatrixX1 &)>;
  using ResidualFunc = std::function<MatrixZ1(const MatrixZ1 &, const MatrixZ1 &)>;

  ErrorStateExtendedKalmanFilter() = default;

  explicit ErrorStateExtendedKalmanFilter(
    const PredictFunc & f, const MeasureFunc & h, const UpdateQFunc & u_q, const UpdateRFunc & u_r,
    const MatrixXX & p0) noexcept
  : f_(f), h_(h), update_q_(u_q), update_r_(u_r), p_delta_(p0), p_delta_pri_(p0)
  {
    cal_residual_ = [](const MatrixZ1 & z_pred, const MatrixZ1 & z_meas) { return z_meas - z_pred; };
    inject_state_ = [](const MatrixX1 & delta, MatrixX1 & nominal) { nominal += delta; };
  }

  void setInjectFunc(const InjectFunc & inject_func) { inject_state_ = inject_func; }

  void setState(const MatrixX1 & x0) noexcept
  {
    x_nominal_ = x0;
    delta_x_.setZero();
  }

  void setPredictFunc(const PredictFunc & f) noexcept { f_ = f; }

  void setMeasureFunc(const MeasureFunc & h) noexcept { h_ = h; }

  void setIterationNum(int num) { iteration_num_ = num; }

  void setResidualFunc(const ResidualFunc & func) { cal_residual_ = func; }

  void setUpdateQ(const UpdateQFunc & u_q) { update_q_ = u_q; }

  void setUpdateR(const UpdateRFunc & u_r) { update_r_ = u_r; }

  const MatrixX1 & getState() const noexcept { return x_nominal_; }

  MatrixX1 predict() noexcept
  {
    ceres::Jet<double, N_X> x_jet[N_X];
    for (int i = 0; i < N_X; ++i) {
      x_jet[i].a = x_nominal_[i];
      x_jet[i].v.setZero();
      x_jet[i].v[i] = 1.0;
    }

    ceres::Jet<double, N_X> x_pred_jet[N_X];
    f_(x_jet, x_pred_jet);

    MatrixX1 x_pri;
    for (int i = 0; i < N_X; ++i) {
      x_pri[i] = x_pred_jet[i].a;
      f_jacobian_.block(i, 0, 1, N_X) = x_pred_jet[i].v.transpose();
    }

    q_ = update_q_();
    p_delta_pri_ = f_jacobian_ * p_delta_ * f_jacobian_.transpose() + q_;
    p_delta_pri_ = 0.5 * (p_delta_pri_ + p_delta_pri_.transpose());

    p_delta_ = p_delta_pri_;
    x_nominal_ = x_pri;
    delta_x_.setZero();
    return x_pri;
  }

  MatrixX1 update(const MatrixZ1 & z) noexcept { return update(z, h_); }

  MatrixX1 update(const MatrixZ1 & z, const MeasureFunc & h) noexcept
  {
    MatrixX1 delta_iter = delta_x_;
    MatrixXX p_iter = p_delta_;
    MatrixXZ k_last = MatrixXZ::Zero();

    double prev_res_norm = std::numeric_limits<double>::max();

    for (int iter = 0; iter < iteration_num_; ++iter) {
      MatrixX1 x_full = x_nominal_;
      if (inject_state_) {
        inject_state_(delta_iter, x_full);
      }

      ceres::Jet<double, N_X> x_jet[N_X];
      for (int i = 0; i < N_X; ++i) {
        x_jet[i].a = x_full[i];
        x_jet[i].v.setZero();
        x_jet[i].v[i] = 1.0;
      }

      ceres::Jet<double, N_X> z_jet[N_Z];
      h(x_jet, z_jet);

      MatrixZ1 z_pred;
      for (int i = 0; i < N_Z; ++i) {
        z_pred[i] = z_jet[i].a;
        h_jacobian_.block(i, 0, 1, N_X) = z_jet[i].v.transpose();
      }

      r_ = update_r_(z);
      MatrixZZ s = h_jacobian_ * p_iter * h_jacobian_.transpose() + r_;
      s += small_noise_ * MatrixZZ::Identity();
      MatrixXZ k = p_iter * h_jacobian_.transpose() * s.inverse();
      k_last = k;

      MatrixZ1 residual = cal_residual_(z_pred, z);
      for (int i = 0; i < N_Z; ++i) {
        if (!std::isfinite(residual[i])) {
          residual[i] = 0.0;
        }
      }

      const double cur_res_norm = residual.norm();
      double alpha = 1.0;
      if (cur_res_norm > prev_res_norm) {
        alpha = 0.5;
      }
      delta_iter += alpha * k * residual;

      const double old_res_norm = prev_res_norm;
      prev_res_norm = cur_res_norm;
      last_residual_ = residual;

      if (cur_res_norm < 1e-4 || std::abs(old_res_norm - cur_res_norm) < 1e-6) {
        break;
      }
    }

    if (inject_state_) {
      inject_state_(delta_iter, x_nominal_);
    }
    delta_x_.setZero();

    p_delta_ = (MatrixXX::Identity() - k_last * h_jacobian_) * p_iter *
                 (MatrixXX::Identity() - k_last * h_jacobian_).transpose() +
               k_last * r_ * k_last.transpose();
    p_delta_ = 0.5 * (p_delta_ + p_delta_.transpose());
    return x_nominal_;
  }

private:
  PredictFunc f_;
  MeasureFunc h_;
  UpdateQFunc update_q_;
  UpdateRFunc update_r_;
  InjectFunc inject_state_;
  ResidualFunc cal_residual_;

  MatrixXX f_jacobian_ = MatrixXX::Zero();
  MatrixZX h_jacobian_ = MatrixZX::Zero();
  MatrixXX q_ = MatrixXX::Zero();
  MatrixZZ r_ = MatrixZZ::Zero();

  MatrixX1 x_nominal_ = MatrixX1::Zero();
  MatrixX1 delta_x_ = MatrixX1::Zero();
  MatrixXX p_delta_ = MatrixXX::Identity();
  MatrixXX p_delta_pri_ = MatrixXX::Identity();
  MatrixZ1 last_residual_ = MatrixZ1::Zero();

  int iteration_num_ = 1;
  double small_noise_ = 1e-6;
};

}  // namespace tools

#endif  // TOOLS__ERROR_STATE_EXTENDED_KALMAN_FILTER_HPP
