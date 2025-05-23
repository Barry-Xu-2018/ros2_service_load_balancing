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

#include <rclcpp/logging.hpp>

#include "common.hpp"
#include "service_client_proxy_manager.hpp"

ServiceClientProxyManager::ServiceClientProxyManager(
  const std::string & base_service_name,
  const std::string & service_type,
  rclcpp::Node::SharedPtr & node,
  ResponseReceiveQueue::SharedPtr & response_queue,
  std::chrono::seconds discovery_interval)
  : logger_(rclcpp::get_logger(class_name_)),
    base_service_name_(base_service_name),
    service_type_(service_type),
    node_(node),
    response_queue_(response_queue),
    discovery_interval_(discovery_interval)
{
}

ServiceClientProxyManager::~ServiceClientProxyManager()
{
  stop_discovery_thread_running();
}

void
ServiceClientProxyManager::start_discovery_service_servers_thread()
{
  auto discovery_service_server_thread =
    [this](){
      while (thread_exit_.load() == false) {
        // Return new service server list and removed service server list
        auto change_info = check_service_server_change();

        // found new load balancing service
        for (auto new_service : change_info.first) {
          auto client_proxy = create_service_proxy(new_service);
          if (register_new_client_proxy(client_proxy)) {
            add_new_load_balancing_service(new_service, client_proxy);
            RCLCPP_INFO(
              logger_,
              "Find a new service server \"%s\" and register client proxy %p.",
              new_service.c_str(), static_cast<void *>(client_proxy.get()));
          }
        }

        // found removed load balancing service
        for (auto removed_service: change_info.second) {
          auto client_proxy = get_created_client_proxy(removed_service);
          if (client_proxy != nullptr && unregister_client_proxy(client_proxy)) {
            remove_load_balancing_service(removed_service);
            RCLCPP_INFO(
              logger_,
              "Find a removed service server \"%s\" and unregister client proxy %p.",
              removed_service.c_str(), static_cast<void *>(client_proxy.get()));
          }
        }

        wait_for_request_to_check_service_servers();
      }
      RCLCPP_INFO(logger_, "Discovery service server thread exit.");
    };

  discovery_service_server_thread_ = std::thread(discovery_service_server_thread);

  // Use ros2 timer to periodically wake up the thread
  timer_ = node_->create_wall_timer(
    discovery_interval_,
    [this](){
      send_request_to_check_service_servers();
    });
}

void
ServiceClientProxyManager::set_client_proxy_change_callback(
  ClientProxyChangeCallbackType func_add,
  ClientProxyChangeCallbackType func_remove)
{
  std::lock_guard<std::mutex> lock(callback_mutex_);
  add_callback_ = std::move(func_add);
  remove_callback_ = std::move(func_remove);
}

bool
ServiceClientProxyManager::is_discovery_thread_running(void)
{
  return discovery_service_server_thread_.joinable();
}

