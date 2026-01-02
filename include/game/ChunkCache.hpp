#pragma once

#include <memory>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <filesystem>

#include "WorldChunk.hpp"
#include "ChunkLOD.hpp"

class ChunkCache {
public:
    enum class CacheLevel {
        MEMORY = 0,
        DISK = 1,
        DATABASE = 2,
        NONE = 3
    };
    
    struct CacheConfig {
        // Memory cache
        size_t max_memory_cache_size = 100; // Number of chunks
        size_t max_memory_size_bytes = 100 * 1024 * 1024; // 100MB
        
        // Disk cache
        std::string disk_cache_path = "./chunk_cache/";
        size_t max_disk_cache_size = 1024; // Number of chunks
        bool enable_disk_cache = true;
        bool compress_disk_cache = true;
        
        // Cache policies
        enum class EvictionPolicy {
            LRU,      // Least Recently Used
            LFU,      // Least Frequently Used
            FIFO      // First In First Out
        };
        EvictionPolicy eviction_policy = EvictionPolicy::LRU;
        
        // Performance
        bool async_save = true;
        size_t save_batch_size = 10;
        int compression_level = 6; // 0-9
    };
    
    struct CacheStats {
        size_t memory_cache_hits = 0;
        size_t memory_cache_misses = 0;
        size_t disk_cache_hits = 0;
        size_t disk_cache_misses = 0;
        size_t database_cache_hits = 0;
        size_t database_cache_misses = 0;
        size_t cache_evictions = 0;
        size_t cache_saves = 0;
        size_t cache_loads = 0;
        size_t memory_usage_bytes = 0;
        size_t disk_usage_bytes = 0;
        float average_load_time_ms = 0.0f;
        float average_save_time_ms = 0.0f;
    };
    
    ChunkCache(const CacheConfig& config = CacheConfig());
    ~ChunkCache();
    
    // Cache operations
    bool Put(int x, int z, ChunkLOD lod, std::shared_ptr<WorldChunk> chunk);
    std::shared_ptr<WorldChunk> Get(int x, int z, ChunkLOD lod);
    bool Remove(int x, int z, ChunkLOD lod);
    bool Contains(int x, int z, ChunkLOD lod) const;
    void Clear();
    
    // Batch operations
    bool PutBatch(const std::vector<std::tuple<int, int, ChunkLOD, 
                  std::shared_ptr<WorldChunk>>>& chunks);
    std::vector<std::shared_ptr<WorldChunk>> GetBatch(
        const std::vector<std::tuple<int, int, ChunkLOD>>& requests);
    
    // Persistence
    bool SaveToDisk();
    bool LoadFromDisk();
    bool Flush(); // Force write all dirty chunks
    
    // Cache management
    void SetCacheLevel(CacheLevel level) { cache_level_ = level; }
    CacheLevel GetCacheLevel() const { return cache_level_; }
    
    // Configuration
    void SetConfig(const CacheConfig& config);
    const CacheConfig& GetConfig() const { return config_; }
    
    // Statistics
    CacheStats GetStats() const;
    void ResetStats();
    
    // Utility
    size_t GetMemoryUsage() const;
    size_t GetDiskUsage() const;
    std::vector<std::string> GetCachedChunkKeys() const;

private:
    struct CacheEntry {
        std::shared_ptr<WorldChunk> chunk;
        size_t size_bytes;
        uint64_t access_count;
        std::chrono::steady_clock::time_point last_access;
        std::chrono::steady_clock::time_point creation_time;
        bool dirty = false; // Needs to be saved to disk
        bool persisted = false; // Already saved to disk
    };
    
    CacheConfig config_;
    CacheLevel cache_level_ = CacheLevel::MEMORY;
    
    // Memory cache (LRU/LFU implementation)
    std::unordered_map<std::string, CacheEntry> memory_cache_;
    std::list<std::string> access_order_; // For LRU
    std::unordered_map<std::string, uint64_t> access_frequency_; // For LFU
    
    // Disk cache metadata
    struct DiskCacheEntry {
        std::string filename;
        size_t size_bytes;
        std::chrono::system_clock::time_point saved_time;
        ChunkLOD lod;
    };
    std::unordered_map<std::string, DiskCacheEntry> disk_cache_index_;
    
    // Synchronization
    mutable std::shared_mutex cache_mutex_;
    mutable std::mutex disk_mutex_;
    
    // Statistics
    CacheStats stats_;
    mutable std::mutex stats_mutex_;
    
    // Background workers
    std::atomic<bool> running_{false};
    std::thread save_thread_;
    std::queue<std::string> save_queue_;
    std::condition_variable save_cv_;
    std::mutex save_mutex_;
    
    // Helper methods
    std::string MakeCacheKey(int x, int z, ChunkLOD lod) const;
    std::string GetDiskFilename(int x, int z, ChunkLOD lod) const;
    
    // Cache policies
    void ApplyEvictionPolicy();
    void LRUEviction();
    void LFUEviction();
    void FIFOEviction();
    
    // Memory management
    size_t EstimateChunkSize(const WorldChunk& chunk) const;
    void UpdateMemoryUsage(size_t delta);
    
    // Disk operations
    bool SaveToDiskInternal(const std::string& key, const CacheEntry& entry);
    std::shared_ptr<WorldChunk> LoadFromDiskInternal(const std::string& key);
    bool RemoveFromDisk(const std::string& key);
    
