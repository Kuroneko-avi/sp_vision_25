add_library(auto_buff_fyt OBJECT
    rune_detector.cpp
    rune_detector_trt.cpp
    buff_detector.cpp
    buff_detector_trt.cpp
    buff_solver.cpp
)

target_link_libraries(auto_buff_fyt openvino::runtime)

find_path(
    TENSORRT_INCLUDE_DIR
    NAMES NvInfer.h
    HINTS /usr/include /usr/include/aarch64-linux-gnu /usr/include/x86_64-linux-gnu /usr/local/include /usr/src/tensorrt/include
)
find_library(
    TENSORRT_NVINFER_LIBRARY
    NAMES nvinfer
    HINTS /usr/lib /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/local/lib
)
find_library(
    TENSORRT_CUDART_LIBRARY
    NAMES cudart
    HINTS /usr/lib /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/local/cuda/lib64
)
find_path(
    CUDA_RUNTIME_INCLUDE_DIR
    NAMES cuda_runtime_api.h
    HINTS /usr/include /usr/local/cuda/include
)

if(TENSORRT_INCLUDE_DIR AND CUDA_RUNTIME_INCLUDE_DIR AND TENSORRT_NVINFER_LIBRARY AND TENSORRT_CUDART_LIBRARY)
    message(STATUS "TensorRT found, enabling auto_buff_fyt TRT backend")
    target_compile_definitions(auto_buff_fyt PRIVATE AUTO_BUFF_FYT_ENABLE_TENSORRT=1)
    target_include_directories(auto_buff_fyt PRIVATE ${TENSORRT_INCLUDE_DIR} ${CUDA_RUNTIME_INCLUDE_DIR})
    target_link_libraries(auto_buff_fyt ${TENSORRT_NVINFER_LIBRARY} ${TENSORRT_CUDART_LIBRARY})
else()
    message(WARNING "TensorRT not found, auto_buff_fyt TRT backend will throw at runtime if selected")
endif()
