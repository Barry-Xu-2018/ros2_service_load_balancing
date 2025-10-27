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

#include <rclcpp/executors/single_threaded_executor.hpp>

#include "../src/service_client_proxy_manager.hpp"

class TestServiceClientProxyManager : public ::testing::Test
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

TEST_F(TestServiceClientProxyManager, test_constructor_and_destructor)
{
  auto response_queue = std::make_shared<ResponseReceiveQueue>();

  EXPECT_NO_THROW(
    auto client_proxy_mgr = std::make_shared<ServiceClientProxyManager>(
      "test_service", "std_srvs/srv/Empty", node_, response_queue););
}

TEST_F(TestServiceClientProxyManager, test_is_discovery_thread_running)
{
  auto response_queue = std::make_shared<ResponseReceiveQueue>();

  ServiceClientProxyManager::SharedPtr client_proxy_mgr;
  ASSERT_NO_THROW(
    client_proxy_mgr = std::make_shared<ServiceClientProxyManager>(
      "test_service", "std_srvs/srv/Empty", node_, response_queue););

  // Start discovery thread
  client_proxy_mgr->start_discovery_service_servers_thread();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_TRUE(client_proxy_mgr->is_discovery_thread_running());
}

TEST_F(TestServiceClientProxyManager, test_stop_discovery_thread_running)
{
  auto response_queue = std::make_shared<ResponseReceiveQueue>();

  ServiceClientProxyManager::SharedPtr client_proxy_mgr;
  ASSERT_NO_THROW(
    client_proxy_mgr = std::make_shared<ServiceClientProxyManager>(
      "test_service", "std_srvs/srv/Empty", node_, response_queue););

  // Start discovery thread
  client_proxy_mgr->start_discovery_service_servers_thread();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(client_proxy_mgr->is_discovery_thread_running());

  client_proxy_mgr->stop_discovery_thread_running();

  EXPECT_FALSE(client_proxy_mgr->is_discovery_thread_running());
}

TEST_F(TestServiceClientProxyManager, test_check_service_server_change_notification)
{
  auto response_queue = std::make_shared<ResponseReceiveQueue>();

  ServiceClientProxyManager::SharedPtr client_proxy_mgr;
  ASSERT_NO_THROW(
    client_proxy_mgr = std::make_shared<ServiceClientProxyManager>(
      "test_service", "std_srvs/srv/Empty", node_, response_queue););

  std::atomic_uint32_t service_client_count = 0;

  auto add_service_client_proxy_func =
    [& service_client_count](
      ServiceClientProxyManager::SharedClientProxy & shared_client_proxy)-> bool {
      (void) shared_client_proxy;
      service_client_count++;
      return true;
    };
  auto remove_service_client_proxy_func =
    [& service_client_count](
      ServiceClientProxyManager::SharedClientProxy & shared_client_proxy)-> bool {
      (void) shared_client_proxy;
      service_client_count--;
      return true;
    };

  client_proxy_mgr->set_client_proxy_change_callback(
    add_service_client_proxy_func, remove_service_client_proxy_func);

  client_proxy_mgr->start_discovery_service_servers_thread();

  const std::string load_balancing_service_name_base = "/load_balancing/test_service/";
  auto callback = [](
    rclcpp::GenericService::SharedRequest, rclcpp::GenericService::SharedResponse) {};
  auto service_server_1 = node_->create_generic_service(
   load_balancing_service_name_base + "s1" , "std_srvs/srv/Empty", callback);
  auto service_server_2 = node_->create_generic_service(
   load_balancing_service_name_base + "s2" , "std_srvs/srv/Empty", callback);
  auto service_server_3 = node_->create_generic_service(
   load_balancing_service_name_base + "s3" , "std_srvs/srv/Empty", callback);

  {
    std::thread run_spin_thread([this](){
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.spin_node_all(node_, std::chrono::milliseconds(200));
    });
    run_spin_thread.join();
  }
  client_proxy_mgr->send_request_to_check_service_servers();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Should call add_service_client_proxy_func 3 times
  EXPECT_EQ(service_client_count, 3);

  // Remove one load balancing service server
  service_server_2.reset();
  {
    std::thread run_spin_thread([this](){
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.spin_node_all(node_, std::chrono::milliseconds(200));
    });
    run_spin_thread.join();
  }
  client_proxy_mgr->send_request_to_check_service_servers();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(service_client_count, 2);

  // Remove the remaining 2 load balancing service server
  service_server_1.reset();
  service_server_3.reset();

  {
    std::thread run_spin_thread([this](){
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.spin_node_all(node_, std::chrono::milliseconds(200));
    });
    run_spin_thread.join();
  }
  client_proxy_mgr->send_request_to_check_service_servers();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(service_client_count, 0);

  //client_proxy_mgr->stop_discovery_thread_running();
}