    // Compression
    std::vector<uint8_t> CompressData(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> DecompressData(const std::vector<uint8_t>& compressed) const;
    
    // Serialization
    std::vector<uint8_t> SerializeChunk(const WorldChunk& chunk) const;
    std::shared_ptr<WorldChunk> DeserializeChunk(const std::vector<uint8_t>& data, 
                                                int x, int z, ChunkLOD lod) const;
    
    // Background save thread
    void SaveThreadFunc();
    
    // Statistics updates
    void RecordHit(CacheLevel level);
    void RecordMiss(CacheLevel level);
    void RecordSave(float time_ms);
    void RecordLoad(float time_ms);
};

// Implementation
ChunkCache::ChunkCache(const CacheConfig& config) 
    : config_(config) {
    
    // Create disk cache directory if enabled
    if (config_.enable_disk_cache) {
        std::filesystem::create_directories(config_.disk_cache_path);
        
        // Load disk cache index
        std::string index_file = config_.disk_cache_path + "/index.dat";
        if (std::filesystem::exists(index_file)) {
            LoadFromDisk();
        }
    }
    
    // Start background save thread
    if (config_.async_save) {
        running_ = true;
        save_thread_ = std::thread(&ChunkCache::SaveThreadFunc, this);
    }
}

ChunkCache::~ChunkCache() {
    running_ = false;
    save_cv_.notify_all();
    if (save_thread_.joinable()) {
        save_thread_.join();
    }
    
    // Save remaining dirty chunks
    Flush();
}

bool ChunkCache::Put(int x, int z, ChunkLOD lod, 
                     std::shared_ptr<WorldChunk> chunk) {
    if (!chunk) return false;
    
    std::string key = MakeCacheKey(x, z, lod);
    size_t estimated_size = EstimateChunkSize(*chunk);
    
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    // Check if we need to evict
    if (memory_cache_.size() >= config_.max_memory_cache_size ||
        stats_.memory_usage_bytes + estimated_size > config_.max_memory_size_bytes) {
        ApplyEvictionPolicy();
    }
    
    // Create cache entry
    CacheEntry entry{
        .chunk = chunk,
        .size_bytes = estimated_size,
        .access_count = 1,
        .last_access = std::chrono::steady_clock::now(),
        .creation_time = std::chrono::steady_clock::now(),
        .dirty = true,
        .persisted = false
    };
    
    // Add to memory cache
    memory_cache_[key] = entry;
    access_order_.push_front(key); // Most recently used
    
    // Update memory usage
    UpdateMemoryUsage(estimated_size);
    
    // Queue for disk save if enabled
    if (config_.enable_disk_cache) {
        std::lock_guard<std::mutex> save_lock(save_mutex_);
        save_queue_.push(key);
        save_cv_.notify_one();
    }
    
    return true;
}

std::shared_ptr<WorldChunk> ChunkCache::Get(int x, int z, ChunkLOD lod) {
    std::string key = MakeCacheKey(x, z, lod);
    
    // Try memory cache first
    {
        std::shared_lock<std::shared_mutex> lock(cache_mutex_);
        auto it = memory_cache_.find(key);
        if (it != memory_cache_.end()) {
            // Update access info
            it->second.access_count++;
            it->second.last_access = std::chrono::steady_clock::now();
            
            // Update LRU order
            access_order_.remove(key);
            access_order_.push_front(key);
            
            RecordHit(CacheLevel::MEMORY);
            return it->second.chunk;
        }
    }
    
    // Memory cache miss
    RecordMiss(CacheLevel::MEMORY);
    
    // Try disk cache if enabled
    if (config_.enable_disk_cache) {
        auto start_time = std::chrono::steady_clock::now();
        auto chunk = LoadFromDiskInternal(key);
        auto end_time = std::chrono::steady_clock::now();
        
        if (chunk) {
            float load_time = std::chrono::duration<float, std::milli>(
                end_time - start_time).count();
            
            RecordHit(CacheLevel::DISK);
            RecordLoad(load_time);
            
            // Add to memory cache
            Put(x, z, lod, chunk);
            
            return chunk;
        }
        RecordMiss(CacheLevel::DISK);
    }
    
    // Cache miss at all levels
    return nullptr;
}

void ChunkCache::SaveThreadFunc() {
    std::vector<std::string> batch_keys;
    batch_keys.reserve(config_.save_batch_size);
    
    while (running_) {
        batch_keys.clear();
        
        {
            std::unique_lock<std::mutex> lock(save_mutex_);
            save_cv_.wait(lock, [this] {
                return !running_ || !save_queue_.empty();
            });
            
            if (!running_) break;
            
            // Collect batch of keys to save
            while (!save_queue_.empty() && 
                   batch_keys.size() < config_.save_batch_size) {
                batch_keys.push_back(save_queue_.front());
                save_queue_.pop();
            }
        }
        
        // Process batch
        for (const auto& key : batch_keys) {
            std::shared_lock<std::shared_mutex> cache_lock(cache_mutex_);
            auto it = memory_cache_.find(key);
            if (it != memory_cache_.end() && it->second.dirty) {
                cache_lock.unlock();
                
                auto start_time = std::chrono::steady_clock::now();
                bool saved = SaveToDiskInternal(key, it->second);
                auto end_time = std::chrono::steady_clock::now();
                
                if (saved) {
                    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
                    it->second.dirty = false;
                    it->second.persisted = true;
                    write_lock.unlock();
                    
                    float save_time = std::chrono::duration<float, std::milli>(
                        end_time - start_time).count();
                    RecordSave(save_time);
                    stats_.cache_saves++;
                }
            }
        }
    }
}