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
#include <glm/glm.hpp>
#include <string>
#include <chrono>

#include "../../include/game/WorldChunk.hpp"
#include "../../include/game/ChunkPool.hpp"
#include "../../include/game/ChunkLOD.hpp"
#include "../../include/game/ChunkCache.hpp"

class ChunkStreamer {
public:
    struct ChunkRequest {
        int x;
        int z;
        ChunkLOD lod;
        uint64_t priority; // Lower = higher priority
        std::chrono::steady_clock::time_point request_time;
        
        // For priority queue - changed to correct comparison
        bool operator<(const ChunkRequest& other) const {
            return priority > other.priority; // Min-heap (higher priority = lower number)
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
