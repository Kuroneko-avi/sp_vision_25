find_package(Ceres REQUIRED)

add_library(auto_aim_eskf OBJECT
    planner.cpp
    target.cpp
    tracker.cpp
)

target_link_libraries(auto_aim_eskf
    PUBLIC
        auto_aim
        omniperception
        tools
        io
        Ceres::ceres
)
