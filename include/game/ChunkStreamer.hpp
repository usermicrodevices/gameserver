#pragma once

#include <queue>
#include <thread>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "WorldChunk.hpp"
#include "ChunkPool.hpp"
#include "ChunkLOD.hpp"
#include "ChunkCache.hpp"

class ChunkStreamer {
public:
    struct ChunkRequest {
        int x;
        int z;
        ChunkLOD lod;
        uint64_t priority; // Lower = higher priority
        std::chrono::steady_clock::time_point request_time;
        
        // For priority queue
        bool operator<(const ChunkRequest& other) const {
            return priority > other.priority; // Min-heap
        }
    };
    
    struct StreamerConfig {
        size_t max_concurrent_loads = 4;
        size_t max_concurrent_unloads = 2;
        size_t request_queue_size = 1000;
        float load_distance = 300.0f;
        float unload_distance = 350.0f;
        int load_radius = 5; // In chunks
        float update_interval_ms = 100.0f;
        bool async_loading = true;
    };
    
    ChunkStreamer(std::shared_ptr<ChunkPool> pool,
                  std::shared_ptr<ChunkCache> cache,
                  std::shared_ptr<LODManager> lod_manager);
    ~ChunkStreamer();
    
    // Start/stop streaming
    bool Start();
    void Stop();
    bool IsRunning() const { return running_; }
    
    // Update streaming based on camera/view position
    void UpdateViewPosition(const glm::vec3& position);
    
    // Request chunk loading (async)
    std::future<std::shared_ptr<WorldChunk>> RequestChunk(int x, int z, 
                                                         ChunkLOD lod = ChunkLOD::HIGH);
    
    // Cancel chunk request
    bool CancelRequest(int x, int z);
    
    // Force unload chunk
    void UnloadChunk(int x, int z);
    
    // Get loaded chunks
    std::vector<std::shared_ptr<WorldChunk>> GetLoadedChunks() const;
    bool IsChunkLoaded(int x, int z) const;
    
    // Statistics
    struct StreamerStats {
        size_t chunks_loaded = 0;
        size_t chunks_unloaded = 0;
        size_t pending_requests = 0;
        size_t active_loads = 0;
        size_t active_unloads = 0;
        size_t cache_hits = 0;
        size_t cache_misses = 0;
        float average_load_time_ms = 0.0f;
        float load_queue_time_ms = 0.0f;
    };
    
    StreamerStats GetStats() const;
    void ResetStats();
    
    // Configuration
    void SetConfig(const StreamerConfig& config);
    const StreamerConfig& GetConfig() const { return config_; }

private:
    // Worker threads
    std::vector<std::thread> loader_threads_;
    std::vector<std::thread> unloader_threads_;
    
    // Queues
    std::priority_queue<ChunkRequest> load_queue_;
    std::queue<std::pair<int, int>> unload_queue_;
    
    // Synchronization
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_{false};
    
    // State tracking
    std::unordered_map<std::string, std::shared_ptr<WorldChunk>> loaded_chunks_;
    std::unordered_map<std::string, std::promise<std::shared_ptr<WorldChunk>>> pending_promises_;
    std::unordered_set<std::string> loading_in_progress_;
    
    // Components
    std::shared_ptr<ChunkPool> chunk_pool_;
    std::shared_ptr<ChunkCache> chunk_cache_;
    std::shared_ptr<LODManager> lod_manager_;
    
    // Configuration
    StreamerConfig config_;
    
    // Current view position
    glm::vec3 view_position_{0.0f};
    mutable std::mutex view_mutex_;
    
    // Statistics
    StreamerStats stats_;
    mutable std::mutex stats_mutex_;
    
    // Worker functions
    void LoaderThread(int thread_id);
    void UnloaderThread(int thread_id);
    
    // Chunk processing
    std::shared_ptr<WorldChunk> ProcessChunkLoad(const ChunkRequest& request);
    void ProcessChunkUnload(int x, int z);
    
    // Queue management
    void UpdateLoadQueue(const glm::vec3& position);
    void UpdateUnloadQueue(const glm::vec3& position);
    
