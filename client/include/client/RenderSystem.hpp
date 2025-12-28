#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

#include "Shader.h"
#include "Texture.h"
#include "Mesh.h"

class RenderSystem {
public:
    RenderSystem();
    ~RenderSystem();
    
    bool Initialize();
    void Shutdown();
    
    void BeginFrame();
    void EndFrame();
    
    void RenderWorld(const std::vector<std::shared_ptr<class WorldChunk>>& chunks);
    void RenderEntities(const std::vector<std::shared_ptr<class GameEntity>>& entities);
    void RenderUI();
    
    void SetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
    
    // Resource management
    bool LoadShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);
    bool LoadTexture(const std::string& name, const std::string& path);
    bool LoadModel(const std::string& name, const std::string& path);
    
    // Rendering settings
    void SetWireframeMode(bool enabled);
    void SetCulling(bool enabled);
    void SetVsync(bool enabled);
    void SetRenderDistance(float distance);
    
private:
    void SetupOpenGL();
    void RenderChunk(const std::shared_ptr<class WorldChunk>& chunk);
    void RenderEntity(const std::shared_ptr<class GameEntity>& entity);
    
    std::unordered_map<std::string, std::shared_ptr<Shader>> shaders_;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures_;
    std::unordered_map<std::string, std::shared_ptr<Mesh>> models_;
    
    std::shared_ptr<Shader> currentShader_;
    std::shared_ptr<class Skybox> skybox_;
    
    glm::mat4 viewMatrix_;
    glm::mat4 projectionMatrix_;
    
    struct RenderStats {
        uint32_t drawCalls{0};
        uint32_t trianglesDrawn{0};
        uint32_t chunksRendered{0};
        uint32_t entitiesRendered{0};
    } stats_;
    
    // OpenGL state
    unsigned int vao_{0};
    unsigned int vbo_{0};
    unsigned int ebo_{0};
};