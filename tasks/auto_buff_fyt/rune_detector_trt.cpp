#include "rune_detector_trt.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include "tools/logger.hpp"

#ifdef AUTO_BUFF_FYT_ENABLE_TENSORRT
#include <NvInfer.h>
#include <cuda_runtime_api.h>
#endif

namespace auto_buff_fyt
{
namespace
{
constexpr int INPUT_W = 480;
constexpr int INPUT_H = 480;
constexpr int NUM_CLASSES = 2;
constexpr int NUM_COLORS = 2;
constexpr int NUM_POINTS = 5;
constexpr int NUM_POINTS_2 = 2 * NUM_POINTS;
constexpr int FEATURE_SIZE = 15;
constexpr float MERGE_CONF_ERROR = 0.15f;
constexpr float MERGE_MIN_IOU = 0.9f;

const std::unordered_map<int, EnemyColor> DNN_COLOR_TO_ENEMY_COLOR = {
  {0, EnemyColor::BLUE},
  {1, EnemyColor::RED}};

cv::Mat letterbox(
  const cv::Mat & img, Eigen::Matrix3f & transform_matrix,
  const std::vector<int> & new_shape = {INPUT_W, INPUT_H})
{
  const int img_h = img.rows;
  const int img_w = img.cols;

  const float scale =
    std::min(new_shape[1] * 1.0f / img_h, new_shape[0] * 1.0f / img_w);
  const int resize_h = static_cast<int>(std::round(img_h * scale));
  const int resize_w = static_cast<int>(std::round(img_w * scale));

  const int pad_h = new_shape[1] - resize_h;
  const int pad_w = new_shape[0] - resize_w;
  const float half_h = pad_h * 0.5f;
  const float half_w = pad_w * 0.5f;

  const int top = static_cast<int>(std::round(half_h - 0.1f));
  const int bottom = static_cast<int>(std::round(half_h + 0.1f));
  const int left = static_cast<int>(std::round(half_w - 0.1f));
  const int right = static_cast<int>(std::round(half_w + 0.1f));

  transform_matrix << 1.0f / scale, 0.0f, -half_w / scale, 0.0f, 1.0f / scale,
    -half_h / scale, 0.0f, 0.0f, 1.0f;

  cv::Mat resized_img;
  cv::resize(img, resized_img, cv::Size(resize_w, resize_h));
  cv::copyMakeBorder(
    resized_img, resized_img, top, bottom, left, right, cv::BORDER_CONSTANT,
    cv::Scalar(114, 114, 114));
  return resized_img;
}

void generate_grids_and_stride(
  int target_w, int target_h, const std::vector<int> & strides,
  std::vector<RuneDetectorTRT::GridAndStride> & grid_strides)
{
  for (auto stride : strides) {
    const int num_grid_w = target_w / stride;
    const int num_grid_h = target_h / stride;
    for (int g1 = 0; g1 < num_grid_h; ++g1) {
      for (int g0 = 0; g0 < num_grid_w; ++g0) {
        grid_strides.emplace_back(RuneDetectorTRT::GridAndStride{g0, g1, stride});
      }
    }
  }
}

void generate_proposals(
  std::vector<RuneObject> & output_objs, const cv::Mat & output_buffer,
  const Eigen::Matrix3f & transform_matrix, float conf_threshold,
  const std::vector<RuneDetectorTRT::GridAndStride> & grid_strides)
{
  for (size_t anchor_idx = 0; anchor_idx < grid_strides.size(); ++anchor_idx) {
    const float confidence = output_buffer.at<float>(static_cast<int>(anchor_idx), NUM_POINTS_2);
    if (confidence < conf_threshold) continue;

    const int grid0 = grid_strides[anchor_idx].grid0;
    const int grid1 = grid_strides[anchor_idx].grid1;
    const int stride = grid_strides[anchor_idx].stride;

    double color_score = 0.0;
    double class_score = 0.0;
    cv::Point color_id;
    cv::Point class_id;
    const cv::Mat color_scores = output_buffer.row(static_cast<int>(anchor_idx))
                                   .colRange(NUM_POINTS_2 + 1, NUM_POINTS_2 + 1 + NUM_COLORS);
    const cv::Mat class_scores = output_buffer.row(static_cast<int>(anchor_idx))
                                   .colRange(
                                     NUM_POINTS_2 + 1 + NUM_COLORS,
                                     NUM_POINTS_2 + 1 + NUM_COLORS + NUM_CLASSES);
    cv::minMaxLoc(color_scores, nullptr, &color_score, nullptr, &color_id);
    cv::minMaxLoc(class_scores, nullptr, &class_score, nullptr, &class_id);

    const float x1 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 0) + grid0) * stride;
    const float y1 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 1) + grid1) * stride;
    const float x2 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 2) + grid0) * stride;
    const float y2 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 3) + grid1) * stride;
    const float x3 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 4) + grid0) * stride;
    const float y3 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 5) + grid1) * stride;
    const float x4 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 6) + grid0) * stride;
    const float y4 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 7) + grid1) * stride;
    const float x5 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 8) + grid0) * stride;
    const float y5 = (output_buffer.at<float>(static_cast<int>(anchor_idx), 9) + grid1) * stride;

    Eigen::Matrix<float, 3, 5> apex_norm;
    Eigen::Matrix<float, 3, 5> apex_dst;
    apex_norm << x1, x2, x3, x4, x5, y1, y2, y3, y4, y5, 1, 1, 1, 1, 1;
    apex_dst = transform_matrix * apex_norm;

    RuneObject obj;
    obj.pts.r_center = cv::Point2f(apex_dst(0, 0), apex_dst(1, 0));
    obj.pts.bottom_left = cv::Point2f(apex_dst(0, 1), apex_dst(1, 1));
    obj.pts.top_left = cv::Point2f(apex_dst(0, 2), apex_dst(1, 2));
    obj.pts.top_right = cv::Point2f(apex_dst(0, 3), apex_dst(1, 3));
    obj.pts.bottom_right = cv::Point2f(apex_dst(0, 4), apex_dst(1, 4));
    obj.box = cv::boundingRect(obj.pts.to_vector2f());
    obj.color = DNN_COLOR_TO_ENEMY_COLOR.at(color_id.x);
    obj.type = static_cast<RuneType>(class_id.x);
    obj.prob = confidence;
    output_objs.emplace_back(std::move(obj));
  }
}

