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

#include <sstream>
#include "metrics_cache_observer.h"

MetricsCacheObserver::MetricsCacheObserver(
    flatpak_plugin::CacheMetrics* metrics)
    : metrics_(metrics) {}

void MetricsCacheObserver::OnCacheHit(const std::string& key,
                                      size_t data_size) {
  std::ostringstream oss;
  oss << std::this_thread::get_id();
  ihs::log::debug("Cache hit: key='{}', size={}, thread={}", key, data_size,
                  oss.str());
  ++metrics_->hits;
  metrics_->cache_size_bytes += data_size;
  double hit_ratio = metrics_->GetHitRatio();
  ihs::log::info(
      "Cache hit for key: {}, size: {}. Total hits: {}, Hit ratio: {:.2f}%",
      key, data_size, metrics_->hits.load(), hit_ratio);
}

void MetricsCacheObserver::OnCacheMiss(const std::string& key) {
  std::ostringstream oss;
  oss << std::this_thread::get_id();
  ihs::log::debug("Cache miss: key='{}', thread={}", key, oss.str());
  ++metrics_->misses;
  ihs::log::info("Cache miss for key: {}", key);
}

void MetricsCacheObserver::OnCacheExpired(const std::string& key) {
  ++metrics_->expired_entries;
  ihs::log::info("Cache expired for key: {}", key);
}

void MetricsCacheObserver::OnNetworkFallback(const std::string& reason) {
  ++metrics_->network_calls;
  ihs::log::warn("Network fallback triggered: {}", reason);
}

void MetricsCacheObserver::OnNetworkError(const std::string& url,
                                          long error_code) {
  ++metrics_->network_errors;
  ihs::log::error("Network error for url: {}, error code: {}", url, error_code);
}

void MetricsCacheObserver::OnCacheCleanup(size_t entries_cleaned) {
  ihs::log::info("Cache cleanup event, entries cleaned: {}", entries_cleaned);
}