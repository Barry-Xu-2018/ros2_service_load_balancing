// Copyright 2024 Sony Group Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include <rclcpp/executors/single_threaded_executor.hpp>
#include <std_srvs/srv/empty.hpp>

#include "../src/service_server_proxy.hpp"

class TestServiceServerProxy : public ::testing::Test
{
protected:
  static void SetUpTestCase()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestCase()
  {
    rclcpp::shutdown();
  }

  void SetUp()
  {
    node_ = std::make_shared<rclcpp::Node>("test_node",
      rclcpp::NodeOptions().start_parameter_services(false));
  }

  void TearDown()
  {
    node_.reset();
  }

  rclcpp::Node::SharedPtr node_;
};

TEST_F(TestServiceServerProxy, test_constructor)
{
  auto request_queue = std::make_shared<RequestReceiveQueue>();

  EXPECT_NO_THROW(
    auto service_proxy = std::make_shared<ServiceServerProxy>(
      "test_service", "std_srvs/srv/Empty", node_, request_queue););

  EXPECT_THROW(
    auto service_proxy = std::make_shared<ServiceServerProxy>(
      "test_service", "std_srvs/srv/NotExist", node_, request_queue);, std::runtime_error);
}

TEST_F(TestServiceServerProxy, test_get_service_name)
{
  auto request_queue = std::make_shared<RequestReceiveQueue>();
  const std::string service_name = "test_service";

  ServiceServerProxy::SharedPtr service_proxy;
  ASSERT_NO_THROW(
    service_proxy = std::make_shared<ServiceServerProxy>(
      "test_service", "std_srvs/srv/Empty", node_, request_queue););

  const std::string expected_load_balancing_service_name = "/load_balancing/" + service_name;

  EXPECT_TRUE(service_proxy->get_service_name() == expected_load_balancing_service_name);
}

TEST_F(TestServiceServerProxy, test_service_server_receive_request)
{
  auto request_queue = std::make_shared<RequestReceiveQueue>();
  const std::string service_name = "test_service";

  ServiceServerProxy::SharedPtr service_proxy;
  ASSERT_NO_THROW(
    service_proxy = std::make_shared<ServiceServerProxy>(
      "test_service", "std_srvs/srv/Empty", node_, request_queue););

  const std::string load_balancing_service_name = "/load_balancing/" + service_name;
  auto test_client1 = node_->create_generic_client(
    load_balancing_service_name, "std_srvs/srv/Empty");
  ASSERT_TRUE(test_client1->wait_for_service(std::chrono::milliseconds(100)));

  auto test_client2 = node_->create_generic_client(
    load_balancing_service_name, "std_srvs/srv/Empty");
  ASSERT_TRUE(test_client2->wait_for_service(std::chrono::milliseconds(100)));

  std_srvs::srv::Empty::Request request;
  auto future1 = test_client1->async_send_request(static_cast<void *>(&request));
  auto future2 = test_client2->async_send_request(static_cast<void *>(&request));

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.spin_node_all(node_, std::chrono::milliseconds(100));

  // Should get 2 request in request queue
  EXPECT_TRUE(request_queue->queue_size() == 2);
}
