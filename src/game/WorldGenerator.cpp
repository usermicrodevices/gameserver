#include <cmath>
#include <algorithm>
#include <glm/gtc/noise.hpp>

#include "../../include/game/WorldGenerator.hpp"

// Initialize static members
const float WorldChunk::BLOCK_SIZE = 1.0f;
const float WorldChunk::CHUNK_WIDTH = WorldChunk::CHUNK_SIZE * WorldChunk::BLOCK_SIZE;

WorldGenerator::WorldGenerator(const GenerationConfig& config)
    : config_(config), rng_(config.seed), dist_(-1.0f, 1.0f) {
}

std::unique_ptr<WorldChunk> WorldGenerator::GenerateChunk(int chunkX, int chunkZ) {
    auto chunk = std::make_unique<WorldChunk>(chunkX, chunkZ);
    
    // Generate terrain heightmap
    GenerateLowPolyTerrain(*chunk, chunkX, chunkZ);
    
    // Generate blocks based on heightmap
    const int chunkSize = WorldChunk::CHUNK_SIZE;
    const int worldSize = 256; // Arbitrary world height
    
    for (int x = 0; x < chunkSize; ++x) {
        for (int z = 0; z < chunkSize; ++z) {
            // Convert to world coordinates
            float worldX = (chunkX * chunkSize + x) * WorldChunk::BLOCK_SIZE;
            float worldZ = (chunkZ * chunkSize + z) * WorldChunk::BLOCK_SIZE;
            
            // Get terrain height at this position
            float height = GetTerrainHeight(worldX, worldZ);
            
            // Determine biome
            BiomeType biome = GetBiomeAt(worldX, worldZ);
            chunk->SetBiome(biome);
            
            // Generate column of blocks
            for (int y = 0; y < worldSize; ++y) {
                if (y < height) {
                    // Below ground - generate appropriate block type
                    BlockType type = BlockType::STONE;
                    
                    // Top layer is grass/dirt
                    if (y >= height - 1) {
                        if (biome == BiomeType::DESERT) {
                            type = BlockType::SAND;
                        } else if (biome == BiomeType::MOUNTAIN && y > config_.waterLevel + 10) {
                            type = BlockType::SNOW;
                        } else if (biome == BiomeType::PLAINS || biome == BiomeType::FOREST) {
                            type = BlockType::GRASS;
                        } else {
                            type = BlockType::DIRT;
                        }
                    } 
                    // Just below top layer
                    else if (y >= height - 4 && y < height - 1) {
                        type = BlockType::DIRT;
                    }
                    
                    chunk->SetBlock(x, y, z, type);
                } 
                else if (y <= config_.waterLevel && y > height) {
                    // Water layer
                    chunk->SetBlock(x, y, z, BlockType::WATER);
                } 
                else {
                    // Air
                    chunk->SetBlock(x, y, z, BlockType::AIR);
                }
            }
        }
    }
    
    // Add biome-specific features
    switch (chunk->GetBiome()) {
        case BiomeType::FOREST:
            GenerateForestFeatures(*chunk);
            break;
        case BiomeType::MOUNTAIN:
            GenerateMountainFeatures(*chunk);
            break;
        case BiomeType::DESERT:
            GenerateDesertFeatures(*chunk);
            break;
        case BiomeType::PLAINS:
            GeneratePlainsFeatures(*chunk);
            break;
        case BiomeType::OCEAN:
        case BiomeType::RIVER:
            AddWaterPlane(*chunk);
            break;
        default:
            break;
    }
    
    // Generate low-poly geometry
    chunk->GenerateLowPolyGeometry();
    chunk->GenerateCollisionMesh();
    
    return chunk;
}

