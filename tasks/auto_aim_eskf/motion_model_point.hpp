#ifndef AUTO_AIM_ESKF__MOTION_MODEL_POINT_HPP
#define AUTO_AIM_ESKF__MOTION_MODEL_POINT_HPP

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <ceres/ceres.h>
#include <ceres/jet.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "tasks/auto_aim/armor.hpp"
#include "tasks/auto_aim/solver.hpp"

namespace auto_aim_eskf::model
{
constexpr int X_N = 11;
constexpr int Z_N = 8;
constexpr double FIFTEEN_DEGREE_RAD = 15.0 * CV_PI / 180.0;

using VecX = Eigen::Matrix<double, X_N, 1>;
using VecZ = Eigen::Matrix<double, Z_N, 1>;

namespace idx
{
enum
{
  CX,
  VCX,
  CY,
  VCY,
  CZ,
  VCZ,
  YAW,
  VYAW,
  R,
  P1,
  P2
};

constexpr int L = P1;
constexpr int H = P2;
constexpr int OUTPOST01DZ = P1;
constexpr int OUTPOST02DZ = P2;
}  // namespace idx

inline int armor_num_by_name(auto_aim::ArmorName name)
{
  if (name == auto_aim::ArmorName::outpost) return 3;
  if (name == auto_aim::ArmorName::base) return 3;
  return 4;
}

inline bool is_balance_target(auto_aim::ArmorType type, auto_aim::ArmorName name)
{
  return type == auto_aim::ArmorType::big &&
         (name == auto_aim::ArmorName::three || name == auto_aim::ArmorName::four ||
          name == auto_aim::ArmorName::five);
}

inline std::vector<cv::Point3f> object_points(auto_aim::ArmorType type)
{
  constexpr double lightbar_length = 56e-3;
  constexpr double big_armor_width = 230e-3;
  constexpr double small_armor_width = 135e-3;

  const double width = (type == auto_aim::ArmorType::big) ? big_armor_width : small_armor_width;
  return {
    {0.0f, static_cast<float>(width / 2.0), static_cast<float>(lightbar_length / 2.0)},
    {0.0f, static_cast<float>(-width / 2.0), static_cast<float>(lightbar_length / 2.0)},
    {0.0f, static_cast<float>(-width / 2.0), static_cast<float>(-lightbar_length / 2.0)},
    {0.0f, static_cast<float>(width / 2.0), static_cast<float>(-lightbar_length / 2.0)}};
}

template<typename T>
inline T normalize_angle(T a)
{
  const T two_pi = T(2.0 * M_PI);
  return a - two_pi * floor((a + T(M_PI)) / two_pi);
}

inline bool is_outpost(auto_aim::ArmorName name) { return name == auto_aim::ArmorName::outpost; }

inline bool is_base(auto_aim::ArmorName name) { return name == auto_aim::ArmorName::base; }

struct Predict
{
  double dt {0.0};
  auto_aim::ArmorName armor_name {auto_aim::ArmorName::not_armor};

  template<typename T>
  inline void operator()(const T x0[X_N], T x1[X_N]) const
  {
    std::copy(x0, x0 + X_N, x1);

    if (!is_outpost(armor_name) && !is_base(armor_name)) {
      x1[idx::CX] += x0[idx::VCX] * T(dt);
      x1[idx::CY] += x0[idx::VCY] * T(dt);
      x1[idx::CZ] += x0[idx::VCZ] * T(dt);
    } else {
      x1[idx::VCX] = T(0);
      x1[idx::VCY] = T(0);
      x1[idx::VCZ] = T(0);
    }

    if (!is_base(armor_name)) {
      x1[idx::YAW] += x0[idx::VYAW] * T(dt);
    }

    clamp(x1);
  }

