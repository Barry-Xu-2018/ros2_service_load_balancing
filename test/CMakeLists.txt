find_package(ament_cmake_gtest REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_srvs REQUIRED)
find_package(example_interfaces REQUIRED)

# finding gtest once in the highest scope
# prevents finding it repeatedly in each local scope
ament_find_gtest()

ament_add_gtest(test_common
  test_common.cpp
  ../src/common.cpp
)
target_link_libraries(test_common rclcpp::rclcpp)

ament_add_gtest(test_data_queue
  test_data_queue.cpp
)
target_link_libraries(test_data_queue rclcpp::rclcpp)

ament_add_gtest(test_forward_management
  test_forward_management.cpp
  ../src/forward_management.cpp
)
target_link_libraries(test_forward_management rclcpp::rclcpp ${std_srvs_TARGETS})

ament_add_gtest(test_service_server_proxy
  test_service_server_proxy.cpp
  ../src/service_server_proxy.cpp
)
target_link_libraries(test_service_server_proxy rclcpp::rclcpp ${std_srvs_TARGETS})

ament_add_gtest(test_service_client_proxy_manager
  test_service_client_proxy_manager.cpp
  ../src/common.cpp
  ../src/service_client_proxy_manager.cpp
)
target_link_libraries(test_service_client_proxy_manager rclcpp::rclcpp ${std_srvs_TARGETS})

ament_add_gtest(test_message_forward_process
  test_message_forward_process.cpp
  ../src/common.cpp
  ../src/forward_management.cpp
  ../src/service_client_proxy_manager.cpp
  ../src/service_server_proxy.cpp
  ../src/message_forward_process.cpp
)
target_link_libraries(test_message_forward_process rclcpp::rclcpp ${example_interfaces_TARGETS})