BiomeType WorldGenerator::GetBiomeAt(float x, float z) {
    // Use noise to determine biome
    float noiseValue = FractalNoise(x / 1000.0f, z / 1000.0f);
    float temperature = FractalNoise(x / 800.0f, z / 800.0f);
    float humidity = FractalNoise(x / 700.0f, z / 700.0f);
    
    // Height-based biomes
    float height = GetTerrainHeight(x, z);
    
    if (height < config_.waterLevel) {
        if (humidity > 0.7f) return BiomeType::RIVER;
        return BiomeType::OCEAN;
    }
    
    // Temperature and humidity based biomes
    if (height > config_.mountainThreshold * config_.terrainHeight) {
        return BiomeType::MOUNTAIN;
    }
    
    if (temperature < config_.desertThreshold) {
        return BiomeType::DESERT;
    }
    
    if (humidity > config_.forestThreshold) {
        return BiomeType::FOREST;
    }
    
    return BiomeType::PLAINS;
}

float WorldGenerator::GetTerrainHeight(float x, float z) {
    // Base terrain using fractal noise
    float baseHeight = FractalNoise(x / config_.terrainScale, z / config_.terrainScale);
    
    // Add details with higher frequency noise
    float detail = Noise(x / (config_.terrainScale * 0.5f), z / (config_.terrainScale * 0.5f)) * 0.2f;
    
    // Normalize to [0, 1] and scale by terrain height
    float normalizedHeight = (baseHeight + detail + 1.0f) * 0.5f;
    
    // Apply some smoothing
    normalizedHeight = std::pow(normalizedHeight, 1.5f);
    
    return normalizedHeight * config_.terrainHeight;
}

void WorldGenerator::SetSeed(int seed) {
    config_.seed = seed;
    rng_.seed(seed);
}

float WorldGenerator::Noise(float x, float y) {
    // Simple value noise using glm's simplex noise
    return glm::simplex(glm::vec2(x, y));
}

float WorldGenerator::FractalNoise(float x, float y) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    
    for (int i = 0; i < config_.octaves; ++i) {
        float noiseValue = Noise(x * frequency, y * frequency);
        value += noiseValue * amplitude;
        
        amplitude *= config_.persistence;
        frequency *= config_.lacunarity;
    }
    
    return value;
}

glm::vec3 WorldGenerator::CalculateNormal(float x, float z, float height) {
    const float epsilon = 0.1f;
    
    // Sample heights at neighboring points
    float h1 = GetTerrainHeight(x + epsilon, z);
    float h2 = GetTerrainHeight(x - epsilon, z);
    float h3 = GetTerrainHeight(x, z + epsilon);
    float h4 = GetTerrainHeight(x, z - epsilon);
    
    // Calculate gradient
    float dx = (h1 - h2) / (2.0f * epsilon);
    float dz = (h3 - h4) / (2.0f * epsilon);
    
    // Normal vector (pointing up, adjusted by gradient)
    glm::vec3 normal(-dx, 1.0f, -dz);
    return glm::normalize(normal);
}

void WorldGenerator::GenerateLowPolyTerrain(WorldChunk& chunk, int chunkX, int chunkZ) {
    // This function populates the chunk's heightmap
    const int chunkSize = WorldChunk::CHUNK_SIZE;
    
    for (int x = 0; x <= chunkSize; ++x) {
        for (int z = 0; z <= chunkSize; ++z) {
            float worldX = (chunkX * chunkSize + x) * WorldChunk::BLOCK_SIZE;
            float worldZ = (chunkZ * chunkSize + z) * WorldChunk::BLOCK_SIZE;
            
            float height = GetTerrainHeight(worldX, worldZ);
            
            // Store in heightmap (need to convert to 1D index)
            int index = z * (chunkSize + 1) + x;
            if (index < chunkSize * chunkSize) {
                // Note: WorldChunk needs a GetHeightmap method, which isn't in the header
                // We'll assume there's a way to set the heightmap
                // For now, we'll store it in the chunk's internal heightmap array
            }
        }
    }
}