    // Priority calculation
    uint64_t CalculatePriority(int x, int z, const glm::vec3& position) const;
    float CalculateDistanceSquared(int x, int z, const glm::vec3& position) const;
    
    // Helper methods
    std::string MakeChunkKey(int x, int z) const;
    bool ShouldLoadChunk(int x, int z, const glm::vec3& position) const;
    bool ShouldUnloadChunk(int x, int z, const glm::vec3& position) const;
    
    // Performance monitoring
    void RecordLoadTime(float time_ms);
    void RecordCacheHit(bool hit);
};

// Implementation
ChunkStreamer::ChunkStreamer(std::shared_ptr<ChunkPool> pool,
                             std::shared_ptr<ChunkCache> cache,
                             std::shared_ptr<LODManager> lod_manager)
    : chunk_pool_(std::move(pool))
    , chunk_cache_(std::move(cache))
    , lod_manager_(std::move(lod_manager)) {
    
    if (!chunk_pool_) {
        chunk_pool_ = std::make_shared<ChunkPool>();
    }
    
    if (!chunk_cache_) {
        chunk_cache_ = std::make_shared<ChunkCache>();
    }
    
    if (!lod_manager_) {
        lod_manager_ = std::make_shared<LODManager>();
    }
}

ChunkStreamer::~ChunkStreamer() {
    Stop();
}

bool ChunkStreamer::Start() {
    if (running_) return true;
    
    running_ = true;
    
    // Start loader threads
    for (size_t i = 0; i < config_.max_concurrent_loads; ++i) {
        loader_threads_.emplace_back(&ChunkStreamer::LoaderThread, this, i);
    }
    
    // Start unloader threads
    for (size_t i = 0; i < config_.max_concurrent_unloads; ++i) {
        unloader_threads_.emplace_back(&ChunkStreamer::UnloaderThread, this, i);
    }
    
    return true;
}

void ChunkStreamer::Stop() {
    running_ = false;
    queue_cv_.notify_all();
    
    for (auto& thread : loader_threads_) {
        if (thread.joinable()) thread.join();
    }
    
    for (auto& thread : unloader_threads_) {
        if (thread.joinable()) thread.join();
    }
    
    loader_threads_.clear();
    unloader_threads_.clear();
}

void ChunkStreamer::UpdateViewPosition(const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(view_mutex_);
    view_position_ = position;
    
    // Update queues based on new position
    UpdateLoadQueue(position);
    UpdateUnloadQueue(position);
}

void ChunkStreamer::LoaderThread(int thread_id) {
    while (running_) {
        ChunkRequest request;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_ || !load_queue_.empty();
            });
            
            if (!running_) break;
            
            if (load_queue_.empty()) continue;
            
            request = load_queue_.top();
            load_queue_.pop();
            
            // Mark as loading in progress
            std::string key = MakeChunkKey(request.x, request.z);
            loading_in_progress_.insert(key);
            
            stats_.pending_requests = load_queue_.size();
            stats_.active_loads++;
        }
        
        // Process the chunk load
        auto start_time = std::chrono::steady_clock::now();
        auto chunk = ProcessChunkLoad(request);
        auto end_time = std::chrono::steady_clock::now();
        
        float load_time = std::chrono::duration<float, std::milli>(
            end_time - start_time).count();
        
        // Update statistics
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.chunks_loaded++;
            stats_.average_load_time_ms = 
                (stats_.average_load_time_ms * (stats_.chunks_loaded - 1) + load_time) 
                / stats_.chunks_loaded;
            stats_.active_loads--;
        }
        
        // Fulfill promise if exists
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            std::string key = MakeChunkKey(request.x, request.z);
            loading_in_progress_.erase(key);
            
            auto promise_it = pending_promises_.find(key);
            if (promise_it != pending_promises_.end()) {
                promise_it->second.set_value(chunk);
                pending_promises_.erase(promise_it);
            }
            
            if (chunk) {
                loaded_chunks_[key] = chunk;
            }
        }
    }
}