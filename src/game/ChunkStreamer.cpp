#include <chrono>
#include <future>
#include <algorithm>

#include "../../include/game/ChunkStreamer.hpp"
#include "../../include/game/WorldGenerator.hpp"

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
    
    // Clear all queues and state
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!load_queue_.empty()) load_queue_.pop();
    while (!unload_queue_.empty()) unload_queue_.pop();
    loaded_chunks_.clear();
    pending_promises_.clear();
    loading_in_progress_.clear();
}

void ChunkStreamer::UpdateViewPosition(const glm::vec3& position) {
    {
        std::lock_guard<std::mutex> lock(view_mutex_);
        view_position_ = position;
    }
    
    // Update queues based on new position
    UpdateLoadQueue(position);
    UpdateUnloadQueue(position);
}

std::future<std::shared_ptr<WorldChunk>> ChunkStreamer::RequestChunk(int x, int z, 
                                                                     ChunkLOD lod) {
    std::promise<std::shared_ptr<WorldChunk>> promise;
    auto future = promise.get_future();
    
    std::string key = MakeChunkKey(x, z);
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        // Check if already loaded
        auto loaded_it = loaded_chunks_.find(key);
        if (loaded_it != loaded_chunks_.end()) {
            promise.set_value(loaded_it->second);
            return future;
        }
        
        // Check if already loading
        if (loading_in_progress_.find(key) != loading_in_progress_.end()) {
            // Already loading, add to pending promises
            pending_promises_[key] = std::move(promise);
            return future;
        }
        
        // Create new request
        ChunkRequest request;
        request.x = x;
        request.z = z;
        request.lod = lod;
        request.request_time = std::chrono::steady_clock::now();
        
        {
            std::lock_guard<std::mutex> view_lock(view_mutex_);
            request.priority = CalculatePriority(x, z, view_position_);
        }
        
        // Add to load queue
        load_queue_.push(request);
        pending_promises_[key] = std::move(promise);
        
        stats_.pending_requests = load_queue_.size();
    }
    
    queue_cv_.notify_one();
    return future;
}

bool ChunkStreamer::CancelRequest(int x, int z) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    std::string key = MakeChunkKey(x, z);
    
    // Remove from pending promises
    auto promise_it = pending_promises_.find(key);
    if (promise_it != pending_promises_.end()) {
        // Set exception on promise
        try {
            throw std::runtime_error("Chunk request cancelled");
        } catch (...) {
            promise_it->second.set_exception(std::current_exception());
        }
        pending_promises_.erase(promise_it);
    }
    
    // Remove from load queue
    std::priority_queue<ChunkRequest> new_queue;
    while (!load_queue_.empty()) {
        auto request = load_queue_.top();
        load_queue_.pop();
        
        if (request.x != x || request.z != z) {
            new_queue.push(request);
        }
    }
    load_queue_ = std::move(new_queue);
    
    stats_.pending_requests = load_queue_.size();
    
    return true;
}

void ChunkStreamer::UnloadChunk(int x, int z) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    std::string key = MakeChunkKey(x, z);
    
    // Remove from loaded chunks
    auto loaded_it = loaded_chunks_.find(key);
    if (loaded_it != loaded_chunks_.end()) {
        // Add to unload queue
        unload_queue_.push({x, z});
        loaded_chunks_.erase(loaded_it);
        stats_.chunks_unloaded++;
    }
    
    queue_cv_.notify_one();
}

std::vector<std::shared_ptr<WorldChunk>> ChunkStreamer::GetLoadedChunks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    std::vector<std::shared_ptr<WorldChunk>> result;
    result.reserve(loaded_chunks_.size());
    
    for (const auto& pair : loaded_chunks_) {
        result.push_back(pair.second);
    }
    
    return result;
}

bool ChunkStreamer::IsChunkLoaded(int x, int z) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return loaded_chunks_.find(MakeChunkKey(x, z)) != loaded_chunks_.end();
}