  template<typename T>
  inline void clamp(T x[X_N]) const
  {
    auto & r = x[idx::R];
    auto & l = x[idx::L];
    auto & h = x[idx::H];
    auto & vyaw = x[idx::VYAW];

    if (!is_outpost(armor_name)) {
      if (r + l < T(0.1) || r + l > T(0.5)) {
        r = T(0.25);
        l = T(0);
      }
      if (ceres::abs(h) > T(0.5)) {
        h = T(0.0);
      }
    } else {
      x[idx::VCZ] = T(0.0);
      r = T(0.27);
    }

    if (ceres::abs(vyaw) > T(20.0)) {
      vyaw = T(0.0);
    }
  }

  inline void f(const VecX & x0, VecX & x1) const
  {
    operator()(x0.data(), x1.data());
  }
};

template<typename T>
inline void project_points_jets(
  const std::vector<cv::Point3f> & obj_pts, const Eigen::Transform<T, 3, Eigen::Isometry> & pose_cam,
  const cv::Mat & k, const cv::Mat & dist_coeffs, std::vector<Eigen::Matrix<T, 2, 1>> & img_pts_jet)
{
  const Eigen::Matrix<T, 3, 3> & r = pose_cam.linear();
  const Eigen::Matrix<T, 3, 1> & t = pose_cam.translation();

  const T fx = T(k.at<double>(0, 0));
  const T fy = T(k.at<double>(1, 1));
  const T cx = T(k.at<double>(0, 2));
  const T cy = T(k.at<double>(1, 2));

  auto get_dist = [&](int i) -> double {
    return (dist_coeffs.rows == 1) ? dist_coeffs.at<double>(0, i) : dist_coeffs.at<double>(i, 0);
  };

  const int n_dist = dist_coeffs.rows * dist_coeffs.cols;
  const T k1 = n_dist > 0 ? T(get_dist(0)) : T(0);
  const T k2 = n_dist > 1 ? T(get_dist(1)) : T(0);
  const T p1 = n_dist > 2 ? T(get_dist(2)) : T(0);
  const T p2 = n_dist > 3 ? T(get_dist(3)) : T(0);
  const T k3 = n_dist > 4 ? T(get_dist(4)) : T(0);

  img_pts_jet.clear();
  img_pts_jet.reserve(obj_pts.size());

  for (const auto & pt3 : obj_pts) {
    Eigen::Matrix<T, 3, 1> pw(T(pt3.x), T(pt3.y), T(pt3.z));
    Eigen::Matrix<T, 3, 1> pc = r * pw + t;

    const T xp = pc.x() / pc.z();
    const T yp = pc.y() / pc.z();
    const T r2 = xp * xp + yp * yp;
    const T r4 = r2 * r2;
    const T r6 = r4 * r2;
    const T radial = T(1) + k1 * r2 + k2 * r4 + k3 * r6;
    const T xd = xp * radial + T(2) * p1 * xp * yp + p2 * (r2 + T(2) * xp * xp);
    const T yd = yp * radial + p1 * (r2 + T(2) * yp * yp) + T(2) * p2 * xp * yp;

    img_pts_jet.emplace_back(fx * xd + cx, fy * yd + cy);
  }
}

struct Measure
{
  struct Ctx
  {
    int armor_num {4};
    int id {0};
    auto_aim::ArmorType armor_type {auto_aim::ArmorType::small};
    auto_aim::ArmorName armor_name {auto_aim::ArmorName::not_armor};
    const auto_aim::Solver * solver {nullptr};
  } ctx;

