#ifndef PLUGINS_CAMERA_PIPEWIRE_PIPEWIREGRAPH_H
#define PLUGINS_CAMERA_PIPEWIRE_PIPEWIREGRAPH_H

#include <pipewire/core.h>
#include <pipewire/pipewire.h>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct NodeInfo {
  uint32_t id;
  std::string name;
  std::string media_class;
  std::string factory_name;
  uint32_t version;
  bool is_camera;
  bool is_audio;
};

struct PortInfo {
  uint32_t id;
  uint32_t node_id;
  std::string name;
  std::string direction;  // "Input" or "Output"
  std::string format;
};

struct LinkInfo {
  uint32_t id;
  uint32_t output_node_id;
  uint32_t input_node_id;
  uint32_t output_port_id;
  uint32_t input_port_id;
};

class PipewireGraph {
 public:
  ~PipewireGraph();
  PipewireGraph();
  static PipewireGraph& instance();
  pw_thread_loop* threadLoop() const { return pw_thread_loop_; }
  pw_core* core() const { return pw_core_; }

  // Initialize and start object discovery
  bool initialize();
  void shutdown();

  // Query methods - optimized for fast access
  const std::vector<NodeInfo>& getCameraNodes() const;
  const std::vector<NodeInfo>& getAudioNodes() const;
  const std::vector<NodeInfo>& getAllNodes() const;

  const NodeInfo* getNodeById(uint32_t id) const;
  const std::vector<PortInfo>& getPortsForNode(uint32_t node_id) const;
  const std::vector<LinkInfo>& getActiveLinks() const;

  // Statistics
  size_t getNodeCount() const { return nodes_.size(); }
  size_t getCameraCount() const { return camera_nodes_.size(); }
  size_t getAudioCount() const { return audio_nodes_.size(); }

 private:
  // Registry event handlers
  static void on_global(void* data,
                        uint32_t id,
                        uint32_t permissions,
                        const char* type,
                        uint32_t version,
                        const struct spa_dict* props);

  static void on_global_remove(void* data, uint32_t id);
  // Object-specific handlers
  void handleNodeInfo(uint32_t id, uint32_t version, const spa_dict* props);
  void handlePortInfo(uint32_t id, uint32_t version, const spa_dict* props);
  void handleLinkInfo(uint32_t id, uint32_t version, const spa_dict* props);

  // Helper methods
  static bool isCamera(const spa_dict* props);
  static bool isAudio(const spa_dict* props);
  static std::string getStringProperty(const spa_dict* props, const char* key);

  // PipeWire objects (not owned)
  pw_thread_loop* pw_thread_loop_ = nullptr;
  pw_context* pw_context_ = nullptr;
  pw_core* pw_core_ = nullptr;
  pw_registry* pw_registry_ = nullptr;

  // Registry listener
  spa_hook registry_listener_{};
  static const pw_registry_events registry_events_;

  // Optimized storage - separate containers for fast filtered access
  mutable std::mutex data_mutex_;
  mutable std::mutex mutex_;

  // Primary storage - indexed by ID for O(1) lookup
  std::unordered_map<uint32_t, NodeInfo> nodes_;
  std::unordered_map<uint32_t, std::vector<PortInfo>> node_ports_;
  std::unordered_map<uint32_t, LinkInfo> links_;

  // Filtered views for fast iteration - stored as vectors of indices
  std::vector<NodeInfo> camera_nodes_;
  std::vector<NodeInfo> audio_nodes_;
  std::vector<NodeInfo> all_nodes_;
  std::vector<LinkInfo> active_links_;

  bool initialized_ = false;

  // Debug/logging methods
  static void printNodeInfo(const NodeInfo& node);
  static void printPortInfo(const PortInfo& port);
  static void printLinkInfo(const LinkInfo& link);
};

#endif  // PLUGINS_CAMERA_PIPEWIRE_PIPEWIREGRAPH_H
