
#include "PipewireGraph.h"

#include <spdlog/spdlog.h>

const pw_registry_events PipewireGraph::registry_events_ = {
  .version = PW_VERSION_REGISTRY_EVENTS,
  .global = onGlobal,
  .global_remove = onGlobalRemove,
};

PipewireGraph::PipewireGraph(pw_thread_loop* thread_loop,
                                 pw_context* context,
                                 pw_core* core,
                                 pw_registry* registry)
  : thread_loop_(thread_loop)
    , context_(context)
    , core_(core)
    , registry_(registry), registry_listener_() {
  // Pre-allocate containers for better performance
  nodes_.reserve(64);
  camera_nodes_.reserve(8);
  audio_nodes_.reserve(16);
  all_nodes_.reserve(64);
}

PipewireGraph::~PipewireGraph() {
  shutdown();
}

bool PipewireGraph::initialize() {
  if (initialized_ || !registry_) {
    return false;
  }

  pw_thread_loop_lock(thread_loop_);

  // Add registry listener
  pw_registry_add_listener(registry_, &registry_listener_,
                          &registry_events_, this);

  pw_thread_loop_unlock(thread_loop_);

  initialized_ = true;
  spdlog::info("[PipewireGraph] Initialized successfully");
  return true;
}

void PipewireGraph::shutdown() {
  if (!initialized_) {
    return;
  }

  pw_thread_loop_lock(thread_loop_);
  spa_hook_remove(&registry_listener_);
  pw_thread_loop_unlock(thread_loop_);

  std::lock_guard lock(data_mutex_);
  nodes_.clear();
  node_ports_.clear();
  links_.clear();
  camera_nodes_.clear();
  audio_nodes_.clear();
  all_nodes_.clear();
  active_links_.clear();

  initialized_ = false;
  spdlog::info("[PipewireGraph] Shutdown completed");
}

void PipewireGraph::onGlobal(void* data, const uint32_t id, uint32_t /* permissions */,
                               const char* type, const uint32_t version,
                               const spa_dict* props) {
  auto* manager = static_cast<PipewireGraph*>(data);

  if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    manager->handleNodeInfo(id, version, props);
  } else if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
    manager->handlePortInfo(id, version, props);
  } else if (strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
    manager->handleLinkInfo(id, version, props);
  }
}

void PipewireGraph::onGlobalRemove(void* data, uint32_t id) {
  auto* manager = static_cast<PipewireGraph*>(data);
  std::lock_guard lock(manager->data_mutex_);

  // Remove from all containers
  manager->nodes_.erase(id);
  manager->node_ports_.erase(id);
  manager->links_.erase(id);

  // Rebuild filtered views efficiently
  manager->camera_nodes_.erase(
    std::remove_if(manager->camera_nodes_.begin(), manager->camera_nodes_.end(),
                   [id](const NodeInfo& node) { return node.id == id; }),
    manager->camera_nodes_.end());

  manager->audio_nodes_.erase(
    std::remove_if(manager->audio_nodes_.begin(), manager->audio_nodes_.end(),
                   [id](const NodeInfo& node) { return node.id == id; }),
    manager->audio_nodes_.end());

  manager->all_nodes_.erase(
    std::remove_if(manager->all_nodes_.begin(), manager->all_nodes_.end(),
                   [id](const NodeInfo& node) { return node.id == id; }),
    manager->all_nodes_.end());
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