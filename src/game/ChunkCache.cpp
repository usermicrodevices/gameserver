#include <zlib.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

#include "../../include/game/ChunkCache.hpp"

ChunkCache::ChunkCache(const CacheConfig& config) 
    : config_(config) {
    
    // Create disk cache directory if enabled
    if (config_.enable_disk_cache) {
        std::filesystem::create_directories(config_.disk_cache_path);
        
        // Load disk cache index
        LoadFromDisk();
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
        lock.unlock();
        ApplyEvictionPolicy();
        lock.lock();
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
    access_frequency_[key] = 1;
    
    // Update memory usage
    UpdateMemoryUsage(estimated_size);
    
    // Queue for disk save if enabled
    if (config_.enable_disk_cache && config_.async_save) {
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
            access_frequency_[key]++;
            
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

bool ChunkCache::Remove(int x, int z, ChunkLOD lod) {
    std::string key = MakeCacheKey(x, z, lod);
    
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    // Remove from memory cache
    auto it = memory_cache_.find(key);
    if (it != memory_cache_.end()) {
        UpdateMemoryUsage(-it->second.size_bytes);
        memory_cache_.erase(it);
        
        // Remove from access tracking
        access_order_.remove(key);
        access_frequency_.erase(key);
        
        // Remove from disk if present
        if (config_.enable_disk_cache) {
            lock.unlock();
            RemoveFromDisk(key);
            lock.lock();
        }
        
        return true;
    }
    
    return false;
}

bool ChunkCache::Contains(int x, int z, ChunkLOD lod) const {
    std::string key = MakeCacheKey(x, z, lod);
    
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    
    // Check memory cache
    if (memory_cache_.find(key) != memory_cache_.end()) {
        return true;
    }
    
    // Check disk cache if enabled
    if (config_.enable_disk_cache) {
        std::lock_guard<std::mutex> disk_lock(disk_mutex_);
        return disk_cache_index_.find(key) != disk_cache_index_.end();
    }
    
    return false;
}

void ChunkCache::Clear() {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    // Clear memory cache
    memory_cache_.clear();
    access_order_.clear();
    access_frequency_.clear();
    UpdateMemoryUsage(-stats_.memory_usage_bytes);
    
    // Clear disk cache if enabled
    if (config_.enable_disk_cache) {
        lock.unlock();
        
        std::lock_guard<std::mutex> disk_lock(disk_mutex_);
        for (const auto& pair : disk_cache_index_) {
            std::filesystem::remove(pair.second.filename);
        }
        disk_cache_index_.clear();
        stats_.disk_usage_bytes = 0;
        
        lock.lock();
    }
}

bool ChunkCache::PutBatch(const std::vector<std::tuple<int, int, ChunkLOD, 
                         std::shared_ptr<WorldChunk>>>& chunks) {
    bool all_success = true;
    
    for (const auto& tuple : chunks) {
        bool success = Put(std::get<0>(tuple), std::get<1>(tuple), 
                          std::get<2>(tuple), std::get<3>(tuple));
        if (!success) all_success = false;
    }
    
    return all_success;
}

std::vector<std::shared_ptr<WorldChunk>> ChunkCache::GetBatch(
    const std::vector<std::tuple<int, int, ChunkLOD>>& requests) {
    
    std::vector<std::shared_ptr<WorldChunk>> results;
    results.reserve(requests.size());
    
    for (const auto& request : requests) {
        auto chunk = Get(std::get<0>(request), std::get<1>(request), 
                        std::get<2>(request));
        results.push_back(chunk);
    }
    
    return results;
}

bool ChunkCache::SaveToDisk() {
    if (!config_.enable_disk_cache) return false;
    
    std::lock_guard<std::mutex> disk_lock(disk_mutex_);
    
    // Save all dirty chunks in memory cache
    {
        std::shared_lock<std::shared_mutex> cache_lock(cache_mutex_);
        
        for (auto& pair : memory_cache_) {
            if (pair.second.dirty) {
                if (!SaveToDiskInternal(pair.first, pair.second)) {
                    return false;
                }
                pair.second.dirty = false;
                pair.second.persisted = true;
            }
        }
    }
    
    // Save index file
    std::string index_file = config_.disk_cache_path + "/index.dat";
    std::ofstream index_stream(index_file, std::ios::binary);
    
    if (!index_stream) return false;
    
    // Write number of entries
    size_t entry_count = disk_cache_index_.size();
    index_stream.write(reinterpret_cast<const char*>(&entry_count), sizeof(entry_count));
    
    // Write each entry
    for (const auto& pair : disk_cache_index_) {
        // Write key
        size_t key_size = pair.first.size();
        index_stream.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
        index_stream.write(pair.first.data(), key_size);
        
        // Write filename
        size_t filename_size = pair.second.filename.size();
        index_stream.write(reinterpret_cast<const char*>(&filename_size), sizeof(filename_size));
        index_stream.write(pair.second.filename.data(), filename_size);
        
        // Write size and time
        index_stream.write(reinterpret_cast<const char*>(&pair.second.size_bytes), 
                          sizeof(pair.second.size_bytes));
        
        auto time_t = std::chrono::system_clock::to_time_t(pair.second.saved_time);
        index_stream.write(reinterpret_cast<const char*>(&time_t), sizeof(time_t));
        
        // Write LOD
        int lod_int = static_cast<int>(pair.second.lod);
        index_stream.write(reinterpret_cast<const char*>(&lod_int), sizeof(lod_int));
    }
    
    return index_stream.good();
}

bool ChunkCache::LoadFromDisk() {
    if (!config_.enable_disk_cache) return false;
    
    std::lock_guard<std::mutex> disk_lock(disk_mutex_);
    
    std::string index_file = config_.disk_cache_path + "/index.dat";
    if (!std::filesystem::exists(index_file)) return true; // No index yet
    
    std::ifstream index_stream(index_file, std::ios::binary);
    if (!index_stream) return false;
    
    // Read number of entries
    size_t entry_count = 0;
    index_stream.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
    
    // Read each entry
    for (size_t i = 0; i < entry_count; ++i) {
        // Read key
        size_t key_size = 0;
        index_stream.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
        
        std::string key(key_size, '\0');
        index_stream.read(&key[0], key_size);
        
        // Read filename
        size_t filename_size = 0;
        index_stream.read(reinterpret_cast<char*>(&filename_size), sizeof(filename_size));
        
        std::string filename(filename_size, '\0');
        index_stream.read(&filename[0], filename_size);
        
        // Read size and time
        size_t size_bytes = 0;
        index_stream.read(reinterpret_cast<char*>(&size_bytes), sizeof(size_bytes));
        
        std::time_t time_t;
        index_stream.read(reinterpret_cast<char*>(&time_t), sizeof(time_t));
        
        // Read LOD
        int lod_int = 0;
        index_stream.read(reinterpret_cast<char*>(&lod_int), sizeof(lod_int));
        
        // Create disk cache entry
        DiskCacheEntry entry;
        entry.filename = filename;
        entry.size_bytes = size_bytes;
        entry.saved_time = std::chrono::system_clock::from_time_t(time_t);
        entry.lod = static_cast<ChunkLOD>(lod_int);
        
        disk_cache_index_[key] = entry;
        stats_.disk_usage_bytes += size_bytes;
    }
    
    return index_stream.good();
}

bool ChunkCache::Flush() {
    if (!config_.enable_disk_cache) return false;
    
    // Process remaining save queue
    {
        std::lock_guard<std::mutex> save_lock(save_mutex_);
        while (!save_queue_.empty()) {
            std::string key = save_queue_.front();
            save_queue_.pop();
            
            std::shared_lock<std::shared_mutex> cache_lock(cache_mutex_);
            auto it = memory_cache_.find(key);
            if (it != memory_cache_.end() && it->second.dirty) {
                cache_lock.unlock();
                
                if (SaveToDiskInternal(key, it->second)) {
                    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
                    it->second.dirty = false;
                    it->second.persisted = true;
                }
            }
        }
    }
    
    // Save index
    return SaveToDisk();
}

size_t ChunkCache::GetMemoryUsage() const {
    return stats_.memory_usage_bytes;
}

size_t ChunkCache::GetDiskUsage() const {
    return stats_.disk_usage_bytes;
}

std::vector<std::string> ChunkCache::GetCachedChunkKeys() const {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    
    std::vector<std::string> keys;
    keys.reserve(memory_cache_.size());
    
    for (const auto& pair : memory_cache_) {
        keys.push_back(pair.first);
    }
    
    return keys;
}

std::string ChunkCache::MakeCacheKey(int x, int z, ChunkLOD lod) const {
    return std::to_string(x) + "_" + std::to_string(z) + "_" + 
           std::to_string(static_cast<int>(lod));
}

std::string ChunkCache::GetDiskFilename(int x, int z, ChunkLOD lod) const {
    std::stringstream ss;
    ss << config_.disk_cache_path << "/chunk_"
       << std::setw(8) << std::setfill('0') << std::hex << x << "_"
       << std::setw(8) << std::setfill('0') << std::hex << z << "_"
       << static_cast<int>(lod) << ".bin";
    return ss.str();
}

void ChunkCache::ApplyEvictionPolicy() {
    switch (config_.eviction_policy) {
        case CacheConfig::EvictionPolicy::LRU:
            LRUEviction();
            break;
        case CacheConfig::EvictionPolicy::LFU:
            LFUEviction();
            break;
        case CacheConfig::EvictionPolicy::FIFO:
            FIFOEviction();
            break;
    }
}

void ChunkCache::LRUEviction() {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    while (!access_order_.empty() && 
           (memory_cache_.size() > config_.max_memory_cache_size ||
            stats_.memory_usage_bytes > config_.max_memory_size_bytes)) {
        
        // Get least recently used key
        std::string key = access_order_.back();
        access_order_.pop_back();
        
        auto it = memory_cache_.find(key);
        if (it != memory_cache_.end()) {
            // Update memory usage
            UpdateMemoryUsage(-it->second.size_bytes);
            
            // Save to disk if dirty
            if (it->second.dirty && config_.enable_disk_cache) {
                lock.unlock();
                SaveToDiskInternal(key, it->second);
                lock.lock();
            }
            
            // Remove from memory
            memory_cache_.erase(it);
            access_frequency_.erase(key);
            
            stats_.cache_evictions++;
        }
    }
}

void ChunkCache::LFUEviction() {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    while (!access_frequency_.empty() && 
           (memory_cache_.size() > config_.max_memory_cache_size ||
            stats_.memory_usage_bytes > config_.max_memory_size_bytes)) {
        
        // Find least frequently used key
        std::string lfu_key;
        uint64_t min_frequency = UINT64_MAX;
        
        for (const auto& pair : access_frequency_) {
            if (pair.second < min_frequency) {
                min_frequency = pair.second;
                lfu_key = pair.first;
            }
        }
        
        if (lfu_key.empty()) break;
        
        auto it = memory_cache_.find(lfu_key);
        if (it != memory_cache_.end()) {
            // Update memory usage
            UpdateMemoryUsage(-it->second.size_bytes);
            
            // Save to disk if dirty
            if (it->second.dirty && config_.enable_disk_cache) {
                lock.unlock();
                SaveToDiskInternal(lfu_key, it->second);
                lock.lock();
            }
            
            // Remove from memory
            memory_cache_.erase(it);
            access_frequency_.erase(lfu_key);
            
            // Remove from LRU list
            access_order_.remove(lfu_key);
            
            stats_.cache_evictions++;
        }
    }
}

void ChunkCache::FIFOEviction() {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    // Sort by creation time
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> creation_times;
    creation_times.reserve(memory_cache_.size());
    
    for (const auto& pair : memory_cache_) {
        creation_times.emplace_back(pair.first, pair.second.creation_time);
    }
    
    std::sort(creation_times.begin(), creation_times.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });
    
    for (const auto& pair : creation_times) {
        if (memory_cache_.size() <= config_.max_memory_cache_size &&
            stats_.memory_usage_bytes <= config_.max_memory_size_bytes) {
            break;
        }
        
        const std::string& key = pair.first;
        auto it = memory_cache_.find(key);
        if (it != memory_cache_.end()) {
            // Update memory usage
            UpdateMemoryUsage(-it->second.size_bytes);
            
            // Save to disk if dirty
            if (it->second.dirty && config_.enable_disk_cache) {
                lock.unlock();
                SaveToDiskInternal(key, it->second);
                lock.lock();
            }
            
            // Remove from memory
            memory_cache_.erase(it);
            access_frequency_.erase(key);
            access_order_.remove(key);
            
            stats_.cache_evictions++;
        }
    }
}

size_t ChunkCache::EstimateChunkSize(const WorldChunk& chunk) const {
    // Rough estimation based on vertices and triangles
    size_t size = 0;
    
    // Vertices: position (3 floats) + normal (3 floats) + color (3 floats) + uv (2 floats)
    size += chunk.GetVertices().size() * (3 + 3 + 3 + 2) * sizeof(float);
    
    // Triangles: 3 uint32_t per triangle
    size += chunk.GetTriangles().size() * 3 * sizeof(uint32_t);
    
    // Block data: CHUNK_SIZE^3 * sizeof(BlockType)
    size += WorldChunk::CHUNK_SIZE * WorldChunk::CHUNK_SIZE * WorldChunk::CHUNK_SIZE * sizeof(int);
    
    return size;
}

void ChunkCache::UpdateMemoryUsage(size_t delta) {
    stats_.memory_usage_bytes += delta;
    if (delta < 0 && stats_.memory_usage_bytes < 0) {
        stats_.memory_usage_bytes = 0;
    }
}

bool ChunkCache::SaveToDiskInternal(const std::string& key, const CacheEntry& entry) {
    if (!config_.enable_disk_cache || !entry.chunk) return false;
    
    std::lock_guard<std::mutex> disk_lock(disk_mutex_);
    
    // Check if we need to evict disk cache
    if (disk_cache_index_.size() >= config_.max_disk_cache_size) {
        // Remove oldest file
        auto oldest = disk_cache_index_.begin();
        for (auto it = disk_cache_index_.begin(); it != disk_cache_index_.end(); ++it) {
            if (it->second.saved_time < oldest->second.saved_time) {
                oldest = it;
            }
        }
        
        if (oldest != disk_cache_index_.end()) {
            std::filesystem::remove(oldest->second.filename);
            disk_cache_index_.erase(oldest);
        }
    }
    
    // Serialize chunk
    auto serialized_data = SerializeChunk(*entry.chunk);
    
    // Compress if enabled
    std::vector<uint8_t> data_to_save;
    if (config_.compress_disk_cache) {
        data_to_save = CompressData(serialized_data);
    } else {
        data_to_save = std::move(serialized_data);
    }
    
    // Parse coordinates from key
    size_t pos1 = key.find('_');
    size_t pos2 = key.find('_', pos1 + 1);
    
    if (pos1 == std::string::npos || pos2 == std::string::npos) {
        return false;
    }
    
    int x = std::stoi(key.substr(0, pos1));
    int z = std::stoi(key.substr(pos1 + 1, pos2 - pos1 - 1));
    ChunkLOD lod = static_cast<ChunkLOD>(std::stoi(key.substr(pos2 + 1)));
    
    // Create filename
    std::string filename = GetDiskFilename(x, z, lod);
    
    // Write to file
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    
    // Write compression flag
    bool compressed = config_.compress_disk_cache;
    file.write(reinterpret_cast<const char*>(&compressed), sizeof(compressed));
    
    // Write data size
    size_t data_size = data_to_save.size();
    file.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    
    // Write data
    file.write(reinterpret_cast<const char*>(data_to_save.data()), data_size);
    
    if (!file.good()) return false;
    
    // Update disk cache index
    DiskCacheEntry disk_entry;
    disk_entry.filename = filename;
    disk_entry.size_bytes = data_size;
    disk_entry.saved_time = std::chrono::system_clock::now();
    disk_entry.lod = lod;
    
    disk_cache_index_[key] = disk_entry;
    stats_.disk_usage_bytes += data_size;
    
    return true;
}

std::shared_ptr<WorldChunk> ChunkCache::LoadFromDiskInternal(const std::string& key) {
    if (!config_.enable_disk_cache) return nullptr;
    
    std::lock_guard<std::mutex> disk_lock(disk_mutex_);
    
    auto it = disk_cache_index_.find(key);
    if (it == disk_cache_index_.end()) return nullptr;
    
    const auto& disk_entry = it->second;
    
    // Read file
    std::ifstream file(disk_entry.filename, std::ios::binary);
    if (!file) {
        disk_cache_index_.erase(it);
        return nullptr;
    }
    
    // Read compression flag
    bool compressed = false;
    file.read(reinterpret_cast<char*>(&compressed), sizeof(compressed));
    
    // Read data size
    size_t data_size = 0;
    file.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
    
    // Read data
    std::vector<uint8_t> file_data(data_size);
    file.read(reinterpret_cast<char*>(file_data.data()), data_size);
    
    if (!file.good()) {
        disk_cache_index_.erase(it);
        return nullptr;
    }
    
    // Decompress if needed
    std::vector<uint8_t> chunk_data;
    if (compressed) {
        chunk_data = DecompressData(file_data);
    } else {
        chunk_data = std::move(file_data);
    }
    
    // Parse coordinates from key
    size_t pos1 = key.find('_');
    size_t pos2 = key.find('_', pos1 + 1);
    
    if (pos1 == std::string::npos || pos2 == std::string::npos) {
        return nullptr;
    }
    
    int x = std::stoi(key.substr(0, pos1));
    int z = std::stoi(key.substr(pos1 + 1, pos2 - pos1 - 1));
    ChunkLOD lod = static_cast<ChunkLOD>(std::stoi(key.substr(pos2 + 1)));
    
    // Deserialize chunk
    auto chunk = DeserializeChunk(chunk_data, x, z, lod);
    if (!chunk) {
        disk_cache_index_.erase(it);
        return nullptr;
    }
    
    return chunk;
}

bool ChunkCache::RemoveFromDisk(const std::string& key) {
    std::lock_guard<std::mutex> disk_lock(disk_mutex_);
    
    auto it = disk_cache_index_.find(key);
    if (it == disk_cache_index_.end()) return false;
    
    // Remove file
    std::filesystem::remove(it->second.filename);
    
    // Update disk usage
    if (stats_.disk_usage_bytes >= it->second.size_bytes) {
        stats_.disk_usage_bytes -= it->second.size_bytes;
    } else {
        stats_.disk_usage_bytes = 0;
    }
    
    disk_cache_index_.erase(it);
    return true;
}

std::vector<uint8_t> ChunkCache::CompressData(const std::vector<uint8_t>& data) const {
    if (data.empty()) return {};
    
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    
    if (deflateInit(&stream, config_.compression_level) != Z_OK) {
        return {};
    }
    
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    
    std::vector<uint8_t> compressed(data.size()); // Start with same size
    
    do {
        if (stream.total_out >= compressed.size()) {
            compressed.resize(compressed.size() * 2);
        }
        
        stream.next_out = compressed.data() + stream.total_out;
        stream.avail_out = static_cast<uInt>(compressed.size() - stream.total_out);
        
        deflate(&stream, Z_FINISH);
    } while (stream.avail_out == 0);
    
    deflateEnd(&stream);
    compressed.resize(stream.total_out);
    
    return compressed;
}

std::vector<uint8_t> ChunkCache::DecompressData(const std::vector<uint8_t>& compressed) const {
    if (compressed.empty()) return {};
    
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    
    if (inflateInit(&stream) != Z_OK) {
        return {};
    }
    
    stream.next_in = const_cast<Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    
    std::vector<uint8_t> decompressed(compressed.size() * 2); // Start with double size
    
    do {
        if (stream.total_out >= decompressed.size()) {
            decompressed.resize(decompressed.size() * 2);
        }
        
        stream.next_out = decompressed.data() + stream.total_out;
        stream.avail_out = static_cast<uInt>(decompressed.size() - stream.total_out);
        
        inflate(&stream, Z_NO_FLUSH);
    } while (stream.avail_out == 0);
    
    inflateEnd(&stream);
    decompressed.resize(stream.total_out);
    
    return decompressed;
}

std::vector<uint8_t> ChunkCache::SerializeChunk(const WorldChunk& chunk) const {
    // Simple serialization to JSON then to binary
    nlohmann::json json_data = chunk.Serialize();
    std::string json_str = json_data.dump();
    
    std::vector<uint8_t> data(json_str.begin(), json_str.end());
    return data;
}

std::shared_ptr<WorldChunk> ChunkCache::DeserializeChunk(const std::vector<uint8_t>& data, 
                                                        int x, int z, ChunkLOD lod) const {
    try {
        std::string json_str(data.begin(), data.end());
        nlohmann::json json_data = nlohmann::json::parse(json_str);
        
        auto chunk = std::make_shared<WorldChunk>(x, z, lod);
        chunk->Deserialize(json_data);
        
        return chunk;
    } catch (const std::exception& e) {
        return nullptr;
    }
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

void ChunkCache::RecordHit(CacheLevel level) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    switch (level) {
        case CacheLevel::MEMORY:
            stats_.memory_cache_hits++;
            break;
        case CacheLevel::DISK:
            stats_.disk_cache_hits++;
            break;
        case CacheLevel::DATABASE:
            stats_.database_cache_hits++;
            break;
        default:
            break;
    }
}

void ChunkCache::RecordMiss(CacheLevel level) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    switch (level) {
        case CacheLevel::MEMORY:
            stats_.memory_cache_misses++;
            break;
        case CacheLevel::DISK:
            stats_.disk_cache_misses++;
            break;
        case CacheLevel::DATABASE:
            stats_.database_cache_misses++;
            break;
        default:
            break;
    }
}

void ChunkCache::RecordSave(float time_ms) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    stats_.average_save_time_ms = 
        (stats_.average_save_time_ms * stats_.cache_saves + time_ms) 
        / (stats_.cache_saves + 1);
}

void ChunkCache::RecordLoad(float time_ms) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    stats_.average_load_time_ms = 
        (stats_.average_load_time_ms * stats_.cache_loads + time_ms) 
        / (stats_.cache_loads + 1);
    stats_.cache_loads++;
}

ChunkCache::CacheStats ChunkCache::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void ChunkCache::ResetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = CacheStats();
}