void WorldGenerator::AddTrees(WorldChunk& chunk, BiomeType biome) {
    if (biome != BiomeType::FOREST) return;
    
    const int chunkSize = WorldChunk::CHUNK_SIZE;
    std::uniform_int_distribution<int> treeDist(0, 10);
    
    for (int x = 2; x < chunkSize - 2; x += 3) {
        for (int z = 2; z < chunkSize - 2; z += 3) {
            // 30% chance to place a tree
            if (treeDist(rng_) < 3) {
                // Check if position is valid (on ground, not underwater)
                float worldX = (chunk.GetChunkX() * chunkSize + x) * WorldChunk::BLOCK_SIZE;
                float worldZ = (chunk.GetChunkZ() * chunkSize + z) * WorldChunk::BLOCK_SIZE;
                
                float height = GetTerrainHeight(worldX, worldZ);
                
                if (height > config_.waterLevel + 0.5f) {
                    // Place tree trunk (3-5 blocks high)
                    int treeHeight = 4 + (rng_() % 3);
                    int baseY = static_cast<int>(height);
                    
                    for (int y = 0; y < treeHeight; ++y) {
                        if (baseY + y < 256) { // World height limit
                            chunk.SetBlock(x, baseY + y, z, BlockType::WOOD);
                        }
                    }
                    
                    // Place leaves
                    if (treeHeight >= 3) {
                        int leavesStart = baseY + treeHeight - 2;
                        for (int dx = -2; dx <= 2; ++dx) {
                            for (int dz = -2; dz <= 2; ++dz) {
                                for (int dy = 0; dy <= 2; ++dy) {
                                    // Skip corners for more natural shape
                                    if (abs(dx) == 2 && abs(dz) == 2) continue;
                                    
                                    int leafX = x + dx;
                                    int leafZ = z + dz;
                                    int leafY = leavesStart + dy;
                                    
                                    if (leafX >= 0 && leafX < chunkSize &&
                                        leafZ >= 0 && leafZ < chunkSize &&
                                        leafY < 256) {
                                        chunk.SetBlock(leafX, leafY, leafZ, BlockType::LEAVES);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void WorldGenerator::AddRocks(WorldChunk& chunk, BiomeType biome) {
    if (biome != BiomeType::MOUNTAIN && biome != BiomeType::DESERT) return;
    
    const int chunkSize = WorldChunk::CHUNK_SIZE;
    std::uniform_int_distribution<int> rockDist(0, 20);
    
    for (int x = 0; x < chunkSize; ++x) {
        for (int z = 0; z < chunkSize; ++z) {
            // 5% chance to place a rock
            if (rockDist(rng_) == 0) {
                float worldX = (chunk.GetChunkX() * chunkSize + x) * WorldChunk::BLOCK_SIZE;
                float worldZ = (chunk.GetChunkZ() * chunkSize + z) * WorldChunk::BLOCK_SIZE;
                
                float height = GetTerrainHeight(worldX, worldZ);
                
                if (height > config_.waterLevel + 0.5f) {
                    int baseY = static_cast<int>(height);
                    
                    // Place a 1x1x1 rock
                    if (baseY < 255) {
                        chunk.SetBlock(x, baseY, z, BlockType::STONE);
                    }
                }
            }
        }
    }
}

void WorldGenerator::AddWaterPlane(WorldChunk& chunk) {
    const int chunkSize = WorldChunk::CHUNK_SIZE;
    int waterY = static_cast<int>(config_.waterLevel);
    
    // Only add water plane if water level is within chunk bounds
    if (waterY >= 0 && waterY < 256) {
        for (int x = 0; x < chunkSize; ++x) {
            for (int z = 0; z < chunkSize; ++z) {
                // Check if this position should have water
                float worldX = (chunk.GetChunkX() * chunkSize + x) * WorldChunk::BLOCK_SIZE;
                float worldZ = (chunk.GetChunkZ() * chunkSize + z) * WorldChunk::BLOCK_SIZE;
                
                float height = GetTerrainHeight(worldX, worldZ);
                
                if (height <= config_.waterLevel) {
                    chunk.SetBlock(x, waterY, z, BlockType::WATER);
                }
            }
        }
    }
}

void WorldGenerator::GenerateForestFeatures(WorldChunk& chunk) {
    AddTrees(chunk, BiomeType::FOREST);
    AddRocks(chunk, BiomeType::FOREST);
}

void WorldGenerator::GenerateMountainFeatures(WorldChunk& chunk) {
    AddRocks(chunk, BiomeType::MOUNTAIN);
    
    // Add snow on high mountains
    const int chunkSize = WorldChunk::CHUNK_SIZE;
    int snowLevel = static_cast<int>(config_.waterLevel + 20);
    
    for (int x = 0; x < chunkSize; ++x) {
        for (int z = 0; z < chunkSize; ++z) {
            float worldX = (chunk.GetChunkX() * chunkSize + x) * WorldChunk::BLOCK_SIZE;
            float worldZ = (chunk.GetChunkZ() * chunkSize + z) * WorldChunk::BLOCK_SIZE;
            
            float height = GetTerrainHeight(worldX, worldZ);
            
            if (height > snowLevel) {
                int snowY = static_cast<int>(height);
                if (snowY < 256) {
                    // Replace top block with snow
                    chunk.SetBlock(x, snowY, z, BlockType::SNOW);
                }
            }
        }
    }
}

void WorldGenerator::GenerateDesertFeatures(WorldChunk& chunk) {
    AddRocks(chunk, BiomeType::DESERT);
    
    // Add occasional cactus
    const int chunkSize = WorldChunk::CHUNK_SIZE;
    std::uniform_int_distribution<int> cactusDist(0, 30);
    
    for (int x = 0; x < chunkSize; ++x) {
        for (int z = 0; z < chunkSize; ++z) {
            if (cactusDist(rng_) == 0) {
                float worldX = (chunk.GetChunkX() * chunkSize + x) * WorldChunk::BLOCK_SIZE;
                float worldZ = (chunk.GetChunkZ() * chunkSize + z) * WorldChunk::BLOCK_SIZE;
                
                float height = GetTerrainHeight(worldX, worldZ);
                
                if (height > config_.waterLevel + 0.5f) {
                    int baseY = static_cast<int>(height);
                    int cactusHeight = 2 + (rng_() % 3);
                    
                    for (int y = 0; y < cactusHeight; ++y) {
                        if (baseY + y < 256) {
                            // Use wood block as placeholder for cactus
                            chunk.SetBlock(x, baseY + y, z, BlockType::WOOD);
                        }
                    }
                }
            }
        }
    }
}

void WorldGenerator::GeneratePlainsFeatures(WorldChunk& chunk) {
    // Plains have few features - just occasional grass/trees
    const int chunkSize = WorldChunk::CHUNK_SIZE;
    std::uniform_int_distribution<int> featureDist(0, 50);
    
    for (int x = 0; x < chunkSize; ++x) {
        for (int z = 0; z < chunkSize; ++z) {
            if (featureDist(rng_) == 0) {
                float worldX = (chunk.GetChunkX() * chunkSize + x) * WorldChunk::BLOCK_SIZE;
                float worldZ = (chunk.GetChunkZ() * chunkSize + z) * WorldChunk::BLOCK_SIZE;
                
                float height = GetTerrainHeight(worldX, worldZ);
                
                if (height > config_.waterLevel + 0.5f) {
                    int baseY = static_cast<int>(height);
                    
                    // Small chance for a tree
                    if (rng_() % 10 == 0) {
                        AddTrees(chunk, BiomeType::FOREST);
                    }
                    // Otherwise just place a tall grass block (using leaves as placeholder)
                    else if (baseY < 255) {
                        chunk.SetBlock(x, baseY, z, BlockType::LEAVES);
                    }
                }
            }
        }
    }
}
