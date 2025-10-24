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

#ifndef PORTAL_INTERFACE_H
#define PORTAL_INTERFACE_H

#include <string>
#include <tuple>

/**
 * \brief This enum Representing and Identify different D-Bus portal interface.
 */
enum class BUS_TYPE { SESSION, SYSTEM };

/**
 * \brief This struct represents the portal interface description.
 */
struct PortalInterface {
  std::string interface_name;
  std::string object_path;
  std::string service_name;
  BUS_TYPE bus_type;

  bool operator==(const PortalInterface& other) const {
    return interface_name == other.interface_name && bus_type == other.bus_type;
  }

  bool operator<(const PortalInterface& other) const {
    return std::tie(interface_name, bus_type) <
           std::tie(other.interface_name, other.bus_type);
  }
};

struct PortalInterfaceHash {
  size_t operator()(const PortalInterface& portal) const {
    return std::hash<std::string>{}(portal.interface_name) ^
           (std::hash<int>{}(static_cast<int>(portal.bus_type)) << 1);
  }
};

#endif  // PORTAL_INTERFACE_H
