#include "tools/dashboard_params.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tasks/auto_aim/planner/planner.hpp"
#include "tasks/auto_buff/buff_aimer.hpp"
#include "tools/dashboard_mqtt_contract.hpp"

namespace tools
{
namespace dashboard
{
namespace
{
struct ParamSpec
{
  const char * key;
  const char * local_key;
  double DashboardParamSnapshot::*value;
  double min;
  double max;
  double step;
  const char * unit;
  const char * group;
};

const std::vector<ParamSpec> & param_specs()
{
  static const std::vector<ParamSpec> specs{
    {"planner.yaw_offset_deg",
     "yaw_offset_deg",
     &DashboardParamSnapshot::planner_yaw_offset_deg,
     -20.0,
     20.0,
     0.1,
     "deg",
     "planner"},
    {"planner.pitch_offset_deg",
     "pitch_offset_deg",
     &DashboardParamSnapshot::planner_pitch_offset_deg,
     -20.0,
     20.0,
     0.1,
     "deg",
     "planner"},
    {"planner.fire_thresh",
     "fire_thresh",
     &DashboardParamSnapshot::planner_fire_thresh,
     0.0,
     0.05,
     0.0001,
     "rad",
     "planner"},
    {"planner.decision_speed",
     "decision_speed",
     &DashboardParamSnapshot::planner_decision_speed,
     0.0,
     30.0,
     0.1,
     "rad/s",
     "planner"},
    {"planner.high_speed_delay_time",
     "high_speed_delay_time",
     &DashboardParamSnapshot::planner_high_speed_delay_time,
     0.0,
     0.5,
     0.001,
     "s",
     "planner"},
    {"planner.low_speed_delay_time",
     "low_speed_delay_time",
     &DashboardParamSnapshot::planner_low_speed_delay_time,
     0.0,
     0.5,
     0.001,
     "s",
     "planner"},
    {"buff.yaw_offset_deg",
     "yaw_offset_deg",
     &DashboardParamSnapshot::buff_yaw_offset_deg,
     -20.0,
     20.0,
     0.1,
     "deg",
     "buff"},
    {"buff.pitch_offset_deg",
     "pitch_offset_deg",
     &DashboardParamSnapshot::buff_pitch_offset_deg,
     -20.0,
     20.0,
     0.1,
     "deg",
     "buff"},
    {"buff.fire_gap_time",
     "fire_gap_time",
     &DashboardParamSnapshot::buff_fire_gap_time,
     0.0,
     5.0,
     0.001,
     "s",
     "buff"},
    {"buff.predict_time",
     "predict_time",
     &DashboardParamSnapshot::buff_predict_time,
     0.0,
     1.0,
     0.001,
     "s",
     "buff"}};
  return specs;
}

const ParamSpec * find_spec(const std::string & key)
{
  const auto & specs = param_specs();
  const auto iter = std::find_if(
    specs.begin(), specs.end(), [&key](const auto & spec) { return key == spec.key; });
  return iter == specs.end() ? nullptr : &*iter;
}

bool spec_enabled(const ParamSpec & spec, bool include_buff)
{
  return include_buff || std::string(spec.group) != "buff";
}

DashboardParamResult make_result(
  bool ok, DashboardParamStatus status, const std::string & key, const std::string & message,
  nlohmann::json applied = nlohmann::json::object())
{
  return {ok, status, key, message, applied};
}

}  // namespace

DashboardParams::DashboardParams(
  SnapshotReader read_snapshot, ParamWriter write_param, bool include_buff)
: read_snapshot_(std::move(read_snapshot)),
  write_param_(std::move(write_param)),
  include_buff_(include_buff)
{
  if (!read_snapshot_ || !write_param_) {
    throw std::invalid_argument("DashboardParams requires read and write callbacks");
  }
}

DashboardParams::DashboardParams(auto_aim::Planner & planner)
: DashboardParams(
    [&planner]() {
      const auto planner_params = planner.get_hot_params();
      return DashboardParamSnapshot{
        planner_params.yaw_offset_deg,
        planner_params.pitch_offset_deg,
        planner_params.fire_thresh,
        planner_params.decision_speed,
        planner_params.high_speed_delay_time,
        planner_params.low_speed_delay_time,
        0.0,
        0.0,
        0.0,
        0.0};
    },
    [&planner](const DashboardParamUpdate & update) {
      if (update.key.rfind("planner.", 0) == 0) {
        return planner.apply_hot_param(update.local_key, update.value);
      }
      return false;
    },
    false)
{
}

DashboardParams::DashboardParams(auto_aim::Planner & planner, auto_buff::Aimer & buff_aimer)
: DashboardParams(
    [&planner, &buff_aimer]() {
      const auto planner_params = planner.get_hot_params();
      const auto buff_params = buff_aimer.get_hot_params();
      return DashboardParamSnapshot{
        planner_params.yaw_offset_deg,
        planner_params.pitch_offset_deg,
        planner_params.fire_thresh,
        planner_params.decision_speed,
        planner_params.high_speed_delay_time,
        planner_params.low_speed_delay_time,
        buff_params.yaw_offset_deg,
        buff_params.pitch_offset_deg,
        buff_params.fire_gap_time,
        buff_params.predict_time};
    },
    [&planner, &buff_aimer](const DashboardParamUpdate & update) {
      if (update.key.rfind("planner.", 0) == 0) {
        return planner.apply_hot_param(update.local_key, update.value);
      }
      if (update.key.rfind("buff.", 0) == 0) {
        return buff_aimer.apply_hot_param(update.local_key, update.value);
      }
      return false;
    },
    true)
{
}

nlohmann::json DashboardParams::make_schema() const
{
  const auto snapshot = read_snapshot_();
  std::vector<nlohmann::json> params;
  params.reserve(param_specs().size());
  for (const auto & spec : param_specs()) {
    if (!spec_enabled(spec, include_buff_)) {
      continue;
    }
    params.push_back(make_number_param_schema(
      spec.key, snapshot.*(spec.value), spec.min, spec.max, spec.step, spec.unit, spec.group));
  }
  return make_params_schema_payload(params);
}

nlohmann::json DashboardParams::make_current(std::int64_t timestamp) const
{
  const auto snapshot = read_snapshot_();
  nlohmann::json values = nlohmann::json::object();
  for (const auto & spec : param_specs()) {
    if (!spec_enabled(spec, include_buff_)) {
      continue;
    }
    values[spec.key] = snapshot.*(spec.value);
  }
  return make_params_current_payload(values, timestamp);
}

DashboardParamResult DashboardParams::apply(const std::string & key, const nlohmann::json & value)
  const
{
  const auto * spec = find_spec(key);
  if (spec == nullptr || !spec_enabled(*spec, include_buff_)) {
    return make_result(false, DashboardParamStatus::UnknownKey, key, "unknown parameter key");
  }

  if (!value.is_number() || value.is_boolean()) {
    return make_result(false, DashboardParamStatus::TypeError, key, "parameter value must be number");
  }

  const auto number = value.get<double>();
  if (!std::isfinite(number)) {
    return make_result(false, DashboardParamStatus::TypeError, key, "parameter value must be finite");
  }

  if (number < spec->min || number > spec->max) {
    return make_result(false, DashboardParamStatus::OutOfRange, key, "parameter value out of range");
  }

  DashboardParamUpdate update{spec->key, spec->local_key, number};
  if (!write_param_(update)) {
    return make_result(false, DashboardParamStatus::ApplyFailed, key, "parameter apply failed");
  }

  return make_result(
    true, DashboardParamStatus::Applied, key, std::string(spec->key) + " applied",
    nlohmann::json{{spec->key, number}});
}

}  // namespace dashboard
}  // namespace tools
