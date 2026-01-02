#pragma once

#include <memory>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <filesystem>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <list>
#include <string>
#include <sstream>
#include <iomanip>

#include "../../include/game/WorldChunk.hpp"
#include "../../include/game/ChunkLOD.hpp"

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
