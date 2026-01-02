#include "ChunkPool.hpp"
#include "WorldChunk.hpp"
#include <algorithm>
#include <chrono>

constexpr size_t ChunkPool::ESTIMATED_CHUNK_SIZE;

ChunkPool::ChunkPool(size_t initial_size, size_t max_size) 
    : initial_pool_size_(initial_size), max_pool_size_(max_size) {
    
    Preallocate(initial_size);
    
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
    
    // Clean up all chunks
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        chunk_pool_.clear();
        while (!available_chunks_.empty()) available_chunks_.pop();
        active_chunks_.clear();
    }
}

std::shared_ptr<WorldChunk> ChunkPool::AcquireChunk(int x, int z, ChunkLOD lod) {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
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
        lock.unlock();
        CleanupUnused(max_pool_size_ / 2);
        lock.lock();
    }
    
    auto chunk = CreateNewChunk(x, z, lod);
    AddToPool(key, chunk);
    
    stats_.allocations++;
    stats_.active_chunks++;
    
    return chunk;
}

void ChunkPool::ReleaseChunk(int x, int z, std::shared_ptr<WorldChunk> chunk) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    if (!chunk) return;
    
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

void ChunkPool::Preallocate(size_t count) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    for (size_t i = 0; i < count && chunk_pool_.size() < max_pool_size_; ++i) {
        // Create placeholder chunks at invalid coordinates
        std::string key = "prealloc_" + std::to_string(i);
        auto chunk = std::make_shared<WorldChunk>(-9999, -9999);
        AddToPool(key, chunk);
    }
    
    stats_.total_pool_size = chunk_pool_.size();
}

void ChunkPool::CleanupUnused(size_t keep_min) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    if (chunk_pool_.size() <= keep_min) {
        return;
    }
    
    // Calculate how many to remove
    size_t to_remove = chunk_pool_.size() - keep_min;
    size_t removed = 0;
    
    // Remove oldest unused chunks
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> unused_chunks;
    
    for (const auto& pair : chunk_pool_) {
        if (!pair.second.is_active) {
            unused_chunks.emplace_back(pair.first, pair.second.last_used);
        }
    }
    
    // Sort by last used (oldest first)
    std::sort(unused_chunks.begin(), unused_chunks.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });
    
    // Remove the oldest
    for (size_t i = 0; i < std::min(to_remove, unused_chunks.size()); ++i) {
        const auto& key = unused_chunks[i].first;
        
        // Remove from available queue
        std::queue<std::string> new_queue;
        while (!available_chunks_.empty()) {
            if (available_chunks_.front() != key) {
                new_queue.push(available_chunks_.front());
            }
            available_chunks_.pop();
        }
        available_chunks_ = std::move(new_queue);
        
        // Remove from pool
        chunk_pool_.erase(key);
        stats_.total_pool_size--;
        removed++;
    }
    
    UpdateMemoryUsage(-(removed * ESTIMATED_CHUNK_SIZE));
}

ChunkPool::ChunkPoolStats ChunkPool::GetStats() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return stats_;
}

void ChunkPool::ResetStats() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    stats_ = ChunkPoolStats();
    stats_.total_pool_size = chunk_pool_.size();
}

size_t ChunkPool::GetMemoryUsage() const {
    return memory_usage_.load();
}

std::string ChunkPool::MakeChunkKey(int x, int z, ChunkLOD lod) const {
    return std::to_string(x) + "_" + std::to_string(z) + "_" + 
           std::to_string(static_cast<int>(lod));
}

std::shared_ptr<WorldChunk> ChunkPool::CreateNewChunk(int x, int z, ChunkLOD lod) {
    auto chunk = std::make_shared<WorldChunk>(x, z, lod);
    
    // Generate geometry based on LOD
    switch (lod) {
        case ChunkLOD::HIGH:
            chunk->GenerateHighLODGeometry();
            break;
        case ChunkLOD::MEDIUM:
            chunk->GenerateMediumLODGeometry();
            break;
        case ChunkLOD::LOW:
            chunk->GenerateLowLODGeometry();
            break;
        case ChunkLOD::BILLBOARD:
            chunk->GenerateBillboardGeometry();
            break;
        default:
            chunk->GenerateGeometry();
    }
    
    chunk->GenerateCollisionMesh();
    return chunk;
}

void ChunkPool::AddToPool(const std::string& key, std::shared_ptr<WorldChunk> chunk) {
    PooledChunk pooled_chunk;
    pooled_chunk.chunk = chunk;
    pooled_chunk.last_used = std::chrono::steady_clock::now();
    pooled_chunk.is_active = true;
    
    chunk_pool_[key] = pooled_chunk;
    active_chunks_.insert(key);
    UpdateMemoryUsage(ESTIMATED_CHUNK_SIZE);
}

void ChunkPool::CleanupLoop() {
    while (cleanup_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        CleanupUnused(initial_pool_size_);
    }
}

void ChunkPool::UpdateMemoryUsage(size_t delta) {
    memory_usage_ += delta;
}