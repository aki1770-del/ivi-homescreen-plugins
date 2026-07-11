/*
 * Copyright 2020-2026 Toyota Connected North America
 * Copyright 2026 Ahmed Wafdy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "operation_tracker.h"

#include "asio/post.hpp"
#include "logging/logging.h"

namespace flatpak_plugin {

OperationTracker::OperationTracker(
    asio::io_context& io_context,
    std::function<void(const flutter::EncodableMap&)> send_event_callback)
    : io_context_(io_context),
      send_event_callback_(std::move(send_event_callback)) {}

std::map<std::string, OperationTracker::OperationInfo>
OperationTracker::GetActiveOperations() const {
  return active_ops_;
}

void OperationTracker::TrackOperationStart(const std::string& app_id,
                                           const std::string& op_type) {
  asio::post(io_context_, [this, app_id, op_type]() {
    OperationInfo op_info;
    op_info.app_id = app_id;
    op_info.op_type = op_type;
    op_info.total_ops = 0;
    op_info.completed_ops = 0;
    op_info.required_bytes = 0;  // Initialize
    active_ops_[app_id] = op_info;

    ihs::log::debug("[FlatpakPlugin] Operation {} Tracker started for {}",
                    op_type, app_id);
  });
}

void OperationTracker::SetOperationSize(const std::string& app_id,
                                        uint64_t size_bytes) {
  asio::post(io_context_, [this, app_id, size_bytes]() {
    auto it = active_ops_.find(app_id);
    if (it != active_ops_.end()) {
      total_pending_bytes_ -= it->second.required_bytes;
      it->second.required_bytes = size_bytes;
      total_pending_bytes_ += size_bytes;

      ihs::log::debug(
          "[FlatpakPlugin] Cached required size {} bytes for app {}. Total "
          "queue pending: {}",
          size_bytes, app_id, total_pending_bytes_.load());
    }
  });
}

uint64_t OperationTracker::GetTotalPendingSize() const {
  return total_pending_bytes_.load();
}

void OperationTracker::TrackOperationProgress(const std::string& app_id,
                                              const std::string& ref,
                                              int progress) {
  asio::post(io_context_, [this, app_id, ref, progress]() {
    auto it = active_ops_.find(app_id);
    if (it == active_ops_.end()) {
      return;
    }
    OperationInfo& op_info = it->second;

    int overall_progress = 0;
    if (op_info.total_ops > 0) {
      overall_progress =
          ((op_info.completed_ops * 100) + progress) / op_info.total_ops;
    } else {
      overall_progress = progress;
    }

    overall_progress = std::max(0, std::min(100, overall_progress));
    flutter::EncodableMap progress_event;
    progress_event[flutter::EncodableValue("type")] =
        flutter::EncodableValue("aggregated_progress");
    progress_event[flutter::EncodableValue("app_id")] =
        flutter::EncodableValue(app_id);
    progress_event[flutter::EncodableValue("progress")] =
        flutter::EncodableValue(overall_progress);
    progress_event[flutter::EncodableValue("completed_operations")] =
        flutter::EncodableValue(op_info.completed_ops);
    progress_event[flutter::EncodableValue("total_operations")] =
        flutter::EncodableValue(op_info.total_ops);
    progress_event[flutter::EncodableValue("current_ref")] =
        flutter::EncodableValue(ref);
    if (send_event_callback_) {
      send_event_callback_(progress_event);
    }
  });
}

void OperationTracker::UpdateTotalOperations(const std::string& app_id,
                                             int total_ops) {
  asio::post(io_context_, [this, app_id, total_ops]() {
    auto it = active_ops_.find(app_id);
    if (it == active_ops_.end()) {
      ihs::log::error("[FlatpakPlugin] can't update total operations for {}",
                      app_id);
      return;
    }

    it->second.total_ops = total_ops;
    ihs::log::debug("[FlatpakPlugin] Updated total operations {} for app {}",
                    total_ops, app_id);
  });
}

void OperationTracker::TrackOperationComplete(const std::string& app_id,
                                              const std::string& ref) {
  asio::post(io_context_, [this, app_id, ref]() {
    auto it = active_ops_.find(app_id);
    if (it == active_ops_.end()) {
      return;
    }
    OperationInfo& op_info = it->second;
    op_info.completed_ops++;
    op_info.completed_ref.insert(ref);
    ihs::log::debug("[FlatpakPlugin] Operation Progress for {}: {}/{}", app_id,
                    op_info.completed_ops, op_info.total_ops);

    if (ref.find(app_id) != std::string::npos) {
      op_info.is_app = true;
      ihs::log::info("[FlatpakPlugin] Application Operations completed: {}",
                     app_id);
    }
  });
}

void OperationTracker::SendOperationFinish(const std::string& app_id,
                                           const std::string& op_type,
                                           bool success) {
  asio::post(io_context_, [this, app_id, op_type, success]() {
    auto it = active_ops_.find(app_id);
    if (it == active_ops_.end()) {
      return;
    }
    OperationInfo& op_info = it->second;
    ihs::log::info(
        "[FlatpakPlugin] {} operation for {} completed, success:{}, total "
        "operations: {}, completed operations:{}",
        op_type, app_id, success, op_info.total_ops, op_info.completed_ops);
    flutter::EncodableMap summary_event;
    summary_event[flutter::EncodableValue("type")] =
        flutter::EncodableValue("operation_summary");
    summary_event[flutter::EncodableValue("app_id")] =
        flutter::EncodableValue(app_id);
    summary_event[flutter::EncodableValue("operation_type")] =
        flutter::EncodableValue(op_type);
    summary_event[flutter::EncodableValue("success")] =
        flutter::EncodableValue(success);
    summary_event[flutter::EncodableValue("operations_completed")] =
        flutter::EncodableValue(op_info.completed_ops);
    summary_event[flutter::EncodableValue("total_operations")] =
        flutter::EncodableValue(op_info.total_ops);

    if (send_event_callback_) {
      send_event_callback_(summary_event);
    }

    // Decrease total pending size
    total_pending_bytes_ -= it->second.required_bytes;
    active_ops_.erase(it);
  });
}

void OperationTracker::ClearOperation(const std::string& app_id) {
  asio::post(io_context_, [this, app_id]() {
    auto it = active_ops_.find(app_id);
    if (it != active_ops_.end()) {
      total_pending_bytes_ -= it->second.required_bytes;
      active_ops_.erase(it);
      ihs::log::debug("[FlatpakPlugin] clear operation for {}", app_id);
    }
  });
}
}  // namespace flatpak_plugin
