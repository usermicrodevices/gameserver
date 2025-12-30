#include "GameWorld.h"
#include <algorithm>
#include <cmath>

GameWorld::GameWorld()
    : player_(nullptr),
      camera_(std::make_unique<Camera>()),
      chunkSize_(16.0f),
      renderDistance_(500.0f) {
}

GameWorld::~GameWorld() {
}

bool GameWorld::Initialize() {
    // Initialize OpenGL resources
    if (!InitializeShaders()) {
        return false;
    }
    
    if (!InitializeTextures()) {
        return false;
    }
    
    if (!InitializeMeshes()) {
        return false;
    }
    
    // Create skybox
    skybox_ = std::make_unique<Skybox>();
    if (!skybox_->Initialize()) {
        std::cerr << "Failed to initialize skybox" << std::endl;
    }
    
    // Setup initial camera
    camera_->SetPosition(glm::vec3(0.0f, 5.0f, 10.0f));
    camera_->SetRotation(-90.0f, -20.0f);
    
    return true;
}

void GameWorld::Shutdown() {
    // Clear entities
    entities_.clear();
    
    // Clear chunks
    loadedChunks_.clear();
    
    // Clear player
    player_.reset();
    
    // Cleanup OpenGL resources
    skybox_.reset();
    
    // Clear shaders and textures
    shaders_.clear();
    textures_.clear();
    meshes_.clear();
}

void GameWorld::Update(float deltaTime) {
    // Update player
    if (player_) {
        player_->Update(deltaTime);
        
        // Update camera to follow player
        if (cameraFollowsPlayer_) {
            glm::vec3 playerPos = player_->GetPosition();
            glm::vec3 cameraOffset = glm::vec3(0.0f, 5.0f, 10.0f);
            camera_->SetPosition(playerPos + cameraOffset);
            camera_->SetRotation(-90.0f, -20.0f);
        }
    }
    
    // Update entities
    for (auto& entity : entities_) {
        entity->Update(deltaTime);
    }
    
    // Update camera
    camera_->Update(deltaTime);
    
    // Load/unload chunks based on player position
    if (player_) {
        UpdateChunks(player_->GetPosition());
    }
}

void GameWorld::Render() {
    if (!camera_) return;
    
    // Get camera matrices
    glm::mat4 view = camera_->GetViewMatrix();
    glm::mat4 projection = camera_->GetProjectionMatrix();
    
    // Setup shader uniforms
    auto basicShader = shaders_["basic"];
    if (basicShader) {
        basicShader->Use();
        basicShader->SetMat4("view", view);
        basicShader->SetMat4("projection", projection);
        
        // Set lighting
        basicShader->SetVec3("lightPos", glm::vec3(100.0f, 100.0f, 100.0f));
        basicShader->SetVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
        basicShader->SetVec3("viewPos", camera_->GetPosition());
    }
    
    // Render skybox first
    if (skybox_) {
        skybox_->Render(view, projection);
    }
    
    // Render terrain chunks
    for (const auto& chunkPair : loadedChunks_) {
        const auto& chunk = chunkPair.second;
        if (chunk->IsVisible(camera_->GetPosition(), renderDistance_)) {
            chunk->Render();
        }
    }
    
    // Render entities
    for (const auto& entity : entities_) {
        if (entity->IsVisible(camera_->GetPosition(), renderDistance_)) {
            entity->Render();
        }
    }
    
    // Render player
    if (player_ && player_->IsVisible(camera_->GetPosition(), renderDistance_)) {
        player_->Render();
    }
}

bool GameWorld::AddEntity(uint64_t entityId, const nlohmann::json& data) {
    try {
        std::string entityType = data.value("type", "unknown");
        glm::vec3 position = ParseVec3(data, "position");
        
        std::shared_ptr<GameEntity> entity;
        
        if (entityType == "player") {
            entity = std::make_shared<PlayerEntity>();
            entity->SetPosition(position);
            // Store reference to player
            if (data.value("is_local", false)) {
                player_ = std::static_pointer_cast<PlayerEntity>(entity);
            }
        }
        else if (entityType == "npc") {
            entity = std::make_shared<NPCEntity>();
            entity->SetPosition(position);
            
            // Set NPC-specific properties
            auto npc = std::static_pointer_cast<NPCEntity>(entity);
            if (data.contains("npc_type")) {
                std::string npcType = data["npc_type"];
                // Set NPC type
            }
        }
        else if (entityType == "item") {
            entity = std::make_shared<ItemEntity>();
            entity->SetPosition(position);
        }
        else {
            // Generic entity
            entity = std::make_shared<GameEntity>();
            entity->SetPosition(position);
        }
        
        // Set common properties
        entity->SetId(entityId);
        
        if (data.contains("rotation")) {
            glm::vec3 rotation = ParseVec3(data, "rotation");
            entity->SetRotation(rotation);
        }
        
        if (data.contains("scale")) {
            glm::vec3 scale = ParseVec3(data, "scale");
            entity->SetScale(scale);
        }
        
        // Add to entity map
        entities_[entityId] = entity;
        
        // Add to spatial grid for efficient queries
        AddEntityToGrid(entityId, position);
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to add entity: " << e.what() << std::endl;
        return false;
    }
}

