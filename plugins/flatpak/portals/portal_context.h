/*
 * Copyright 2023-2025 Toyota Connected North America
 * Copyright 2025 Ahmed Wafdy
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

#ifndef PORTAL_CONTEXT_H
#define PORTAL_CONTEXT_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "asio/io_context.hpp"
#include "asio/io_context_strand.hpp"
#include "portal_interface.h"

/**
 * \brief This struct track all resources associated to app.
 * Each app gets its own strand to execute apps concurrently.
 */
struct PortalContext {
  PortalContext(std::string id, asio::io_context& io_context)
      : app_id(std::move(id)),
        strand(std::make_unique<asio::io_context::strand>(io_context)) {}

  PortalContext(const PortalContext&) = delete;

  PortalContext& operator=(const PortalContext&) = delete;

  PortalContext(PortalContext&&) = default;

  PortalContext& operator=(PortalContext&&) = default;

  std::string app_id;
  std::vector<uint64_t> signal_sub_ids;
  std::vector<std::shared_ptr<sdbus::IProxy>> owned_proxies;
  std::unique_ptr<asio::io_context::strand> strand;
  std::vector<PortalInterface> interfaces;
};

#endif  // PORTAL_CONTEXT_H
