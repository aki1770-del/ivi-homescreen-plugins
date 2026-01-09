/*
 * Copyright 2020-2026 Toyota Connected North America
 * Copyright 2026 Ahmed Wafdy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FLUTTER_PLUGIN_FLATPAK_OPERATION_TRACKER_H
#define FLUTTER_PLUGIN_FLATPAK_OPERATION_TRACKER_H

#include <string>
#include <set>
#include <map>

#include "asio/io_context.hpp"
#include "flatpak_shim.h"

namespace flatpak_plugin {

class OperationTracker {
public:
  struct OperationInfo {
    std::string app_id;
    std::string op_type;
    bool is_app = false;
    int total_ops;
    int completed_ops;
    std::set<std::string> completed_ref;
  };
  explicit OperationTracker(
      asio::io_context& io_context,
      std::function<void(const flutter::EncodableMap&)> send_event_callback);


  ~OperationTracker() = default;

  OperationTracker(const OperationTracker&) = delete;
  OperationTracker& operator=(const OperationTracker&) = delete;
  OperationTracker(OperationTracker&&) = delete;
  OperationTracker& operator=(OperationTracker&&) = delete;

  std::map<std::string, OperationInfo> GetActiveOperations() const;

  void TrackOperationStart(const std::string& app_id,const std::string& op_type);

  void TrackOperationProgress(const std::string& app_id,const std::string& ref,int progress);

  void UpdateTotalOperations(const std::string& app_id,int total_ops);

  void TrackOperationComplete(const std::string& app_id,const std::string& ref);

  void SendOperationFinish(const std::string& app_id,const std::string& op_type,bool success);

  void ClearOperation(const std::string& app_id);
private:

  asio::io_context& io_context_;
  std::function<void(const flutter::EncodableMap&)> send_event_callback_;
  std::map<std::string,OperationInfo> active_ops_;
};
}  // namespace flatpak_plugin

#endif  // FLUTTER_PLUGIN_FLATPAK_OPERATION_TRACKER_H