TEST_F(TestServiceClientProxyManager, test_get_created_client_proxy)
{
  auto response_queue = std::make_shared<ResponseReceiveQueue>();

  ServiceClientProxyManager::SharedPtr client_proxy_mgr;
  ASSERT_NO_THROW(
    client_proxy_mgr = std::make_shared<ServiceClientProxyManager>(
      "test_service", "std_srvs/srv/Empty", node_, response_queue););

  auto add_service_client_proxy_func =
    [](ServiceClientProxyManager::SharedClientProxy & shared_client_proxy)-> bool {
      (void) shared_client_proxy;
      return true;
    };
  auto remove_service_client_proxy_func =
    [](ServiceClientProxyManager::SharedClientProxy & shared_client_proxy)-> bool {
      (void) shared_client_proxy;
      return true;
    };

  client_proxy_mgr->set_client_proxy_change_callback(
    add_service_client_proxy_func, remove_service_client_proxy_func);

  client_proxy_mgr->start_discovery_service_servers_thread();

  {
    std::thread run_spin_thread([this](){
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.spin_node_all(node_, std::chrono::milliseconds(200));
    });
    run_spin_thread.join();
  }
  client_proxy_mgr->send_request_to_check_service_servers();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const std::string load_balancing_service_name_base = "/load_balancing/test_service/";

  EXPECT_EQ(
    client_proxy_mgr->get_created_client_proxy(
      load_balancing_service_name_base + "s1").get(), nullptr);

  auto callback = [](
    rclcpp::GenericService::SharedRequest, rclcpp::GenericService::SharedResponse) {};
  auto service_server_1 = node_->create_generic_service(
   load_balancing_service_name_base + "s1" , "std_srvs/srv/Empty", callback);

  {
    std::thread run_spin_thread([this](){
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.spin_node_all(node_, std::chrono::milliseconds(200));
    });
    run_spin_thread.join();
  }
  client_proxy_mgr->send_request_to_check_service_servers();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_TRUE(
    client_proxy_mgr->get_created_client_proxy(
      load_balancing_service_name_base + "s1").get() != nullptr);
}

TEST_F(TestServiceClientProxyManager, test_remove_load_balancing_service)
{
  auto response_queue = std::make_shared<ResponseReceiveQueue>();

  ServiceClientProxyManager::SharedPtr client_proxy_mgr;
  ASSERT_NO_THROW(
    client_proxy_mgr = std::make_shared<ServiceClientProxyManager>(
      "test_service", "std_srvs/srv/Empty", node_, response_queue););

  auto add_service_client_proxy_func =
    [](ServiceClientProxyManager::SharedClientProxy & shared_client_proxy)-> bool {
      (void) shared_client_proxy;
      return true;
    };
  auto remove_service_client_proxy_func =
    [](ServiceClientProxyManager::SharedClientProxy & shared_client_proxy)-> bool {
      (void) shared_client_proxy;
      return true;
    };

  client_proxy_mgr->set_client_proxy_change_callback(
    add_service_client_proxy_func, remove_service_client_proxy_func);

  client_proxy_mgr->start_discovery_service_servers_thread();

  const std::string load_balancing_service_name_base = "/load_balancing/test_service/";
  const std::string to_be_removed_service_name = load_balancing_service_name_base + "s3";
  auto callback = [](
    rclcpp::GenericService::SharedRequest, rclcpp::GenericService::SharedResponse) {};
  auto service_server_1 = node_->create_generic_service(
   load_balancing_service_name_base + "s1" , "std_srvs/srv/Empty", callback);
  auto service_server_2 = node_->create_generic_service(
   load_balancing_service_name_base + "s2" , "std_srvs/srv/Empty", callback);
  auto service_server_3 = node_->create_generic_service(
   to_be_removed_service_name , "std_srvs/srv/Empty", callback);

  std::thread run_spin_thread([this](){
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.spin_node_all(node_, std::chrono::milliseconds(200));
  });
  run_spin_thread.join();
  client_proxy_mgr->send_request_to_check_service_servers();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Check if service client proxy exist for service name
  ASSERT_NE(
    client_proxy_mgr->get_created_client_proxy(
      to_be_removed_service_name).get(), nullptr);

  client_proxy_mgr->remove_load_balancing_service(to_be_removed_service_name);

  EXPECT_EQ(
    client_proxy_mgr->get_created_client_proxy(
      to_be_removed_service_name), nullptr);
}
