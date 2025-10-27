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

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <rclcpp/executors/single_threaded_executor.hpp>
#include <example_interfaces/srv/add_two_ints.hpp>

#include "../src/data_queues.hpp"
#include "../src/forward_management.hpp"
#include "../src/message_forward_process.hpp"
#include "../src/service_client_proxy_manager.hpp"
#include "../src/service_server_proxy.hpp"

class TestMessageForwardProcess : public ::testing::Test
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
    node1_ = std::make_shared<rclcpp::Node>("test_node_load_balancing",
      rclcpp::NodeOptions().start_parameter_services(false));
    node2_ = std::make_shared<rclcpp::Node>("test_node",
      rclcpp::NodeOptions().start_parameter_services(false));
  }

  void TearDown()
  {
    node1_.reset();
  }

  rclcpp::Node::SharedPtr node1_;
  rclcpp::Node::SharedPtr node2_;
};

TEST_F(TestMessageForwardProcess, test_complete_request_send_and_response_receive)
{
  const std::string service_name = "test_service";
  const std::string service_type = "example_interfaces/srv/AddTwoInts";

  // Launch load-balancing service
  auto request_queue = std::make_shared<RequestReceiveQueue>();
  auto response_queue = std::make_shared<ResponseReceiveQueue>();

  auto forward_management =
    std::make_shared<ForwardManagement>(LoadBalancingStrategy::ROUND_ROBIN);

  auto client_proxy_manager =
    std::make_shared<ServiceClientProxyManager>(
      service_name, service_type, node1_, response_queue);

  // When a new service server is added or removed, the corresponding client proxy will be created
  // and removed. ServiceClientProxyManager will notify the change to ForwardManagement.
  client_proxy_manager->set_client_proxy_change_callback(
    [&forward_management](ServiceClientProxyManager::SharedClientProxy & cli_proxy) -> bool {
      return forward_management->register_client_proxy(cli_proxy);
    },
    [&forward_management](ServiceClientProxyManager::SharedClientProxy & cli_proxy) -> bool {
      return forward_management->unregister_client_proxy(cli_proxy);
    });

  auto service_proxy =
    std::make_shared<ServiceServerProxy>(service_name, service_type, node1_, request_queue);

  auto message_forward_process =
    std::make_shared<MessageForwardProcess>(
      service_proxy, client_proxy_manager, forward_management, request_queue, response_queue);

  // Start to discovery service server
  client_proxy_manager->start_discovery_service_servers_thread();

  std::atomic_bool exit_thread = false;
  std::thread run_spin_thread([&exit_thread, this](){
    while(!exit_thread.load()) {
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.spin_node_some(node1_);
    }
  });

  // Launch test service server and client
  auto callback1 =
    [](example_interfaces::srv::AddTwoInts_Request::SharedPtr request,
      example_interfaces::srv::AddTwoInts_Response::SharedPtr response)
    {
      response->sum = request->a + request->b;
    };
  const std::string load_balancing_service_name_base = "/load_balancing/" + service_name;
  // Create 2 service servers

  auto server1 = node1_->create_service<example_interfaces::srv::AddTwoInts>(
    load_balancing_service_name_base + "/s1", std::move(callback1));

  auto callback2 =
    [](example_interfaces::srv::AddTwoInts_Request::SharedPtr request,
      example_interfaces::srv::AddTwoInts_Response::SharedPtr response)
    {
      response->sum = request->a + request->b + 100;
    };
  auto server2 = node1_->create_service<example_interfaces::srv::AddTwoInts>(
    load_balancing_service_name_base + "/s2", std::move(callback2));

  // Make sure load balancing service servers are found.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  // Create 4 service clients
  std::atomic_uint8_t response_from_service1 = 0;
  std::atomic_uint8_t response_from_service2 = 0;
  auto client1 =
    node2_->create_client<example_interfaces::srv::AddTwoInts>(load_balancing_service_name_base);
  auto client2 =
    node2_->create_client<example_interfaces::srv::AddTwoInts>(load_balancing_service_name_base);
  auto client3 =
    node2_->create_client<example_interfaces::srv::AddTwoInts>(load_balancing_service_name_base);
  auto client4 =
    node2_->create_client<example_interfaces::srv::AddTwoInts>(load_balancing_service_name_base);

  ASSERT_TRUE(client1->service_is_ready());
  ASSERT_TRUE(client2->service_is_ready());
  ASSERT_TRUE(client3->service_is_ready());
  ASSERT_TRUE(client4->service_is_ready());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node2_);

  using SharedFuture = rclcpp::Client<example_interfaces::srv::AddTwoInts>::SharedFuture;

  auto request = std::make_shared<example_interfaces::srv::AddTwoInts_Request>();
  request->a = 1;
  request->b = 0;

  auto client_callback1 =
    [&response_from_service1, &response_from_service2](SharedFuture future_response) {
      EXPECT_TRUE(future_response.get()->sum == 1 || future_response.get()->sum == 101);
      if (future_response.get()->sum == 1) {
        response_from_service1++;
      } else {
        response_from_service2++;
      }
    };
  //auto future1 = client1->async_send_request(static_cast<void *>(&typed_request));
  auto future1 = client1->async_send_request(request, client_callback1);
  executor.spin_until_future_complete(future1, std::chrono::milliseconds(100));

  request->a = 2;
  auto client_callback2 =
    [&response_from_service1, &response_from_service2](SharedFuture future_response) {
      EXPECT_TRUE(future_response.get()->sum == 2 || future_response.get()->sum == 102);
      if (future_response.get()->sum == 2) {
        response_from_service1++;
      } else {
        response_from_service2++;
      }
    };
  auto future2 = client2->async_send_request(request, client_callback2);
  executor.spin_until_future_complete(future2, std::chrono::milliseconds(100));

  request->a = 3;
  auto client_callback3 =
    [&response_from_service1, &response_from_service2](SharedFuture future_response) {
      EXPECT_TRUE(future_response.get()->sum == 3 || future_response.get()->sum == 103);
      if (future_response.get()->sum == 3) {
        response_from_service1++;
      } else {
        response_from_service2++;
      }
    };
  auto future3 = client3->async_send_request(request, client_callback3);
  executor.spin_until_future_complete(future3, std::chrono::milliseconds(100));

  request->a = 4;
  auto client_callback4 =
    [&response_from_service1, &response_from_service2](SharedFuture future_response) {
      EXPECT_TRUE(future_response.get()->sum == 4 || future_response.get()->sum == 104);
      if (future_response.get()->sum == 4) {
        response_from_service1++;
      } else {
        response_from_service2++;
      }
    };
  auto future4 = client4->async_send_request(request, client_callback4);
  executor.spin_until_future_complete(future4, std::chrono::milliseconds(100));

  // Since the LoadBalancingStrategy::ROUND_ROBIN is being used, the requests from the 4 clients
  // will be evenly distributed between the 2 servers for processing.
  EXPECT_TRUE(response_from_service1 == response_from_service2);

  exit_thread.store(true);
  if (run_spin_thread.joinable()) {
    run_spin_thread.join();
  }

  // Shutdown queue
  request_queue->shutdown();
  response_queue->shutdown();
}
