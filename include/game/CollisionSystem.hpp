#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <unordered_set>
#include "WorldChunk.hpp"

struct RaycastHit {
    bool hit = false;
    glm::vec3 point;
    glm::vec3 normal;
    float distance = 0.0f;
    uint64_t entityId = 0;
    uint64_t chunkId = 0;
};

struct CollisionResult {
    bool collided = false;
    glm::vec3 resolution;
    float penetration = 0.0f;
    uint64_t collidedWith = 0; // entityId or 0 for world
    CollisionType type = CollisionType::NONE;
};

enum class CollisionType {
    NONE,
    WORLD,
    ENTITY,
    TRIGGER
};

struct BoundingSphere {
    glm::vec3 center;
    float radius;

    bool Intersects(const BoundingSphere& other) const;
    bool IntersectsRay(const glm::vec3& origin, const glm::vec3& direction, float& distance) const;
};

struct BoundingBox {
    glm::vec3 min;
    glm::vec3 max;

    bool Intersects(const BoundingBox& other) const;
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
    glm::vec3 GetCenter() const;
    float GetRadius() const;
};

class CollisionSystem {
public:
    CollisionSystem();

    // Entity collision
    void RegisterEntity(uint64_t entityId, const BoundingSphere& bounds, CollisionType type = CollisionType::ENTITY);
    void UpdateEntity(uint64_t entityId, const glm::vec3& position);
    void UnregisterEntity(uint64_t entityId);

    // World collision
    void RegisterChunk(const WorldChunk& chunk);
    void UnregisterChunk(int chunkX, int chunkZ);

    // Collision checks
    CollisionResult CheckCollision(const glm::vec3& position, float radius, uint64_t excludeId = 0);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit);
    std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position, float radius);

    // Broad phase and narrow phase
    void UpdateBroadPhase();
    std::vector<std::pair<uint64_t, uint64_t>> GetPotentialCollisions();

private:
    struct CollisionEntity {
        uint64_t id;
        BoundingSphere bounds;
        CollisionType type;
        bool isStatic;
    };

    struct CollisionChunk {
        int chunkX;
        int chunkZ;
        BoundingBox bounds;
        std::vector<BoundingSphere> obstacles;
    };

    std::unordered_map<uint64_t, CollisionEntity> entities_;
    std::unordered_map<std::string, CollisionChunk> chunks_; // key: "x_z"

    // Spatial partitioning
    struct GridCell {
        std::unordered_set<uint64_t> entities;
    };

    float gridCellSize_ = 10.0f;
    std::unordered_map<std::string, GridCell> spatialGrid_; // key: "x_y_z"

    std::mutex mutex_;

    // Helper methods
    std::string GetGridKey(const glm::vec3& position) const;
    bool TestSphereSphere(const BoundingSphere& a, const BoundingSphere& b, CollisionResult& result) const;
    bool TestSphereBox(const BoundingSphere& sphere, const BoundingBox& box, CollisionResult& result) const;
    bool TestSphereTriangle(const glm::vec3& sphereCenter, float radius,
                           const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                           CollisionResult& result) const;

    void UpdateEntityInGrid(uint64_t entityId, const glm::vec3& oldPos, const glm::vec3& newPos);
};