ChunkStreamer::StreamerStats ChunkStreamer::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void ChunkStreamer::ResetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = StreamerStats();
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
        
        // Record load time
        RecordLoadTime(load_time);
        
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

void ChunkStreamer::UnloaderThread(int thread_id) {
    while (running_) {
        std::pair<int, int> coords;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_ || !unload_queue_.empty();
            });
            
            if (!running_) break;
            
            if (unload_queue_.empty()) continue;
            
            coords = unload_queue_.front();
            unload_queue_.pop();
            
            stats_.active_unloads++;
        }
        
        // Process the chunk unload
        ProcessChunkUnload(coords.first, coords.second);
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stats_.active_unloads--;
        }
    }
}

std::shared_ptr<WorldChunk> ChunkStreamer::ProcessChunkLoad(const ChunkRequest& request) {
    // Try cache first
    auto cached_chunk = chunk_cache_->Get(request.x, request.z, request.lod);
    if (cached_chunk) {
        RecordCacheHit(true);
        return cached_chunk;
    }
    
    RecordCacheHit(false);
    
    // Try pool
    auto chunk = chunk_pool_->AcquireChunk(request.x, request.z, request.lod);
    if (!chunk) {
        // Generate new chunk
        auto generator = std::make_unique<WorldGenerator>();
        auto new_chunk = generator->GenerateChunk(request.x, request.z);
        
        if (new_chunk) {
            // Set LOD
            auto lod_chunk = std::dynamic_pointer_cast<LODChunk>(new_chunk);
            if (lod_chunk) {
                lod_chunk->SetLOD(request.lod);
            }
            
            // Cache it
            chunk_cache_->Put(request.x, request.z, request.lod, new_chunk);
            chunk = new_chunk;
        }
    }
    
    return chunk;
}

void ChunkStreamer::ProcessChunkUnload(int x, int z) {
    std::string key = MakeChunkKey(x, z);
    
    std::shared_ptr<WorldChunk> chunk;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        auto it = loaded_chunks_.find(key);
        if (it != loaded_chunks_.end()) {
            chunk = it->second;
            loaded_chunks_.erase(it);
        }
    }
    
    if (chunk) {
        // Release back to pool
        chunk_pool_->ReleaseChunk(x, z, chunk);
    }
}

void ChunkStreamer::UpdateLoadQueue(const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    // Clear existing queue
    while (!load_queue_.empty()) load_queue_.pop();
    
    // Calculate chunk coordinates around position
    int center_x = static_cast<int>(std::floor(position.x / WorldChunk::CHUNK_WIDTH));
    int center_z = static_cast<int>(std::floor(position.z / WorldChunk::CHUNK_WIDTH));
    
    // Queue chunks within load radius
    for (int dx = -config_.load_radius; dx <= config_.load_radius; ++dx) {
        for (int dz = -config_.load_radius; dz <= config_.load_radius; ++dz) {
            int chunk_x = center_x + dx;
            int chunk_z = center_z + dz;
            
            if (ShouldLoadChunk(chunk_x, chunk_z, position)) {
                std::string key = MakeChunkKey(chunk_x, chunk_z);
                
                // Check if already loaded or loading
                if (loaded_chunks_.find(key) == loaded_chunks_.end() &&
                    loading_in_progress_.find(key) == loading_in_progress_.end()) {
                    
                    // Calculate LOD based on distance
                    glm::vec3 chunk_pos = glm::vec3(
                        chunk_x * WorldChunk::CHUNK_WIDTH + WorldChunk::CHUNK_WIDTH / 2.0f,
                        0.0f,
                        chunk_z * WorldChunk::CHUNK_WIDTH + WorldChunk::CHUNK_WIDTH / 2.0f
                    );
                    
                    ChunkLOD lod = lod_manager_->CalculateLOD(position, chunk_pos);
                    
                    ChunkRequest request;
                    request.x = chunk_x;
                    request.z = chunk_z;
                    request.lod = lod;
                    request.priority = CalculatePriority(chunk_x, chunk_z, position);
                    request.request_time = std::chrono::steady_clock::now();
                    
                    load_queue_.push(request);
                }
            }
        }
    }
    
    stats_.pending_requests = load_queue_.size();
}

