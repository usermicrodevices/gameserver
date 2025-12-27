#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_set>

enum class BiomeType {
    PLAINS,
    FOREST,
    MOUNTAIN,
    DESERT,
    OCEAN,
    RIVER
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
};

struct Triangle {
    uint32_t v0, v1, v2;
};

struct CollisionMesh {
    std::vector<glm::vec3> vertices;
    std::vector<Triangle> triangles;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;

    bool TestRay(const glm::vec3& origin, const glm::vec3& direction, float& distance) const;
    bool TestSphere(const glm::vec3& center, float radius) const;
};

class WorldChunk {
public:
    const int CHUNK_SIZE = 16;
    const float CHUNK_WIDTH = 32.0f;

    WorldChunk(int x, int z);

    // Geometry data
    const std::vector<Vertex>& GetVertices() const { return vertices_; }
    const std::vector<Triangle>& GetTriangles() const { return triangles_; }
    const std::vector<glm::vec3>& GetCollisionVertices() const { return collisionVertices_; }
    const std::vector<Triangle>& GetCollisionTriangles() const { return collisionTriangles_; }

    // Metadata
    int GetChunkX() const { return chunkX_; }
    int GetChunkZ() const { return chunkZ_; }
    BiomeType GetBiome() const { return biome_; }
    glm::vec3 GetWorldPosition() const;

    // Entity management
    void AddEntity(uint64_t entityId);
    void RemoveEntity(uint64_t entityId);
    const std::unordered_set<uint64_t>& GetEntities() const { return entities_; }

    // Serialization for network transmission
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);

    // Collision
    const CollisionMesh& GetCollisionMesh() const { return collisionMesh_; }
    bool IsPositionInside(const glm::vec3& position) const;

private:
    int chunkX_;
    int chunkZ_;
    BiomeType biome_;

    std::vector<Vertex> vertices_;
    std::vector<Triangle> triangles_;
    std::vector<glm::vec3> collisionVertices_;
    std::vector<Triangle> collisionTriangles_;
    CollisionMesh collisionMesh_;

    std::unordered_set<uint64_t> entities_; // Entities currently in this chunk

    friend class WorldGenerator;
};
