
#include "PipewireGraph.h"

#include <spdlog/spdlog.h>

#include <iostream>

PipewireGraph& PipewireGraph::instance() {
  static PipewireGraph s_instance;
  return s_instance;
}

PipewireGraph::PipewireGraph() = default;

PipewireGraph::~PipewireGraph() {
  // Ensure shutdown is called in case user forgot
  if (initialized_) {
    shutdown();
  }
}

// Callback function for detecting cameras
void PipewireGraph::on_global(void* data,
                              const uint32_t id,
                              uint32_t /*permissions*/,
                              const char* type,
                              uint32_t version,
                              const struct spa_dict* props) {
  if (!data) {
    std::cerr << "[Error] on_global received null data\n";
    return;
  }

  if (!props)
    return;

  auto* self = static_cast<PipewireGraph*>(data);

  if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    self->handleNodeInfo(id, version, props);
  } else if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
    self->handlePortInfo(id, version, props);
  } else if (strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
    self->handleLinkInfo(id, version, props);
  }
}
void PipewireGraph::on_global_remove(void* data, const uint32_t id) {
  if (!data) {
    spdlog::error("[error] on_global_remove received null data");
    return;
  }
  auto* self = static_cast<PipewireGraph*>(data);
  self->nodes_.erase(id);
  self->node_ports_.erase(id);
  self->links_.erase(id);

  // Rebuild filtered views efficiently
  self->camera_nodes_.erase(
    std::remove_if(self->camera_nodes_.begin(), self->camera_nodes_.end(),
                   [id](const NodeInfo& node) { return node.id == id; }),
    self->camera_nodes_.end());

  self->audio_nodes_.erase(
    std::remove_if(self->audio_nodes_.begin(), self->audio_nodes_.end(),
                   [id](const NodeInfo& node) { return node.id == id; }),
    self->audio_nodes_.end());

  self->all_nodes_.erase(
    std::remove_if(self->all_nodes_.begin(), self->all_nodes_.end(),
                   [id](const NodeInfo& node) { return node.id == id; }),
    self->all_nodes_.end());

}

bool PipewireGraph::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    // Already initialized
    return true;
  }

  // 1) Initialize PipeWire library (safe to call once)
  pw_init(nullptr, nullptr);

  // 2) Create main loop, context, and core
  pw_thread_loop_ = pw_thread_loop_new("camera-loop", nullptr);
  if (!pw_thread_loop_) {
    spdlog::error("[CameraManager] failed to create pw_main_loop.");
    return false;
  }

  // 3) Start the loop in its own thread
  if (int ret = pw_thread_loop_start(pw_thread_loop_); ret != 0) {
    spdlog::error("[CameraManager] failed to start pw_thread_loop (err={})",
                  ret);
    pw_thread_loop_destroy(pw_thread_loop_);
    pw_thread_loop_ = nullptr;
    return false;
  }

  // 4) Lock the loop for context/core creation
  pw_thread_loop_lock(pw_thread_loop_);
  {
    // We get the underlying spa_loop from the thread loop
    if (auto* loop = pw_thread_loop_get_loop(pw_thread_loop_); !loop) {
      spdlog::error("[CameraManager] could not get loop from threadLoop.");
    } else {
      // Create PipeWire context
      pw_context_ = pw_context_new(loop, nullptr, 0);
      if (!pw_context_) {
        spdlog::error("[CameraManager] failed to create pw_context.");
      } else {
        // Connect to PipeWire core
        pw_core_ = pw_context_connect(pw_context_, nullptr, 0);
        if (!pw_core_) {
          spdlog::error("[CameraManager] could not connect to PipeWire core.");
        }
        pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
        static pw_registry_events registry_events = {
            .version = PW_VERSION_REGISTRY_EVENTS,
            .global = on_global,
            .global_remove = on_global_remove,
        };
        pw_registry_add_listener(pw_registry_, &registry_listener_, &registry_events, this);
      }
    }
  }
  pw_thread_loop_unlock(pw_thread_loop_);

  // Check we have context & core
  if (!pw_context_ || !pw_core_) {
    // Something failed
    pw_thread_loop_stop(pw_thread_loop_);
    pw_thread_loop_destroy(pw_thread_loop_);
    pw_thread_loop_ = nullptr;
    pw_deinit();
    return false;
  }

  initialized_ = true;
  return true;
}

void PipewireGraph::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    return;
  }

  // 1) Stop the background thread loop
  pw_thread_loop_stop(pw_thread_loop_);

  // 2) Lock while destroying
  pw_thread_loop_lock(pw_thread_loop_);
  {
    if (pw_core_) {
      pw_core_disconnect(pw_core_);
      pw_core_ = nullptr;
    }
    if (pw_context_) {
      pw_context_destroy(pw_context_);
      pw_context_ = nullptr;
    }
    spa_hook_remove(&registry_listener_);

  }
  pw_thread_loop_unlock(pw_thread_loop_);

  // 3) Destroy the thread loop
  pw_thread_loop_destroy(pw_thread_loop_);
  pw_thread_loop_ = nullptr;

  // 4) De-init PipeWire
  pw_deinit();
  initialized_ = false;
  spdlog::info("[PipewireGraph] Shutdown completed");
}

