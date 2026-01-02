#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <functional>

#include "WorldChunk.hpp"

enum class ChunkLOD {
    HIGH = 0,      // Full detail (0-50 units)
    MEDIUM = 1,    // Reduced detail (50-150 units)
    LOW = 2,       // Minimal detail (150-500 units)
    BILLBOARD = 3, // Impostors (500+ units)
    NONE = 4       // Not visible
};

struct LODConfig {
    // Distance thresholds for LOD transitions
    float high_distance = 50.0f;
    float medium_distance = 150.0f;
    float low_distance = 500.0f;
    
    // LOD-specific generation parameters
    struct LODParams {
        bool generate_collision = true;
        bool generate_physics = true;
        bool generate_full_geometry = true;
        int simplification_factor = 1; // 1 = no simplification
        bool use_impostor = false;
    };
    
    std::unordered_map<ChunkLOD, LODParams> lod_params = {
        {ChunkLOD::HIGH, {true, true, true, 1, false}},
        {ChunkLOD::MEDIUM, {true, false, true, 2, false}},
        {ChunkLOD::LOW, {false, false, false, 4, false}},
        {ChunkLOD::BILLBOARD, {false, false, false, 8, true}},
    };
};

class LODChunk : public WorldChunk {
public:
    LODChunk(int x, int z, ChunkLOD lod);
    
    ChunkLOD GetLOD() const { return lod_; }
    void SetLOD(ChunkLOD lod);
    
    // Override base methods for LOD-specific behavior
    void GenerateGeometry() override;
    void GenerateCollisionMesh() override;
    
    // LOD transitions
    bool CanUpgradeLOD() const;
    bool CanDowngradeLOD() const;
    std::shared_ptr<LODChunk> UpgradeLOD();
    std::shared_ptr<LODChunk> DowngradeLOD();
    
    // Serialization with LOD
    nlohmann::json Serialize() const override;
    void Deserialize(const nlohmann::json& data) override;
    
    // Performance metrics
    size_t GetTriangleCount() const;
    size_t GetVertexCount() const;
    float GetGenerationTime() const { return generation_time_ms_; }

private:
    ChunkLOD lod_;
    float generation_time_ms_ = 0.0f;
    
    // LOD-specific geometry
    std::vector<Vertex> lod_vertices_;
    std::vector<Triangle> lod_triangles_;
    
    // Impostor for distant chunks
    struct BillboardData {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 color;
        glm::vec2 size;
        std::string texture_id;
    };
    std::vector<BillboardData> billboards_;
    
    void GenerateHighLOD();
    void GenerateMediumLOD();
    void GenerateLowLOD();
    void GenerateBillboard();
    
    // Geometry simplification
    void SimplifyMesh(int factor);
    void GenerateImpostors();
    
    friend class LODManager;
};

class LODManager {
public:
    static LODManager& GetInstance();
    
    void Initialize(const LODConfig& config = LODConfig());
    
    // LOD determination
    ChunkLOD CalculateLOD(const glm::vec3& camera_pos, 
                          const glm::vec3& chunk_pos) const;
    
    // Chunk creation with LOD
    std::shared_ptr<LODChunk> CreateChunk(int x, int z, ChunkLOD lod);
    
    // Dynamic LOD updates
    void UpdateChunkLOD(std::shared_ptr<LODChunk> chunk, 
                        const glm::vec3& camera_pos);
    
    // Batch processing
    void UpdateAllChunksLOD(const glm::vec3& camera_pos,
                           const std::vector<std::shared_ptr<LODChunk>>& chunks);
    
    // Configuration
    void SetLODConfig(const LODConfig& config);
    const LODConfig& GetConfig() const { return config_; }
    
    // Statistics
    struct LODStats {
        size_t high_lod_chunks = 0;
        size_t medium_lod_chunks = 0;
        size_t low_lod_chunks = 0;
        size_t billboard_chunks = 0;
        size_t lod_upgrades = 0;
        size_t lod_downgrades = 0;
        float average_triangle_reduction = 0.0f;
    };
    
    LODStats GetStats() const;
    void ResetStats();

private:
    LODManager() = default;
    
    LODConfig config_;
    LODStats stats_;
    mutable std::mutex mutex_;
    
    // Distance calculations with hysteresis
    float CalculateDistanceSquared(const glm::vec3& a, const glm::vec3& b) const;
    
    // LOD transition logic with hysteresis
    bool ShouldUpgradeLOD(ChunkLOD current, ChunkLOD target, 
                          float current_distance, float target_distance) const;
    bool ShouldDowngradeLOD(ChunkLOD current, ChunkLOD target,
                            float current_distance, float target_distance) const;
    
    // Performance monitoring
    struct PerformanceMetrics {
        std::chrono::steady_clock::time_point last_update;
        float update_time_ms = 0.0f;
        size_t chunks_updated = 0;
    };
    
    PerformanceMetrics perf_metrics_;
};