float intersection_area(const RuneObject & a, const RuneObject & b)
{
  const cv::Rect_<float> inter = a.box & b.box;
  return inter.area();
}

void nms_merge_sorted_bboxes(
  std::vector<RuneObject> & objects, std::vector<int> & picked, float nms_threshold)
{
  picked.clear();
  const int n = static_cast<int>(objects.size());
  std::vector<float> areas(n);
  for (int i = 0; i < n; ++i) areas[i] = objects[i].box.area();

  for (int i = 0; i < n; ++i) {
    auto & a = objects[i];
    bool keep = true;
    for (auto picked_idx : picked) {
      auto & b = objects[picked_idx];
      const float inter_area = intersection_area(a, b);
      const float union_area = areas[i] + areas[picked_idx] - inter_area;
      const float iou = inter_area / union_area;
      if (iou > nms_threshold || std::isnan(iou)) {
        keep = false;
        if (
          a.type == b.type && a.color == b.color && iou > MERGE_MIN_IOU &&
          std::abs(a.prob - b.prob) < MERGE_CONF_ERROR)
        {
          a.pts.children.push_back(b.pts);
        }
      }
    }
    if (keep) picked.push_back(i);
  }
}

#ifdef AUTO_BUFF_FYT_ENABLE_TENSORRT
class TRTLogger final : public nvinfer1::ILogger
{
public:
  void log(Severity severity, const char * msg) noexcept override
  {
    if (severity <= Severity::kERROR) {
      tools::logger()->error("[TensorRT] {}", msg);
    } else if (severity == Severity::kWARNING) {
      tools::logger()->warn("[TensorRT] {}", msg);
    } else {
      tools::logger()->debug("[TensorRT] {}", msg);
    }
  }
};

