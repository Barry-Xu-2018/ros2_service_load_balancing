cmake_minimum_required(VERSION 3.8)
project(service_load_balancing)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)

add_executable(service_load_balancing
  src/common.cpp
  src/forward_management.cpp
  src/message_forward_process.cpp
  src/service_client_proxy_manager.cpp
  src/service_server_proxy.cpp
  src/main.cpp
)
target_link_libraries(service_load_balancing rclcpp::rclcpp)

install(TARGETS
  service_load_balancing
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()

ament_package()