void ChunkStreamer::UpdateUnloadQueue(const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    // Queue chunks that should be unloaded
    std::vector<std::string> chunks_to_unload;
    
    for (const auto& pair : loaded_chunks_) {
        // Parse chunk coordinates from key
        size_t pos1 = pair.first.find('_');
        size_t pos2 = pair.first.find('_', pos1 + 1);
        
        if (pos1 != std::string::npos && pos2 != std::string::npos) {
            int chunk_x = std::stoi(pair.first.substr(0, pos1));
            int chunk_z = std::stoi(pair.first.substr(pos1 + 1, pos2 - pos1 - 1));
            
            if (ShouldUnloadChunk(chunk_x, chunk_z, position)) {
                unload_queue_.push({chunk_x, chunk_z});
                chunks_to_unload.push_back(pair.first);
            }
        }
    }
    
    // Remove from loaded chunks immediately
    for (const auto& key : chunks_to_unload) {
        loaded_chunks_.erase(key);
    }
}

uint64_t ChunkStreamer::CalculatePriority(int x, int z, const glm::vec3& position) const {
    // Calculate distance-based priority
    float distance_sq = CalculateDistanceSquared(x, z, position);
    
    // Lower distance = higher priority
    uint64_t distance_priority = static_cast<uint64_t>(distance_sq);
    
    // Add LOD factor (higher LOD = higher priority)
    glm::vec3 chunk_pos = glm::vec3(
        x * WorldChunk::CHUNK_WIDTH + WorldChunk::CHUNK_WIDTH / 2.0f,
        0.0f,
        z * WorldChunk::CHUNK_WIDTH + WorldChunk::CHUNK_WIDTH / 2.0f
    );
    
    ChunkLOD lod = lod_manager_->CalculateLOD(position, chunk_pos);
    uint64_t lod_priority = static_cast<uint64_t>(lod) * 1000000;
    
    return distance_priority + lod_priority;
}

float ChunkStreamer::CalculateDistanceSquared(int x, int z, const glm::vec3& position) const {
    glm::vec3 chunk_center = glm::vec3(
        x * WorldChunk::CHUNK_WIDTH + WorldChunk::CHUNK_WIDTH / 2.0f,
        0.0f,
        z * WorldChunk::CHUNK_WIDTH + WorldChunk::CHUNK_WIDTH / 2.0f
    );
    
    glm::vec3 diff = position - chunk_center;
    return glm::dot(diff, diff);
}

std::string ChunkStreamer::MakeChunkKey(int x, int z) const {
    return std::to_string(x) + "_" + std::to_string(z);
}

bool ChunkStreamer::ShouldLoadChunk(int x, int z, const glm::vec3& position) const {
    float distance_sq = CalculateDistanceSquared(x, z, position);
    return distance_sq <= config_.load_distance * config_.load_distance;
}

bool ChunkStreamer::ShouldUnloadChunk(int x, int z, const glm::vec3& position) const {
    float distance_sq = CalculateDistanceSquared(x, z, position);
    return distance_sq > config_.unload_distance * config_.unload_distance;
}

void ChunkStreamer::RecordLoadTime(float time_ms) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.load_queue_time_ms = 
        (stats_.load_queue_time_ms * stats_.chunks_loaded + time_ms) 
        / (stats_.chunks_loaded + 1);
}

void ChunkStreamer::RecordCacheHit(bool hit) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (hit) {
        stats_.cache_hits++;
    } else {
        stats_.cache_misses++;
    }
}