template <typename T>
struct TRTDestroy
{
  void operator()(T * obj) const
  {
    if (obj == nullptr) return;
#if NV_TENSORRT_MAJOR >= 10
    delete obj;
#else
    obj->destroy();
#endif
  }
};

inline void check_cuda(cudaError_t code, const std::string & hint)
{
  if (code != cudaSuccess) {
    throw std::runtime_error(hint + ": " + cudaGetErrorString(code));
  }
}

template <typename T>
class PinnedHostBuffer
{
public:
  PinnedHostBuffer() = default;
  ~PinnedHostBuffer() { release(); }

  PinnedHostBuffer(const PinnedHostBuffer &) = delete;
  PinnedHostBuffer & operator=(const PinnedHostBuffer &) = delete;

  void resize(size_t count, const std::string & hint)
  {
    if (count == size_) return;
    release();
    if (count == 0) return;

    void * ptr = nullptr;
    check_cuda(cudaMallocHost(&ptr, count * sizeof(T)), hint);
    data_ = static_cast<T *>(ptr);
    size_ = count;
  }

  void release()
  {
    if (data_ != nullptr) {
      cudaFreeHost(data_);
      data_ = nullptr;
      size_ = 0;
    }
  }

  T * data() { return data_; }
  const T * data() const { return data_; }
  size_t size() const { return size_; }

private:
  T * data_ = nullptr;
  size_t size_ = 0;
};

inline bool has_dynamic_dim(const nvinfer1::Dims & dims)
{
  for (int i = 0; i < dims.nbDims; ++i) {
    if (dims.d[i] < 0) return true;
  }
  return false;
}

inline size_t volume(const nvinfer1::Dims & dims)
{
  size_t out = 1;
  for (int i = 0; i < dims.nbDims; ++i) {
    if (dims.d[i] <= 0) {
      throw std::runtime_error("TensorRT dims contains invalid values");
    }
    out *= static_cast<size_t>(dims.d[i]);
  }
  return out;
}
#endif
}  // namespace

struct RuneDetectorTRT::Impl
{
#ifdef AUTO_BUFF_FYT_ENABLE_TENSORRT
  TRTLogger logger;
  std::unique_ptr<nvinfer1::IRuntime, TRTDestroy<nvinfer1::IRuntime>> runtime;
  std::unique_ptr<nvinfer1::ICudaEngine, TRTDestroy<nvinfer1::ICudaEngine>> engine;
  std::unique_ptr<nvinfer1::IExecutionContext, TRTDestroy<nvinfer1::IExecutionContext>> context;

#if NV_TENSORRT_MAJOR >= 10
  std::string input_name;
  std::string output_name;
#else
  int input_index = -1;
  int output_index = -1;
  std::vector<void *> bindings;
#endif

  void * input_device = nullptr;
  void * output_device = nullptr;
  size_t input_count = 0;
  size_t output_count = 0;
  cudaStream_t stream = nullptr;
  PinnedHostBuffer<float> host_input;
  PinnedHostBuffer<float> host_output;
  cv::Mat output_cache;

  ~Impl()
  {
    if (input_device != nullptr) {
      cudaFree(input_device);
      input_device = nullptr;
    }
    if (output_device != nullptr) {
      cudaFree(output_device);
      output_device = nullptr;
    }
    if (stream != nullptr) {
      cudaStreamDestroy(stream);
      stream = nullptr;
    }
  }