  template<typename T>
  inline void operator()(const T x[X_N], T z[Z_N]) const
  {
    if (ctx.solver == nullptr) {
      for (int i = 0; i < Z_N; ++i) z[i] = T(0);
      return;
    }

    T ax, ay, az, yaw;
    armor_pose(x, ax, ay, az, yaw);

    const T armor_pitch =
      is_outpost(ctx.armor_name) ? T(-FIFTEEN_DEGREE_RAD) : T(FIFTEEN_DEGREE_RAD);

    Eigen::Quaternion<T> q_yaw(Eigen::AngleAxis<T>(yaw, Eigen::Vector3<T>::UnitZ()));
    Eigen::Quaternion<T> q_pitch(Eigen::AngleAxis<T>(armor_pitch, Eigen::Vector3<T>::UnitY()));

    Eigen::Transform<T, 3, Eigen::Isometry> pose_in_world = Eigen::Transform<T, 3, Eigen::Isometry>::Identity();
    pose_in_world.translation() << ax, ay, az;
    pose_in_world.linear() = (q_yaw * q_pitch).toRotationMatrix();

    Eigen::Matrix<T, 3, 3> r_world_to_camera =
      ctx.solver->R_camera2gimbal().transpose() *
      ctx.solver->R_gimbal2world().transpose().template cast<T>();
    Eigen::Matrix<T, 3, 1> t_world_to_camera =
      ctx.solver->R_camera2gimbal().transpose().template cast<T>() *
      (ctx.solver->R_gimbal2world().transpose().template cast<T>() * pose_in_world.translation() -
       ctx.solver->t_camera2gimbal().template cast<T>());

    Eigen::Transform<T, 3, Eigen::Isometry> pose_in_camera =
      Eigen::Transform<T, 3, Eigen::Isometry>::Identity();
    pose_in_camera.linear() = r_world_to_camera * pose_in_world.linear();
    pose_in_camera.translation() = t_world_to_camera;

    std::vector<Eigen::Matrix<T, 2, 1>> img_pts_jet;
    project_points_jets(
      object_points(ctx.armor_type), pose_in_camera, ctx.solver->camera_matrix(),
      ctx.solver->distort_coeffs(), img_pts_jet);

    for (int i = 0; i < 4; ++i) {
      z[2 * i] = img_pts_jet[i].x();
      z[2 * i + 1] = img_pts_jet[i].y();
    }
  }

  inline void h(const VecX & x, VecZ & z) const { operator()(x.data(), z.data()); }

  template<typename T>
  inline T get_armor_r(const T x[X_N]) const
  {
    const bool use_lh = (ctx.armor_num == 4) && (ctx.id & 1);
    return use_lh ? x[idx::R] + x[idx::L] : x[idx::R];
  }

  template<typename T>
  inline void armor_pose(const T x[X_N], T & ax, T & ay, T & az, T & yaw) const
  {
    yaw = normalize_angle(x[idx::YAW] + T(ctx.id) * T(2.0 * M_PI / ctx.armor_num));

    const bool outpost = is_outpost(ctx.armor_name);
    const bool use_lh = (ctx.armor_num == 4) && (ctx.id & 1);
    const T r = get_armor_r(x);

    ax = x[idx::CX] - ceres::cos(yaw) * r;
    ay = x[idx::CY] - ceres::sin(yaw) * r;

    if (outpost) {
      az = (ctx.id == 0)   ? x[idx::CZ]
         : (ctx.id == 1)   ? x[idx::CZ] + x[idx::OUTPOST01DZ]
         : (ctx.id == 2)   ? x[idx::CZ] + x[idx::OUTPOST02DZ]
                           : x[idx::CZ];
    } else {
      az = use_lh ? x[idx::CZ] + x[idx::H] : x[idx::CZ];
    }
  }
};

struct State
{
  VecX x = VecX::Zero();

  inline std::vector<Eigen::Vector4d> get_armors_xyza(
    int armor_num, auto_aim::ArmorType armor_type, auto_aim::ArmorName armor_name,
    const auto_aim::Solver & solver) const
  {
    std::vector<Eigen::Vector4d> result;
    result.reserve(armor_num);
    for (int i = 0; i < armor_num; ++i) {
      Measure::Ctx ctx;
      ctx.armor_num = armor_num;
      ctx.id = i;
      ctx.armor_type = armor_type;
      ctx.armor_name = armor_name;
      ctx.solver = &solver;
      Measure measure;
      measure.ctx = ctx;
      double ax, ay, az, yaw;
      measure.armor_pose(x.data(), ax, ay, az, yaw);
      result.push_back({ax, ay, az, yaw});
    }
    return result;
  }

  inline Eigen::Vector3d pos() const { return {x[idx::CX], x[idx::CY], x[idx::CZ]}; }
};

}  // namespace auto_aim_eskf::model

#endif  // AUTO_AIM_ESKF__MOTION_MODEL_POINT_HPP
