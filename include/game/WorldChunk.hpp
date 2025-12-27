#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <nlohmann/json.hpp>

enum class BiomeType {
    PLAINS = 0,
    FOREST = 1,
    MOUNTAIN = 2,
    DESERT = 3,
    OCEAN = 4,
    RIVER = 5
};

enum class BlockType {
    AIR = 0,
    GRASS = 1,
    DIRT = 2,
    STONE = 3,
    WATER = 4,
    SAND = 5,
    SNOW = 6,
    WOOD = 7,
    LEAVES = 8
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;

    Vertex() = default;
    Vertex(glm::vec3 pos, glm::vec3 norm, glm::vec3 col, glm::vec2 u)
    : position(pos), normal(norm), color(col), uv(u) {}
};

struct Triangle {
    uint32_t v0, v1, v2;

    Triangle() = default;
    Triangle(uint32_t a, uint32_t b, uint32_t c) : v0(a), v1(b), v2(c) {}
};

class WorldChunk {
public:
    static const int CHUNK_SIZE = 16;  // 16x16 blocks
    static const float BLOCK_SIZE;
    static const float CHUNK_WIDTH;

    WorldChunk(int x, int z);

    // Geometry access
    const std::vector<Vertex>& GetVertices() const { return vertices_; }
    const std::vector<Triangle>& GetTriangles() const { return triangles_; }
    const std::vector<glm::vec3>& GetCollisionVertices() const { return collisionVertices_; }
    const std::vector<Triangle>& GetCollisionTriangles() const { return collisionTriangles_; }

    // Metadata
    int GetChunkX() const { return chunkX_; }
    int GetChunkZ() const { return chunkZ_; }
    BiomeType GetBiome() const { return biome_; }
    void SetBiome(BiomeType biome) { biome_ = biome; }

    glm::vec3 GetWorldPosition() const {
        return glm::vec3(chunkX_ * CHUNK_WIDTH, 0.0f, chunkZ_ * CHUNK_WIDTH);
    }

    // Block access
    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);
    float GetHeightAt(float x, float z) const;

    // Entity management
    void AddEntity(uint64_t entityId) { entities_.insert(entityId); }
    void RemoveEntity(uint64_t entityId) { entities_.erase(entityId); }
    const std::unordered_set<uint64_t>& GetEntities() const { return entities_; }
    bool HasEntities() const { return !entities_.empty(); }

    // Serialization
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);

    // Geometry generation
    void GenerateLowPolyGeometry();
    void GenerateCollisionMesh();

    // Utility
    bool IsPositionInside(const glm::vec3& position) const;
    glm::vec3 GetCenter() const {
        return glm::vec3(
            chunkX_ * CHUNK_WIDTH + CHUNK_WIDTH / 2.0f,
            0.0f,
            chunkZ_ * CHUNK_WIDTH + CHUNK_WIDTH / 2.0f
        );
    }

private:
    int chunkX_;
    int chunkZ_;
    BiomeType biome_;

    // Block data
    std::vector<BlockType> blocks_;
    std::vector<float> heightmap_;

    // Rendering geometry
    std::vector<Vertex> vertices_;
    std::vector<Triangle> triangles_;

    // Collision geometry (simplified)
    std::vector<glm::vec3> collisionVertices_;
    std::vector<Triangle> collisionTriangles_;

    // Entities in this chunk
    std::unordered_set<uint64_t> entities_;

    // Helper methods
    void GenerateBlockVertices(int x, int y, int z, BlockType type);
    void AddQuad(const glm::vec3& p1, const glm::vec3& p2,
                 const glm::vec3& p3, const glm::vec3& p4,
                 const glm::vec3& normal, const glm::vec3& color);
    void AddTriangle(uint32_t v0, uint32_t v1, uint32_t v2);

    glm::vec3 GetBlockColor(BlockType type) const;
    glm::vec3 GetBiomeColor(BiomeType biome, float height) const;

    friend class WorldGenerator;
};
