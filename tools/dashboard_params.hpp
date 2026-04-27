#ifndef TOOLS__DASHBOARD_PARAMS_HPP
#define TOOLS__DASHBOARD_PARAMS_HPP

#include <functional>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace auto_aim
{
class Planner;
}  // namespace auto_aim

namespace auto_buff
{
class Aimer;
}  // namespace auto_buff

namespace tools
{
namespace dashboard
{

struct DashboardParamSnapshot
{
  double planner_yaw_offset_deg;
  double planner_pitch_offset_deg;
  double planner_fire_thresh;
  double planner_decision_speed;
  double planner_high_speed_delay_time;
  double planner_low_speed_delay_time;
  double buff_yaw_offset_deg;
  double buff_pitch_offset_deg;
  double buff_fire_gap_time;
  double buff_predict_time;
};

struct DashboardParamUpdate
{
  std::string key;
  std::string local_key;
  double value;
};

enum class DashboardParamStatus
{
  Applied,
  UnknownKey,
  TypeError,
  OutOfRange,
  ApplyFailed
};

struct DashboardParamResult
{
  bool ok;
  DashboardParamStatus status;
  std::string key;
  std::string message;
  nlohmann::json applied;
};

class DashboardParams
{
public:
  using SnapshotReader = std::function<DashboardParamSnapshot()>;
  using ParamWriter = std::function<bool(const DashboardParamUpdate &)>;

  DashboardParams(SnapshotReader read_snapshot, ParamWriter write_param, bool include_buff = true);
  explicit DashboardParams(auto_aim::Planner & planner);
  DashboardParams(auto_aim::Planner & planner, auto_buff::Aimer & buff_aimer);

  nlohmann::json make_schema() const;
  nlohmann::json make_current(std::int64_t timestamp) const;
  DashboardParamResult apply(const std::string & key, const nlohmann::json & value) const;

private:
  SnapshotReader read_snapshot_;
  ParamWriter write_param_;
  bool include_buff_{true};
};

}  // namespace dashboard
}  // namespace tools

#endif  // TOOLS__DASHBOARD_PARAMS_HPP
