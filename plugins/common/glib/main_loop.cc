/*
 * Copyright 2023-2024 Toyota Connected North America
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

#include "main_loop.h"

namespace plugin_common_glib {

MainLoop::MainLoop()
    : gthread_(std::make_unique<std::thread>(main_loop, this)) {}

MainLoop::~MainLoop() {
  if (gthread_ && gthread_->joinable()) {
    // The loop polls g_main_context_iteration(context_, TRUE) which may
    // block forever inside ppoll waiting for a glib event. Without the
    // exit_loop_ flip + a wakeup, ~MainLoop's join() hangs at atexit
    // (the singleton instance is destructed by the C++ runtime after
    // main() returns).
    exit_loop_ = true;
    if (context_ != nullptr) {
      g_main_context_wakeup(context_);
    }
    gthread_->join();
  }
}

const MainLoop& MainLoop::GetInstance() {
  static MainLoop sInstance;
  return sInstance;
};

void MainLoop::main_loop(MainLoop* data) {
  data->context_ = g_main_context_default();

  data->is_running_ = true;
  while (data->is_running_) {
    // Block until a glib event or ~MainLoop's g_main_context_wakeup(). Check
    // the exit flag AFTER the iteration, not before: ~MainLoop sets exit_loop_
    // and issues a single wakeup that unblocks exactly one iteration. Checking
    // before would set is_running_ = false and then fall into another blocking
    // g_main_context_iteration(TRUE) that never returns (the wakeup is already
    // consumed), parking the thread forever and hanging ~MainLoop's join() at
    // atexit. Checking after lets the wakeup-returning iteration exit the loop.
    g_main_context_iteration(data->context_, TRUE);
    if (data->exit_loop_) {
      data->is_running_ = false;
    }
  }

  g_main_loop_quit(data->main_loop_);
  g_main_loop_unref(data->main_loop_);
}

}  // namespace plugin_common_glib
