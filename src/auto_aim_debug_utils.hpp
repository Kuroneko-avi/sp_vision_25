#ifndef SRC__AUTO_AIM_DEBUG_UTILS_HPP
#define SRC__AUTO_AIM_DEBUG_UTILS_HPP

namespace auto_aim
{
namespace debug
{

inline double plot_pitch(double pitch) { return -pitch; }

inline double plot_pitch_error(double gimbal_pitch, double target_pitch)
{
  return gimbal_pitch - plot_pitch(target_pitch);
}

}  // namespace debug
}  // namespace auto_aim

#endif  // SRC__AUTO_AIM_DEBUG_UTILS_HPP
