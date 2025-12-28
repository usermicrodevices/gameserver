#include <algorithm>

#include "../../include/game/WorldChunk.hpp"

const float WorldChunk::BLOCK_SIZE = 1.0f;
const float WorldChunk::CHUNK_WIDTH = CHUNK_SIZE * BLOCK_SIZE;

WorldChunk::WorldChunk(int x, int z)
    : chunkX_(x), chunkZ_(z), biome_(BiomeType::PLAINS) {

    blocks_.resize(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE, BlockType::AIR);
    heightmap_.resize(CHUNK_SIZE * CHUNK_SIZE, 0.0f);
}

BlockType WorldChunk::GetBlock(int x, int y, int z) const {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) {
        return BlockType::AIR;
    }

    int index = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
    return blocks_[index];
}

void WorldChunk::SetBlock(int x, int y, int z, BlockType type) {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) {
        return;
    }

    int index = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
    blocks_[index] = type;
}

float WorldChunk::GetHeightAt(float x, float z) const {
    int localX = static_cast<int>(std::floor(x)) - chunkX_ * CHUNK_SIZE;
    int localZ = static_cast<int>(std::floor(z)) - chunkZ_ * CHUNK_SIZE;

    if (localX < 0 || localX >= CHUNK_SIZE || localZ < 0 || localZ >= CHUNK_SIZE) {
        return 0.0f;
    }

    return heightmap_[localX + localZ * CHUNK_SIZE];
}

void WorldChunk::GenerateLowPolyGeometry() {
    vertices_.clear();
    triangles_.clear();

    // Generate geometry for each block
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            float height = heightmap_[x + z * CHUNK_SIZE];
            int blockHeight = static_cast<int>(std::floor(height));

            for (int y = 0; y <= blockHeight && y < CHUNK_SIZE; ++y) {
                BlockType type = GetBlock(x, y, z);
                if (type != BlockType::AIR) {
                    GenerateBlockVertices(x, y, z, type);
                }
            }
        }
    }

    GenerateCollisionMesh();
}

void WorldChunk::GenerateBlockVertices(int x, int y, int z, BlockType type) {
    float px = static_cast<float>(x);
    float py = static_cast<float>(y);
    float pz = static_cast<float>(z);

    glm::vec3 color = GetBlockColor(type);

    // Only generate visible faces (simple culling)
    // For a proper implementation, check neighboring blocks

    // Top face
    if (y == CHUNK_SIZE - 1 || GetBlock(x, y + 1, z) == BlockType::AIR) {
        glm::vec3 p1(px, py + 1, pz);
        glm::vec3 p2(px + 1, py + 1, pz);
        glm::vec3 p3(px + 1, py + 1, pz + 1);
        glm::vec3 p4(px, py + 1, pz + 1);
        AddQuad(p1, p2, p3, p4, glm::vec3(0, 1, 0), color * 1.2f);
    }

    // Bottom face
    if (y == 0 || GetBlock(x, y - 1, z) == BlockType::AIR) {
        glm::vec3 p1(px, py, pz);
        glm::vec3 p2(px, py, pz + 1);
        glm::vec3 p3(px + 1, py, pz + 1);
        glm::vec3 p4(px + 1, py, pz);
        AddQuad(p1, p2, p3, p4, glm::vec3(0, -1, 0), color * 0.8f);
    }

    // Front face
    if (z == 0 || GetBlock(x, y, z - 1) == BlockType::AIR) {
        glm::vec3 p1(px, py, pz);
        glm::vec3 p2(px + 1, py, pz);
        glm::vec3 p3(px + 1, py + 1, pz);
        glm::vec3 p4(px, py + 1, pz);
        AddQuad(p1, p2, p3, p4, glm::vec3(0, 0, -1), color);
    }

    // Back face
    if (z == CHUNK_SIZE - 1 || GetBlock(x, y, z + 1) == BlockType::AIR) {
        glm::vec3 p1(px, py, pz + 1);
        glm::vec3 p2(px, py + 1, pz + 1);
        glm::vec3 p3(px + 1, py + 1, pz + 1);
        glm::vec3 p4(px + 1, py, pz + 1);
        AddQuad(p1, p2, p3, p4, glm::vec3(0, 0, 1), color);
    }

    // Left face
    if (x == 0 || GetBlock(x - 1, y, z) == BlockType::AIR) {
        glm::vec3 p1(px, py, pz);
        glm::vec3 p2(px, py + 1, pz);
        glm::vec3 p3(px, py + 1, pz + 1);
        glm::vec3 p4(px, py, pz + 1);
        AddQuad(p1, p2, p3, p4, glm::vec3(-1, 0, 0), color * 0.9f);
    }

    // Right face
    if (x == CHUNK_SIZE - 1 || GetBlock(x + 1, y, z) == BlockType::AIR) {
        glm::vec3 p1(px + 1, py, pz);
        glm::vec3 p2(px + 1, py, pz + 1);
        glm::vec3 p3(px + 1, py + 1, pz + 1);
        glm::vec3 p4(px + 1, py + 1, pz);
        AddQuad(p1, p2, p3, p4, glm::vec3(1, 0, 0), color * 0.9f);
    }
}

