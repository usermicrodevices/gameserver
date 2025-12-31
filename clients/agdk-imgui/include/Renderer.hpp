#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>
#include <unordered_map>

#include "GameState.hpp"
#include "ShaderProgram.hpp"
#include "TextureManager.hpp"
#include "Mesh.hpp"

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    bool Initialize(ANativeWindow* window, int width, int height);
    void Shutdown();
    
    void BeginFrame();
    void EndFrame();
    
    void RenderWorld(const GameState& gameState);
    void RenderEntities(const GameState& gameState);
    void RenderUI();
    void RenderDebug(const GameState& gameState);
    
    void SetViewport(int width, int height);
    void SetClearColor(const glm::vec4& color);
    
    // Resource management
    void LoadTexture(const std::string& name, const std::vector<uint8_t>& data);
    void LoadMesh(const std::string& name, const Mesh& mesh);
    void LoadShader(const std::string& name, const std::string& vertexSrc, 
                   const std::string& fragmentSrc);
    
private:
    void SetupGL();
    void CreateFramebuffer();
    void SetupShaders();
    void SetupTextures();
    
    void RenderChunk(const WorldChunk& chunk, const glm::mat4& viewProj);
    void RenderEntity(const EntityState& entity, const glm::mat4& viewProj);
    void RenderSkybox();
    void RenderWater();
    
    // OpenGL state
    ANativeWindow* window_{nullptr};
    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLSurface surface_{EGL_NO_SURFACE};
    EGLContext context_{EGL_NO_CONTEXT};
    
    int width_{0};
    int height_{0};
    
    // Shaders
    std::unordered_map<std::string, std::unique_ptr<ShaderProgram>> shaders_;
    ShaderProgram* currentShader_{nullptr};
    
    // Textures
    std::unique_ptr<TextureManager> textureManager_;
    
    // Meshes
    std::unordered_map<std::string, Mesh> meshes_;
    
    // Matrices
    glm::mat4 projectionMatrix_;
    glm::mat4 viewMatrix_;
    glm::mat4 modelMatrix_;
    
    // Render targets
    GLuint framebuffer_{0};
    GLuint colorTexture_{0};
    GLuint depthTexture_{0};
    
    // Camera
    glm::vec3 cameraPosition_{0.0f, 10.0f, 0.0f};
    glm::vec3 cameraTarget_{0.0f, 0.0f, 1.0f};
    float cameraFOV_{60.0f};
    float cameraNear_{0.1f};
    float cameraFar_{1000.0f};
    
    // Lighting
    glm::vec3 lightDirection_{-0.5f, -1.0f, -0.5f};
    glm::vec3 lightColor_{1.0f, 0.95f, 0.9f};
    float ambientStrength_{0.3f};
    
    // Debug
    bool wireframe_{false};
    bool showNormals_{false};
    bool showCollision_{false};
    
    // Performance
    GLuint vao_{0};
    GLuint vbo_{0};
    GLuint ibo_{0};
};