bool GameWorld::RemoveEntity(uint64_t entityId) {
    auto it = entities_.find(entityId);
    if (it != entities_.end()) {
        // Remove from spatial grid
        RemoveEntityFromGrid(entityId, it->second->GetPosition());
        
        // Remove from map
        entities_.erase(it);
        return true;
    }
    return false;
}

void GameWorld::UpdateEntity(uint64_t entityId, const nlohmann::json& data) {
    auto it = entities_.find(entityId);
    if (it != entities_.end()) {
        auto& entity = it->second;
        
        // Update position if provided
        if (data.contains("position")) {
            glm::vec3 oldPos = entity->GetPosition();
            glm::vec3 newPos = ParseVec3(data, "position");
            entity->SetPosition(newPos);
            
            // Update spatial grid
            UpdateEntityInGrid(entityId, oldPos, newPos);
        }
        
        // Update rotation if provided
        if (data.contains("rotation")) {
            glm::vec3 rotation = ParseVec3(data, "rotation");
            entity->SetRotation(rotation);
        }
        
        // Update scale if provided
        if (data.contains("scale")) {
            glm::vec3 scale = ParseVec3(data, "scale");
            entity->SetScale(scale);
        }
        
        // Update other properties
        if (data.contains("health")) {
            // Update health
        }
        
        if (data.contains("state")) {
            // Update state
        }
    }
}

std::shared_ptr<GameEntity> GameWorld::GetEntity(uint64_t entityId) const {
    auto it = entities_.find(entityId);
    if (it != entities_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<uint64_t> GameWorld::GetEntitiesInRadius(const glm::vec3& center, float radius) const {
    std::vector<uint64_t> result;
    
    // Use spatial grid for efficient query
    std::string gridKey = GetGridKey(center);
    
    // Check current cell and neighboring cells
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            // Calculate neighboring cell key
            // (implementation depends on grid structure)
        }
    }
    
    // Fallback to brute force if spatial grid not available
    for (const auto& pair : entities_) {
        glm::vec3 entityPos = pair.second->GetPosition();
        float distance = glm::distance(center, entityPos);
        
        if (distance <= radius) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

void GameWorld::LoadChunk(int chunkX, int chunkZ, const nlohmann::json& chunkData) {
    std::string chunkKey = GetChunkKey(chunkX, chunkZ);
    
    // Check if chunk already loaded
    if (loadedChunks_.find(chunkKey) != loadedChunks_.end()) {
        // Update existing chunk
        loadedChunks_[chunkKey]->Update(chunkData);
        return;
    }
    
    // Create new chunk
    auto chunk = std::make_shared<WorldChunk>(chunkX, chunkZ);
    chunk->Deserialize(chunkData);
    
    // Generate geometry
    chunk->GenerateGeometry();
    
    // Add to loaded chunks
    loadedChunks_[chunkKey] = chunk;
    
    // Update spatial partitioning
    UpdateChunkInGrid(chunkX, chunkZ);
}

void GameWorld::UnloadChunk(int chunkX, int chunkZ) {
    std::string chunkKey = GetChunkKey(chunkX, chunkZ);
    
    auto it = loadedChunks_.find(chunkKey);
    if (it != loadedChunks_.end()) {
        // Remove from spatial grid
        RemoveChunkFromGrid(chunkX, chunkZ);
        
        // Unload chunk
        loadedChunks_.erase(it);
    }
}

void GameWorld::UpdateChunks(const glm::vec3& centerPosition) {
    // Calculate visible chunk range based on render distance
    int chunkRadius = static_cast<int>(ceil(renderDistance_ / chunkSize_));
    int centerChunkX = static_cast<int>(floor(centerPosition.x / chunkSize_));
    int centerChunkZ = static_cast<int>(floor(centerPosition.z / chunkSize_));
    
    // Determine which chunks should be loaded
    std::set<std::string> chunksToKeep;
    
    for (int dx = -chunkRadius; dx <= chunkRadius; dx++) {
        for (int dz = -chunkRadius; dz <= chunkRadius; dz++) {
            int chunkX = centerChunkX + dx;
            int chunkZ = centerChunkZ + dz;
            
            std::string chunkKey = GetChunkKey(chunkX, chunkZ);
            chunksToKeep.insert(chunkKey);
            
            // Load chunk if not already loaded
            if (loadedChunks_.find(chunkKey) == loadedChunks_.end()) {
                // Request chunk from server or generate locally
                RequestChunk(chunkX, chunkZ);
            }
        }
    }
    
    // Unload chunks that are too far away
    std::vector<std::string> chunksToUnload;
    for (const auto& pair : loadedChunks_) {
        if (chunksToKeep.find(pair.first) == chunksToKeep.end()) {
            chunksToUnload.push_back(pair.first);
        }
    }
    
    for (const auto& chunkKey : chunksToUnload) {
        // Parse chunk coordinates from key
        // Unload chunk
        UnloadChunk(0, 0); // TODO: Parse coordinates from key
    }
}

bool GameWorld::InitializeShaders() {
    // Basic shader for unlit geometry
    const char* basicVert = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aTexCoord;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        
        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;
        
        void main() {
            FragPos = vec3(model * vec4(aPos, 1.0));
            Normal = mat3(transpose(inverse(model))) * aNormal;
            TexCoord = aTexCoord;
            gl_Position = projection * view * vec4(FragPos, 1.0);
        }
    )";
    
    const char* basicFrag = R"(
        #version 330 core
        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;
        
        uniform vec3 lightPos;
        uniform vec3 lightColor;
        uniform vec3 objectColor;
        uniform vec3 viewPos;
        
        out vec4 FragColor;
        
        void main() {
            // Ambient
            float ambientStrength = 0.1;
            vec3 ambient = ambientStrength * lightColor;
            
            // Diffuse
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor;
            
            // Specular
            float specularStrength = 0.5;
            vec3 viewDir = normalize(viewPos - FragPos);
            vec3 reflectDir = reflect(-lightDir, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
            vec3 specular = specularStrength * spec * lightColor;
            
            vec3 result = (ambient + diffuse + specular) * objectColor;
            FragColor = vec4(result, 1.0);
        }
    )";
    
    auto basicShader = std::make_shared<Shader>();
    if (!basicShader->Compile(basicVert, basicFrag)) {
        std::cerr << "Failed to compile basic shader" << std::endl;
        return false;
    }
    
    shaders_["basic"] = basicShader;
    
    return true;
}

bool GameWorld::InitializeTextures() {
    // Load default textures
    // This would load texture files from disk
    
    // Create a default white texture
    unsigned int defaultTexture;
    glGenTextures(1, &defaultTexture);
    glBindTexture(GL_TEXTURE_2D, defaultTexture);
    
    unsigned char whitePixel[] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    auto texture = std::make_shared<Texture>();
    texture->id = defaultTexture;
    texture->width = 1;
    texture->height = 1;
    
    textures_["default"] = texture;
    
    return true;
}

bool GameWorld::InitializeMeshes() {
    // Create a simple cube mesh for testing
    auto cubeMesh = std::make_shared<Mesh>();
    
    // Cube vertices
    std::vector<Vertex> cubeVertices = {
        // Front face
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        
        // Back face
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        
        // Top face
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        
        // Bottom face
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        
        // Right face
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        
        // Left face
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}}
    };
    
    std::vector<unsigned int> cubeIndices = {
        // Front
        0, 1, 2,
        2, 3, 0,
        
        // Back
        4, 5, 6,
        6, 7, 4,
        
        // Top
        8, 9, 10,
        10, 11, 8,
        
        // Bottom
        12, 13, 14,
        14, 15, 12,
        
        // Right
        16, 17, 18,
        18, 19, 16,
        
        // Left
        20, 21, 22,
        22, 23, 20
    };
    
    if (!cubeMesh->Load(cubeVertices, cubeIndices)) {
        std::cerr << "Failed to create cube mesh" << std::endl;
        return false;
    }
    
    meshes_["cube"] = cubeMesh;
    
    return true;
}