void WorldChunk::AddQuad(const glm::vec3& p1, const glm::vec3& p2,
                        const glm::vec3& p3, const glm::vec3& p4,
                        const glm::vec3& normal, const glm::vec3& color) {
    // Create two triangles from the quad
    uint32_t baseIndex = static_cast<uint32_t>(vertices_.size());

    // Create vertices
    vertices_.emplace_back(p1, normal, color, glm::vec2(0, 0));
    vertices_.emplace_back(p2, normal, color, glm::vec2(1, 0));
    vertices_.emplace_back(p3, normal, color, glm::vec2(1, 1));
    vertices_.emplace_back(p4, normal, color, glm::vec2(0, 1));

    // Create triangles
    triangles_.emplace_back(baseIndex, baseIndex + 1, baseIndex + 2);
    triangles_.emplace_back(baseIndex, baseIndex + 2, baseIndex + 3);
}

void WorldChunk::AddTriangle(uint32_t v0, uint32_t v1, uint32_t v2) {
    triangles_.emplace_back(v0, v1, v2);
}

glm::vec3 WorldChunk::GetBlockColor(BlockType type) const {
    switch (type) {
        case BlockType::GRASS: return glm::vec3(0.2f, 0.8f, 0.3f);
        case BlockType::DIRT: return glm::vec3(0.6f, 0.4f, 0.2f);
        case BlockType::STONE: return glm::vec3(0.5f, 0.5f, 0.5f);
        case BlockType::WATER: return glm::vec3(0.2f, 0.4f, 0.8f);
        case BlockType::SAND: return glm::vec3(0.9f, 0.8f, 0.5f);
        case BlockType::SNOW: return glm::vec3(0.95f, 0.95f, 0.95f);
        case BlockType::WOOD: return glm::vec3(0.5f, 0.3f, 0.1f);
        case BlockType::LEAVES: return glm::vec3(0.3f, 0.7f, 0.3f);
        default: return glm::vec3(1.0f, 1.0f, 1.0f);
    }
}

glm::vec3 WorldChunk::GetBiomeColor(BiomeType biome, float height) const {
    switch (biome) {
        case BiomeType::FOREST:
            if (height < 0.3f) return glm::vec3(0.2f, 0.6f, 0.2f);
            else return glm::vec3(0.3f, 0.7f, 0.3f);
        case BiomeType::MOUNTAIN:
            if (height < 0.6f) return glm::vec3(0.4f, 0.4f, 0.4f);
            else return glm::vec3(0.8f, 0.8f, 0.8f);
        case BiomeType::DESERT:
            return glm::vec3(0.9f, 0.8f, 0.5f);
        case BiomeType::OCEAN:
            return glm::vec3(0.1f, 0.3f, 0.6f);
        case BiomeType::RIVER:
            return glm::vec3(0.2f, 0.4f, 0.8f);
        case BiomeType::PLAINS:
        default:
            return glm::vec3(0.4f, 0.7f, 0.3f);
    }
}

void WorldChunk::GenerateCollisionMesh() {
    collisionVertices_.clear();
    collisionTriangles_.clear();

    // Simplified collision mesh - just use block positions for now
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            float height = heightmap_[x + z * CHUNK_SIZE];
            if (height > 0) {
                glm::vec3 pos(x, height, z);
                collisionVertices_.push_back(pos);
                // Create simple collision geometry here
            }
        }
    }
}

nlohmann::json WorldChunk::Serialize() const {
    nlohmann::json data;

    data["chunkX"] = chunkX_;
    data["chunkZ"] = chunkZ_;
    data["biome"] = static_cast<int>(biome_);

    // Serialize heightmap
    nlohmann::json heightmapArray = nlohmann::json::array();
    for (float height : heightmap_) {
        heightmapArray.push_back(height);
    }
    data["heightmap"] = heightmapArray;

    // Serialize blocks (simplified)
    nlohmann::json blocksArray = nlohmann::json::array();
    for (BlockType block : blocks_) {
        blocksArray.push_back(static_cast<int>(block));
    }
    data["blocks"] = blocksArray;

    return data;
}

void WorldChunk::Deserialize(const nlohmann::json& data) {
    chunkX_ = data.value("chunkX", 0);
    chunkZ_ = data.value("chunkZ", 0);
    biome_ = static_cast<BiomeType>(data.value("biome", 0));

    // Deserialize heightmap
    if (data.contains("heightmap") && data["heightmap"].is_array()) {
        const auto& heightmapData = data["heightmap"];
        heightmap_.resize(heightmapData.size());
        for (size_t i = 0; i < heightmapData.size(); ++i) {
            heightmap_[i] = heightmapData[i].get<float>();
        }
    }

    // Deserialize blocks
    if (data.contains("blocks") && data["blocks"].is_array()) {
        const auto& blocksData = data["blocks"];
        blocks_.resize(blocksData.size());
        for (size_t i = 0; i < blocksData.size(); ++i) {
            blocks_[i] = static_cast<BlockType>(blocksData[i].get<int>());
        }
    }

    // Regenerate geometry
    GenerateLowPolyGeometry();
}
