//
// Created by tcna on 3/26/25.
//

#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#pragma once

#include <pipewire/pipewire.h>
#include <mutex>
#include <thread>

/**
 * @brief A singleton manager that initializes and owns the shared
 * PipeWire main loop, context, and core connection.
 *
 * Typical usage:
 *   CameraManager::instance().initialize();   // Once, at startup
 *   // Create & use your CameraStream objects...
 *   CameraManager::instance().shutdown();     // At the end, if desired
 */
class CameraManager {
 public:
  static CameraManager& instance();

  /**
   * @brief Initializes PipeWire (if not already initialized).
   *        Creates main loop, context, core, and starts background thread.
   * @return true on success, false on failure.
   */
  bool initialize();

  /**
   * @brief Shuts down the PipeWire loop/context if currently running.
   *        Joins the background thread and de-initializes PipeWire.
   */
  void shutdown();

  /**
   * @brief Returns the shared pw_thread_loop*, or nullptr if not initialized.
   */
  pw_thread_loop* threadLoop() const { return pw_thread_loop_; }

  /**
   * @brief Returns the shared pw_context*, or nullptr if not initialized.
   */
  pw_context* context() const { return pw_context_; }

  /**
   * @brief Returns the shared pw_core*, or nullptr if not initialized.
   */
  pw_core* core() const { return pw_core_; }

 private:
  // Private constructor/destructor for singleton
  CameraManager();
  ~CameraManager();

  // Not copyable or assignable
  CameraManager(const CameraManager&) = delete;
  CameraManager& operator=(const CameraManager&) = delete;

 private:
  bool initialized_ = false;
  // pw_main_loop*  pw_loop_     = nullptr;
  pw_thread_loop* pw_thread_loop_ = nullptr;  // Instead of pw_main_loop
  pw_context* pw_context_ = nullptr;
  pw_core* pw_core_ = nullptr;
  mutable std::mutex mutex_;
};

#endif  // CAMERAMANAGER_H