  void initialize(const std::string & model_path)
  {
    std::ifstream file(model_path, std::ios::binary);
    if (!file.good()) {
      throw std::runtime_error("Failed to open TensorRT engine: " + model_path);
    }

    file.seekg(0, std::ios::end);
    const auto length = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<char> data(length);
    file.read(data.data(), static_cast<std::streamsize>(length));

    int device_count = 0;
    const auto cuda_status = cudaGetDeviceCount(&device_count);
    if (cuda_status != cudaSuccess) {
      throw std::runtime_error(
        "CUDA initialization failed before TensorRT runtime creation: " +
        std::string(cudaGetErrorString(cuda_status)));
    }
    if (device_count <= 0) {
      throw std::runtime_error("No CUDA device available for TensorRT inference");
    }

    runtime.reset(nvinfer1::createInferRuntime(logger));
    if (!runtime) {
      throw std::runtime_error("Failed to create TensorRT runtime");
    }

    engine.reset(runtime->deserializeCudaEngine(data.data(), data.size()));
    if (!engine) {
      throw std::runtime_error("Failed to deserialize TensorRT engine");
    }

    context.reset(engine->createExecutionContext());
    if (!context) {
      throw std::runtime_error("Failed to create TensorRT execution context");
    }

#if NV_TENSORRT_MAJOR >= 10
    const int nb_io_tensors = engine->getNbIOTensors();
    for (int i = 0; i < nb_io_tensors; ++i) {
      const char * tensor_name = engine->getIOTensorName(i);
      if (tensor_name == nullptr) continue;
      const auto mode = engine->getTensorIOMode(tensor_name);
      if (mode == nvinfer1::TensorIOMode::kINPUT) {
        input_name = tensor_name;
      } else if (mode == nvinfer1::TensorIOMode::kOUTPUT) {
        output_name = tensor_name;
      }
    }

    if (input_name.empty() || output_name.empty()) {
      throw std::runtime_error("TensorRT engine must contain one input and one output tensor");
    }

    auto input_dims = engine->getTensorShape(input_name.c_str());
    const auto input_dtype = engine->getTensorDataType(input_name.c_str());
    if (input_dtype != nvinfer1::DataType::kFLOAT) {
      throw std::runtime_error("RuneDetectorTRT expects FLOAT TensorRT input");
    }
    if (has_dynamic_dim(input_dims)) {
      if (!context->setInputShape(input_name.c_str(), nvinfer1::Dims4(1, 3, INPUT_H, INPUT_W))) {
        throw std::runtime_error("Failed to set TensorRT input dims");
      }
      input_dims = context->getTensorShape(input_name.c_str());
    }

    input_count = volume(input_dims);
    host_input.resize(input_count, "Failed to allocate pinned host input buffer");

    auto output_dims = context->getTensorShape(output_name.c_str());
    const auto output_dtype = engine->getTensorDataType(output_name.c_str());
    if (output_dtype != nvinfer1::DataType::kFLOAT) {
      throw std::runtime_error("RuneDetectorTRT expects FLOAT TensorRT output");
    }
    if (has_dynamic_dim(output_dims)) {
      throw std::runtime_error("TensorRT output dims are still dynamic after setting input dims");
    }

    output_count = volume(output_dims);
    host_output.resize(output_count, "Failed to allocate pinned host output buffer");
#else
    const int nb_bindings = engine->getNbBindings();
    bindings.assign(static_cast<size_t>(nb_bindings), nullptr);
    for (int i = 0; i < nb_bindings; ++i) {
      if (engine->bindingIsInput(i)) {
        input_index = i;
      } else {
        output_index = i;
      }
    }

    if (input_index < 0 || output_index < 0) {
      throw std::runtime_error("TensorRT engine must contain one input and one output binding");
    }

    auto input_dims = engine->getBindingDimensions(input_index);
    const auto input_dtype = engine->getBindingDataType(input_index);
    if (input_dtype != nvinfer1::DataType::kFLOAT) {
      throw std::runtime_error("RuneDetectorTRT expects FLOAT TensorRT input");
    }
    if (has_dynamic_dim(input_dims)) {
      if (!context->setBindingDimensions(input_index, nvinfer1::Dims4(1, 3, INPUT_H, INPUT_W))) {
        throw std::runtime_error("Failed to set TensorRT input dims");
      }
      input_dims = context->getBindingDimensions(input_index);
    }

    input_count = volume(input_dims);
    host_input.resize(input_count, "Failed to allocate pinned host input buffer");

    auto output_dims = context->getBindingDimensions(output_index);
    const auto output_dtype = engine->getBindingDataType(output_index);
    if (output_dtype != nvinfer1::DataType::kFLOAT) {
      throw std::runtime_error("RuneDetectorTRT expects FLOAT TensorRT output");
    }
    if (has_dynamic_dim(output_dims)) {
      throw std::runtime_error("TensorRT output dims are still dynamic after setting input dims");
    }

    output_count = volume(output_dims);
    host_output.resize(output_count, "Failed to allocate pinned host output buffer");
#endif

    check_cuda(
      cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Failed to create CUDA stream");
    check_cuda(
      cudaMalloc(&input_device, input_count * sizeof(float)), "Failed to allocate TensorRT input buffer");
    check_cuda(
      cudaMalloc(&output_device, output_count * sizeof(float)), "Failed to allocate TensorRT output buffer");

#if NV_TENSORRT_MAJOR < 10
    bindings[input_index] = input_device;
    bindings[output_index] = output_device;
#endif
  }

  cv::Mat infer(const cv::Mat & blob)
  {
    if (blob.empty()) {
      return {};
    }

    if (blob.type() != CV_32F) {
      throw std::runtime_error("RuneDetectorTRT expects CV_32F input blob");
    }

    if (static_cast<size_t>(blob.total()) != input_count) {
      throw std::runtime_error("TensorRT input blob size does not match engine input");
    }

    if (!blob.isContinuous()) {
      throw std::runtime_error("TensorRT input blob must be contiguous");
    }

    std::memcpy(host_input.data(), blob.ptr<float>(0), input_count * sizeof(float));
    check_cuda(
      cudaMemcpyAsync(
        input_device, host_input.data(), input_count * sizeof(float), cudaMemcpyHostToDevice,
        stream),
      "Failed to copy TensorRT input to device");

#if NV_TENSORRT_MAJOR >= 10
    if (!context->setTensorAddress(input_name.c_str(), input_device)) {
      throw std::runtime_error("Failed to set TensorRT input address");
    }
    if (!context->setTensorAddress(output_name.c_str(), output_device)) {
      throw std::runtime_error("Failed to set TensorRT output address");
    }
    if (!context->enqueueV3(stream)) {
      throw std::runtime_error("TensorRT enqueueV3 failed");
    }
    auto output_dims = context->getTensorShape(output_name.c_str());
#else
    if (!context->enqueueV2(bindings.data(), stream, nullptr)) {
      throw std::runtime_error("TensorRT enqueueV2 failed");
    }
    auto output_dims = context->getBindingDimensions(output_index);
#endif

    check_cuda(
      cudaMemcpyAsync(
        host_output.data(), output_device, output_count * sizeof(float), cudaMemcpyDeviceToHost,
        stream),
      "Failed to copy TensorRT output to host");
    check_cuda(cudaStreamSynchronize(stream), "Failed to sync TensorRT CUDA stream");

    if (has_dynamic_dim(output_dims)) {
      throw std::runtime_error("TensorRT output dims are dynamic at inference time");
    }

    std::vector<int> shape;
    shape.reserve(static_cast<size_t>(output_dims.nbDims));
    for (int i = 0; i < output_dims.nbDims; ++i) {
      if (output_dims.d[i] <= 0) {
        throw std::runtime_error("TensorRT output dims contains invalid values");
      }
      shape.emplace_back(output_dims.d[i]);
    }

    if (!shape.empty() && shape.front() == 1) {
      shape.erase(shape.begin());
    }

    if (shape.size() != 2) {
      throw std::runtime_error("Unexpected TensorRT output rank for RuneDetectorTRT");
    }

    int rows = shape[0];
    int cols = shape[1];
    bool need_transpose = false;
    if (rows == FEATURE_SIZE && cols != FEATURE_SIZE) {
      need_transpose = true;
    } else if (cols != FEATURE_SIZE) {
      throw std::runtime_error("Unexpected TensorRT output shape for RuneDetectorTRT");
    }

    if (need_transpose) {
      cv::Mat transposed;
      cv::transpose(cv::Mat(rows, cols, CV_32F, host_output.data()), transposed);
      output_cache = transposed;
      return output_cache;
    }

    output_cache = cv::Mat(rows, cols, CV_32F, host_output.data());
    return output_cache;
  }
#else
  void initialize(const std::string &)
  {
    throw std::runtime_error(
      "TensorRT is not enabled. Please install TensorRT and reconfigure CMake.");
  }

  cv::Mat infer(const cv::Mat &)
  {
    throw std::runtime_error(
      "TensorRT is not enabled. Please install TensorRT and reconfigure CMake.");
  }
#endif
};

RuneDetectorTRT::RuneDetectorTRT(const std::string & config_path) : impl_(std::make_unique<Impl>())
{
  auto yaml = YAML::LoadFile(config_path);
  auto node = yaml["buff_fyt_detector"];

  std::filesystem::path configured_model_path;
  if (node && node["trt_model"]) {
    configured_model_path = node["trt_model"].as<std::string>();
  } else if (
    node && node["model"] &&
    std::filesystem::path(node["model"].as<std::string>()).extension() == ".engine")
  {
    configured_model_path = node["model"].as<std::string>();
  } else {
    configured_model_path = "assets/yolox_rune_3.6m.engine";
  }

  if (configured_model_path.extension() != ".engine") {
    throw std::runtime_error(
      "TensorRT model path must point to a .engine file, but got: " +
      configured_model_path.string());
  }

  model_path_ = configured_model_path.string();
  conf_threshold_ =
    node && node["confidence_threshold"] ? node["confidence_threshold"].as<float>() : 0.7f;
  top_k_ = node && node["top_k"] ? node["top_k"].as<int>() : 128;
  nms_threshold_ =
    node && node["nms_threshold"] ? node["nms_threshold"].as<float>() : 0.3f;
  detect_r_tag_ = node && node["detect_r_tag"] ? node["detect_r_tag"].as<bool>() : true;
  binary_thresh_ = node && node["min_lightness"] ? node["min_lightness"].as<int>() : 100;
  r_tag_roi_half_size_ =
    node && node["r_tag_roi_half_size"] ? node["r_tag_roi_half_size"].as<int>() : 120;
  r_tag_max_distance_ =
    node && node["r_tag_max_distance"] ? node["r_tag_max_distance"].as<float>() : 80.0f;
  r_tag_min_area_ = node && node["r_tag_min_area"] ? node["r_tag_min_area"].as<float>() : 12.0f;

  init();
}

RuneDetectorTRT::~RuneDetectorTRT() = default;

void RuneDetectorTRT::init()
{
  strides_ = {8, 16, 32};
  grid_strides_.clear();
  generate_grids_and_stride(INPUT_W, INPUT_H, strides_, grid_strides_);
  impl_->initialize(model_path_);
  tools::logger()->info("[RuneDetectorTRT] loaded TensorRT engine from {}", model_path_);
}

std::vector<RuneObject> RuneDetectorTRT::detect(const cv::Mat & rgb_img)
{
  if (rgb_img.empty()) return {};

  Eigen::Matrix3f transform_matrix;
  cv::Mat resized_img = letterbox(rgb_img, transform_matrix);

  cv::Mat blob = cv::dnn::blobFromImage(
    resized_img, 1.0, cv::Size(INPUT_W, INPUT_H), cv::Scalar(0, 0, 0), true);

  std::lock_guard<std::mutex> lock(mtx_);
  cv::Mat output_buffer = impl_->infer(blob);

  std::vector<RuneObject> objects_tmp;
  std::vector<RuneObject> objects_result;
  std::vector<int> picked;

  generate_proposals(
    objects_tmp, output_buffer, transform_matrix, conf_threshold_, grid_strides_);

  std::sort(
    objects_tmp.begin(), objects_tmp.end(),
    [](const RuneObject & a, const RuneObject & b) { return a.prob > b.prob; });
  if (objects_tmp.size() > static_cast<size_t>(top_k_)) {
    objects_tmp.resize(static_cast<size_t>(top_k_));
  }

  nms_merge_sorted_bboxes(objects_tmp, picked, nms_threshold_);
  for (auto idx : picked) {
    objects_result.emplace_back(std::move(objects_tmp[idx]));
    auto & obj = objects_result.back();
    if (!obj.pts.children.empty()) {
      const float n = static_cast<float>(obj.pts.children.size() + 1);
      FeaturePoints merged =
        std::accumulate(obj.pts.children.begin(), obj.pts.children.end(), obj.pts);
      obj.pts = merged / n;
    }
  }

  return objects_result;
}

std::tuple<cv::Point2f, cv::Mat> RuneDetectorTRT::detect_r_tag(
  const cv::Mat & bgr_img, const cv::Point2f & prior) const
{
  if (
    prior.x < 0 || prior.x > bgr_img.cols || prior.y < 0 || prior.y > bgr_img.rows ||
    bgr_img.empty())
  {
    return {prior, cv::Mat::zeros(cv::Size(200, 200), CV_8UC3)};
  }

  const int roi_half_size = std::max(r_tag_roi_half_size_, 1);
  const cv::Rect roi =
    (cv::Rect(
       static_cast<int>(std::round(prior.x)) - roi_half_size,
       static_cast<int>(std::round(prior.y)) - roi_half_size, roi_half_size * 2,
       roi_half_size * 2) &
    cv::Rect(0, 0, bgr_img.cols, bgr_img.rows));
  const cv::Point2f prior_in_roi = prior - cv::Point2f(roi.tl());

  cv::Mat img_roi = bgr_img(roi);
  cv::Mat gray_img;
  cv::cvtColor(img_roi, gray_img, cv::COLOR_BGR2GRAY);
  cv::Mat binary_img;
  if (binary_thresh_ > 0) {
    cv::threshold(gray_img, binary_img, binary_thresh_, 255, cv::THRESH_BINARY);
  } else {
    cv::threshold(gray_img, binary_img, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
  }
  cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
  cv::dilate(binary_img, binary_img, kernel);

  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(binary_img, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

  cv::cvtColor(binary_img, binary_img, cv::COLOR_GRAY2BGR);
  cv::circle(binary_img, prior_in_roi, 3, cv::Scalar(255, 0, 0), -1);

  int best_idx = -1;
  float best_score = std::numeric_limits<float>::max();
  bool best_contains_prior = false;
  cv::Point2f center = prior_in_roi;

  for (int i = 0; i < static_cast<int>(contours.size()); ++i) {
    const auto & contour = contours[i];
    const float area = static_cast<float>(cv::contourArea(contour));
    if (area < r_tag_min_area_) continue;

    const cv::Moments moments = cv::moments(contour);
    if (std::abs(moments.m00) < 1e-6) continue;

    const cv::Point2f contour_center(
      static_cast<float>(moments.m10 / moments.m00), static_cast<float>(moments.m01 / moments.m00));
    const bool contains_prior = cv::pointPolygonTest(contour, prior_in_roi, false) >= 0.0;
    const float distance = cv::norm(contour_center - prior_in_roi);

    if (!contains_prior && distance > r_tag_max_distance_) continue;

    const float score = contains_prior ? distance : distance + r_tag_max_distance_;
    if (
      best_idx < 0 || (contains_prior && !best_contains_prior) ||
      (contains_prior == best_contains_prior && score < best_score))
    {
      best_idx = i;
      best_score = score;
      best_contains_prior = contains_prior;
      center = contour_center;
    }
  }

  if (best_idx < 0) return {prior, binary_img};

  cv::drawContours(binary_img, contours, best_idx, cv::Scalar(0, 255, 0), 2);
  center += cv::Point2f(roi.tl());
  return {center, binary_img};
}
}  // namespace auto_buff_fyt
