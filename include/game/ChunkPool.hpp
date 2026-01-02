#pragma once

#include <memory>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "WorldChunk.hpp"

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

// Implementation
ChunkPool::ChunkPool(size_t initial_size, size_t max_size) 
    : initial_pool_size_(initial_size), max_pool_size_(max_size) {
    
    Preallocate(initial_size_);
    
    // Start background cleanup thread
    cleanup_running_ = true;
    cleanup_thread_ = std::thread([this]() { CleanupLoop(); });
}

ChunkPool::~ChunkPool() {
    cleanup_running_ = false;
    pool_cv_.notify_all();
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    chunk_pool_.clear();
    while (!available_chunks_.empty()) available_chunks_.pop();
}

std::shared_ptr<WorldChunk> ChunkPool::AcquireChunk(int x, int z, ChunkLOD lod) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    std::string key = MakeChunkKey(x, z, lod);
    
    // Check if chunk already exists in pool
    auto it = chunk_pool_.find(key);
    if (it != chunk_pool_.end()) {
        // Chunk exists, mark as active
        if (!it->second.is_active) {
            it->second.is_active = true;
            it->second.last_used = std::chrono::steady_clock::now();
            active_chunks_.insert(key);
            
            // Remove from available queue
            std::queue<std::string> new_queue;
            while (!available_chunks_.empty()) {
                if (available_chunks_.front() != key) {
                    new_queue.push(available_chunks_.front());
                }
                available_chunks_.pop();
            }
            available_chunks_ = std::move(new_queue);
            
            stats_.cache_hits++;
            stats_.active_chunks++;
        }
        return it->second.chunk;
    }
    
    // Chunk not in pool, need to create
    stats_.cache_misses++;
    
    // Check if we need to free up space
    if (chunk_pool_.size() >= max_pool_size_) {
        CleanupUnused(max_pool_size_ / 2);
    }
    
    auto chunk = CreateNewChunk(x, z, lod);
    AddToPool(key, chunk);
    
    stats_.allocations++;
    stats_.active_chunks++;
    
    return chunk;
}

void ChunkPool::ReleaseChunk(int x, int z, std::shared_ptr<WorldChunk> chunk) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    std::string key = MakeChunkKey(x, z, chunk->GetLOD());
    
    auto it = chunk_pool_.find(key);
    if (it != chunk_pool_.end()) {
        it->second.is_active = false;
        it->second.last_used = std::chrono::steady_clock::now();
        available_chunks_.push(key);
        active_chunks_.erase(key);
        
        stats_.deallocations++;
        stats_.active_chunks--;
    }
}

void ChunkPool::CleanupLoop() {
    while (cleanup_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        CleanupUnused(initial_pool_size_);
    }
}