std::string GameWorld::GetChunkKey(int chunkX, int chunkZ) const {
    return std::to_string(chunkX) + "_" + std::to_string(chunkZ);
}

std::string GameWorld::GetGridKey(const glm::vec3& position) const {
    int gridX = static_cast<int>(floor(position.x / gridCellSize_));
    int gridZ = static_cast<int>(floor(position.z / gridCellSize_));
    return std::to_string(gridX) + "_" + std::to_string(gridZ);
}

glm::vec3 GameWorld::ParseVec3(const nlohmann::json& data, const std::string& key) {
    if (data.contains(key) && data[key].is_object()) {
        return glm::vec3(
            data[key].value("x", 0.0f),
            data[key].value("y", 0.0f),
            data[key].value("z", 0.0f)
        );
    }
    return glm::vec3(0.0f);
}

void GameWorld::RequestChunk(int chunkX, int chunkZ) {
    // In a real implementation, this would send a request to the server
    // For now, we'll generate a simple chunk locally
    
    auto chunk = std::make_shared<WorldChunk>(chunkX, chunkZ);
    
    // Generate simple terrain
    for (int x = 0; x < WorldChunk::CHUNK_SIZE; x++) {
        for (int z = 0; z < WorldChunk::CHUNK_SIZE; z++) {
            float height = 0.0f;
            
            // Simple heightmap
            float nx = (chunkX * WorldChunk::CHUNK_SIZE + x) / 100.0f;
            float nz = (chunkZ * WorldChunk::CHUNK_SIZE + z) / 100.0f;
            height = sin(nx) * cos(nz) * 5.0f;
            
            // Set blocks
            for (int y = 0; y < WorldChunk::CHUNK_SIZE; y++) {
                if (y < height) {
                    chunk->SetBlock(x, y, z, BlockType::GRASS);
                } else if (y < 0) {
                    chunk->SetBlock(x, y, z, BlockType::WATER);
                }
            }
        }
    }
    
    chunk->GenerateGeometry();
    loadedChunks_[GetChunkKey(chunkX, chunkZ)] = chunk;
}