void
ServiceClientProxyManager::stop_discovery_thread_running(void)
{
  thread_exit_.store(true);
  send_request_to_check_service_servers();
  if (discovery_service_server_thread_.joinable()) {
      discovery_service_server_thread_.join();
  }
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ServiceClientProxyManager::check_service_server_change()
{
  auto servers = node_->get_service_names_and_types();
  std::vector<std::string> new_servers;
  std::vector<std::string> del_servers;

  std::vector<std::string> matched_service_name_list;
  for (auto & [service_name, service_types] : servers) {
    // type must match
    auto found = std::find(service_types.begin(), service_types.end(), service_type_);
    if (found == service_types.end()) {
      continue;
    }

    if (is_load_balancing_service(base_service_name_, service_name)
      && node_->count_services(service_name) != 0) {
      matched_service_name_list.emplace_back(service_name);
    }
  }

  // Check if there is new load balancing service
  for (auto service_name : matched_service_name_list) {
    std::lock_guard<std::mutex> lock(registered_service_servers_info_mutex_);
    auto found = registered_service_servers_info_.count(service_name);
    if (!found) {
      new_servers.emplace_back(service_name);
    }
  }

  // Check if there is load balancing service is removed
  {
    std::lock_guard<std::mutex> lock(registered_service_servers_info_mutex_);
    for (const auto & info : registered_service_servers_info_) {
      if(!info.second->service_is_ready()) {
        del_servers.emplace_back(info.first);
      }
    }
  }

  return std::pair(std::move(new_servers), std::move(del_servers));
}

ServiceClientProxyManager::SharedClientProxy
ServiceClientProxyManager::create_service_proxy(const std::string service_name)
{
  return node_->create_generic_client(service_name, service_type_);
}

void
ServiceClientProxyManager::add_new_load_balancing_service(
 const std::string & new_services,
 ServiceClientProxyManager::SharedClientProxy & client_proxy)
{
  std::lock_guard<std::mutex> lock(registered_service_servers_info_mutex_);
  registered_service_servers_info_[new_services] = client_proxy;
}

void
ServiceClientProxyManager::remove_load_balancing_service(const std::string & remove_service)
{
  std::lock_guard<std::mutex> lock(registered_service_servers_info_mutex_);
  registered_service_servers_info_.erase(remove_service);
}

ServiceClientProxyManager::SharedClientProxy
ServiceClientProxyManager::get_created_client_proxy(const std::string & service_name)
{
  std::lock_guard<std::mutex> lock(registered_service_servers_info_mutex_);
  if (registered_service_servers_info_.count(service_name)) {
    return registered_service_servers_info_[service_name];
  } else {
    return nullptr;
  }
}

bool
ServiceClientProxyManager::register_new_client_proxy(SharedClientProxy & cli_proxy)
{
  if (!add_callback_) {
    return false;
  }

  add_callback_(cli_proxy);
  return true;
}

bool
ServiceClientProxyManager::unregister_client_proxy(SharedClientProxy & cli_proxy)
{
  if (!remove_callback_) {
    return false;
  }
  remove_callback_(cli_proxy);
  return true;
}

void
ServiceClientProxyManager::wait_for_request_to_check_service_servers()
{
  std::unique_lock lock(cond_mutex_);
  cv_.wait(lock);
}

void
ServiceClientProxyManager::send_request_to_check_service_servers()
{
  cv_.notify_one();
}

bool
ServiceClientProxyManager::async_send_request(
  SharedClientProxy & client_proxy,
  rclcpp::GenericService::SharedRequest & request,
  int64_t & sequence)
{
  auto send_index = get_send_index();

  auto callback = [this, send_index](rclcpp::GenericClient::SharedFuture future) {
    service_client_callback(future, send_index);
  };

  auto future = client_proxy->async_send_request(request.get(), callback);

  sequence = future.request_id;

  // Save send index and client proxy + sequence number
  {
    std::lock_guard<std::mutex> lock(client_proxy_futures_with_info_mutex_);
    client_proxy_futures_with_info_[send_index] =
      std::pair<SharedClientProxy, int64_t>(client_proxy, sequence);
  }

  return true;
}

void
ServiceClientProxyManager::service_client_callback(
  rclcpp::GenericClient::SharedFuture future,
  u_int64_t send_index)
{
  auto response = future.get();
  std::pair<SharedClientProxy, int64_t> client_proxy_and_sequence;
  // Remove send index
  {
    std::lock_guard<std::mutex> lock(client_proxy_futures_with_info_mutex_);
    client_proxy_and_sequence = client_proxy_futures_with_info_[send_index];
    client_proxy_futures_with_info_.erase(send_index);
  }

  // Put to response queue and MessageForwardManager handle it.
  response_queue_->enqueue(
    client_proxy_and_sequence.first,
    client_proxy_and_sequence.second,
    response);
}

uint64_t
ServiceClientProxyManager::get_send_index()
{
  auto id = proxy_send_request_index.load();
  proxy_send_request_index++;
  return id;
}