void PipewireGraph::handleNodeInfo(const uint32_t id, const uint32_t version,
                                     const spa_dict* props) {
  std::lock_guard lock(data_mutex_);

  NodeInfo node{};
  node.id = id;
  node.version = version;
  node.name = getStringProperty(props, PW_KEY_NODE_NAME);
  node.media_class = getStringProperty(props, PW_KEY_MEDIA_CLASS);
  node.factory_name = getStringProperty(props, PW_KEY_FACTORY_NAME);
  node.is_camera = isCamera(props);
  node.is_audio = isAudio(props);

  nodes_[id] = node;
  all_nodes_.push_back(node);

  if (node.is_camera) {
    camera_nodes_.push_back(node);
  }
  if (node.is_audio) {
    audio_nodes_.push_back(node);
  }

  printNodeInfo(node);
}

void PipewireGraph::handlePortInfo(const uint32_t id, uint32_t /* version */,
                                     const spa_dict* props) {
  std::lock_guard lock(data_mutex_);

  PortInfo port{};
  port.id = id;
  port.name = getStringProperty(props, PW_KEY_PORT_NAME);
  port.direction = getStringProperty(props, PW_KEY_PORT_DIRECTION);
  port.format = getStringProperty(props, PW_KEY_FORMAT_DSP);

  if (const char* node_id_str = spa_dict_lookup(props, PW_KEY_NODE_ID)) {
    port.node_id = std::stoul(node_id_str);
    node_ports_[port.node_id].push_back(port);
  }

  printPortInfo(port);
}

void PipewireGraph::handleLinkInfo(const uint32_t id, uint32_t /* version */,
                                     const spa_dict* props) {
  std::lock_guard lock(data_mutex_);

  LinkInfo link{};
  link.id = id;

  const char* output_node = spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_NODE);
  const char* input_node = spa_dict_lookup(props, PW_KEY_LINK_INPUT_NODE);
  const char* output_port = spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_PORT);
  const char* input_port = spa_dict_lookup(props, PW_KEY_LINK_INPUT_PORT);

  if (output_node) link.output_node_id = std::stoul(output_node);
  if (input_node) link.input_node_id = std::stoul(input_node);
  if (output_port) link.output_port_id = std::stoul(output_port);
  if (input_port) link.input_port_id = std::stoul(input_port);

  links_[id] = link;
  active_links_.push_back(link);

  printLinkInfo(link);
}

bool PipewireGraph::isCamera(const spa_dict* props) {
  const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  return media_class && (strstr(media_class, "Video/Source") ||
                        strstr(media_class, "Camera"));
}

bool PipewireGraph::isAudio(const spa_dict* props) {
  const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  return media_class && (strstr(media_class, "Audio/Source") ||
                        strstr(media_class, "Audio/Sink"));
}

std::string PipewireGraph::getStringProperty(const spa_dict* props,
                                              const char* key) {
  const char* value = spa_dict_lookup(props, key);
  return value ? std::string(value) : std::string();
}

const std::vector<NodeInfo>& PipewireGraph::getCameraNodes() const {
  std::lock_guard lock(data_mutex_);
  return camera_nodes_;
}

const std::vector<NodeInfo>& PipewireGraph::getAudioNodes() const {
  std::lock_guard lock(data_mutex_);
  return audio_nodes_;
}

const std::vector<NodeInfo>& PipewireGraph::getAllNodes() const {
  std::lock_guard lock(data_mutex_);
  return all_nodes_;
}

const NodeInfo* PipewireGraph::getNodeById(const uint32_t id) const {
  std::lock_guard lock(data_mutex_);
  const auto it = nodes_.find(id);
  return (it != nodes_.end()) ? &it->second : nullptr;
}

const std::vector<PortInfo>& PipewireGraph::getPortsForNode(const uint32_t node_id) const {
  std::lock_guard lock(data_mutex_);
  const auto it = node_ports_.find(node_id);
  static const std::vector<PortInfo> empty_vector;
  return (it != node_ports_.end()) ? it->second : empty_vector;
}

const std::vector<LinkInfo>& PipewireGraph::getActiveLinks() const {
  std::lock_guard lock(data_mutex_);
  return active_links_;
}

void PipewireGraph::printNodeInfo(const NodeInfo& node) {
  spdlog::info("[PipewireGraph] Node ID: {} | Name: {} | Media Class: {} | Factory: {} | Version: {} | Camera: {} | Audio: {}",
               node.id, node.name, node.media_class, node.factory_name, node.version, node.is_camera, node.is_audio);
}

void PipewireGraph::printPortInfo(const PortInfo& port) {
  spdlog::info("[PipewireGraph] Port ID: {} | Node ID: {} | Name: {} | Direction: {} | Format: {}",
               port.id, port.node_id, port.name, port.direction, port.format);
}

void PipewireGraph::printLinkInfo(const LinkInfo& link) {
  spdlog::info("[PipewireGraph] Link ID: {} | Output Node: {} | Input Node: {} | Output Port: {} | Input Port: {}",
               link.id, link.output_node_id, link.input_node_id, link.output_port_id, link.input_port_id);
}