#pragma once

#include <memory>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <thread>
#include <chrono>

#include "../../include/game/WorldChunk.hpp"

class ChunkPool {
public:
    struct ChunkPoolStats {
        size_t total_pool_size = 0;
        size_t active_chunks = 0;
        size_t available_chunks = 0;
        size_t allocations = 0;
        size_t deallocations = 0;
        size_t cache_hits = 0;
        size_t cache_misses = 0;
    };

    ChunkPool(size_t initial_size = 100, size_t max_size = 1000);
    ~ChunkPool();

    // Acquire a chunk from pool (or create new one)
    std::shared_ptr<WorldChunk> AcquireChunk(int x, int z, ChunkLOD lod = ChunkLOD::HIGH);
    
    // Release chunk back to pool
    void ReleaseChunk(int x, int z, std::shared_ptr<WorldChunk> chunk);
    
    // Pre-allocate chunks for performance
    void Preallocate(size_t count);
    
    // Cleanup old/unused chunks
    void CleanupUnused(size_t keep_min = 50);
    
    // Statistics
    ChunkPoolStats GetStats() const;
    void ResetStats();
    
    // Memory management
    size_t GetMemoryUsage() const;
    size_t GetMaxMemoryUsage() const { return max_pool_size_ * ESTIMATED_CHUNK_SIZE; }
    
    // Configuration
    void SetMaxPoolSize(size_t max_size) { max_pool_size_ = max_size; }
    size_t GetMaxPoolSize() const { return max_pool_size_; }

private:
    static constexpr size_t ESTIMATED_CHUNK_SIZE = 1024 * 1024; // 1MB per chunk
    
    struct PooledChunk {
        std::shared_ptr<WorldChunk> chunk;
        std::chrono::steady_clock::time_point last_used;
        bool is_active = false;
    };
    
    mutable std::mutex pool_mutex_;
    std::condition_variable pool_cv_;
    
    // Pool storage
    std::unordered_map<std::string, PooledChunk> chunk_pool_;
    std::queue<std::string> available_chunks_;
    std::unordered_set<std::string> active_chunks_;
    
    // Statistics
    ChunkPoolStats stats_;
    std::atomic<size_t> memory_usage_{0};
    
    // Configuration
    size_t initial_pool_size_;
    size_t max_pool_size_;
    std::atomic<bool> cleanup_running_{false};
    std::thread cleanup_thread_;
    
    // Helper methods
    std::string MakeChunkKey(int x, int z, ChunkLOD lod) const;
    std::shared_ptr<WorldChunk> CreateNewChunk(int x, int z, ChunkLOD lod);
    void AddToPool(const std::string& key, std::shared_ptr<WorldChunk> chunk);
    void CleanupLoop();
    
    // Memory tracking
    void UpdateMemoryUsage(size_t delta);
};
