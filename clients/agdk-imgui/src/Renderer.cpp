#include "Renderer.hpp"
#include <android/log.h>
#include <array>

#define LOG_TAG "Renderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

Renderer::Renderer() {
}

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize(ANativeWindow* window, int width, int height) {
    window_ = window;
    width_ = width;
    height_ = height;
    
    // Initialize EGL
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
        return false;
    }
    
    if (!eglInitialize(display_, nullptr, nullptr)) {
        LOGE("Failed to initialize EGL");
        return false;
    }
    
    // Configure EGL
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(display_, configAttribs, &config, 1, &numConfigs)) {
        LOGE("Failed to choose EGL config");
        return false;
    }
    
    // Create surface
    surface_ = eglCreateWindowSurface(display_, config, window_, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        LOGE("Failed to create EGL surface");
        return false;
    }
    
    // Create context
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("Failed to create EGL context");
        return false;
    }
    
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        LOGE("Failed to make EGL context current");
        return false;
    }
    
    // Setup OpenGL
    SetupGL();
    SetupShaders();
    SetupTextures();
    CreateFramebuffer();
    
    LOGI("Renderer initialized: %dx%d", width_, height_);
    return true;
}

void Renderer::Shutdown() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
        }
        
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
        }
        
        eglTerminate(display_);
    }
    
    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
    surface_ = EGL_NO_SURFACE;
    window_ = nullptr;
}

void Renderer::BeginFrame() {
    eglMakeCurrent(display_, surface_, surface_, context_);
    
    // Set viewport
    glViewport(0, 0, width_, height_);
    
    // Clear screen
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Cull back faces
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}

void Renderer::EndFrame() {
    // Swap buffers
    eglSwapBuffers(display_, surface_);
    
    // Check for errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOGE("OpenGL error: %d", error);
    }
}

void Renderer::RenderWorld(const GameState& gameState) {
    if (!gameState.worldData || !currentShader_) return;
    
    // Calculate matrices
    projectionMatrix_ = glm::perspective(
        glm::radians(cameraFOV_),
        (float)width_ / (float)height_,
        cameraNear_,
        cameraFar_
    );
    
    viewMatrix_ = glm::lookAt(
        cameraPosition_,
        cameraTarget_,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    
    // Use world shader
    auto it = shaders_.find("world");
    if (it != shaders_.end()) {
        it->second->Use();
        
        // Set uniforms
        it->second->SetUniform("uProjection", projectionMatrix_);
        it->second->SetUniform("uView", viewMatrix_);
        it->second->SetUniform("uLightDir", lightDirection_);
        it->second->SetUniform("uLightColor", lightColor_);
        it->second->SetUniform("uViewPos", cameraPosition_);
        it->second->SetUniform("uAmbientStrength", ambientStrength_);
        
        // Render visible chunks
        auto visibleChunks = gameState.worldData->GetVisibleChunks(
            cameraPosition_, cameraFar_);
        
        for (auto chunk : visibleChunks) {
            modelMatrix_ = glm::translate(glm::mat4(1.0f), chunk->GetWorldPosition());
            it->second->SetUniform("uModel", modelMatrix_);
            
            RenderChunk(*chunk, projectionMatrix_ * viewMatrix_ * modelMatrix_);
        }
    }
}

void Renderer::RenderEntities(const GameState& gameState) {
    if (!gameState.entityManager || !currentShader_) return;
    
    auto it = shaders_.find("entity");
    if (it != shaders_.end()) {
        it->second->Use();
        
        it->second->SetUniform("uProjection", projectionMatrix_);
        it->second->SetUniform("uView", viewMatrix_);
        it->second->SetUniform("uLightDir", lightDirection_);
        it->second->SetUniform("uLightColor", lightColor_);
        it->second->SetUniform("uViewPos", cameraPosition_);
        
        auto entities = gameState.entityManager->GetEntitiesInRadius(
            cameraPosition_, cameraFar_);
        
        for (auto entity : entities) {
            modelMatrix_ = glm::translate(glm::mat4(1.0f), entity->position);
            modelMatrix_ = glm::rotate(modelMatrix_, entity->rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix_ = glm::scale(modelMatrix_, entity->scale);
            
            it->second->SetUniform("uModel", modelMatrix_);
            it->second->SetUniform("uColor", entity->color);
            
            // TODO: Render entity mesh
        }
    }
}

void Renderer::SetViewport(int width, int height) {
    width_ = width;
    height_ = height;
    glViewport(0, 0, width_, height_);
}

void Renderer::SetClearColor(const glm::vec4& color) {
    glClearColor(color.r, color.g, color.b, color.a);
}

void Renderer::SetupGL() {
    // Enable features
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    
    // Create VAO
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    
    // Create buffers
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    
    LOGI("OpenGL version: %s", glGetString(GL_VERSION));
    LOGI("GLSL version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
}

void Renderer::SetupShaders() {
    // World shader
    const char* worldVert = R"(
        #version 300 es
        precision mediump float;
        
        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec3 aNormal;
        layout(location = 2) in vec2 aTexCoord;
        layout(location = 3) in vec3 aColor;
        
        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        
        out vec3 vNormal;
        out vec2 vTexCoord;
        out vec3 vColor;
        out vec3 vFragPos;
        
        void main() {
            vec4 worldPos = uModel * vec4(aPosition, 1.0);
            vFragPos = worldPos.xyz;
            vNormal = mat3(transpose(inverse(uModel))) * aNormal;
            vTexCoord = aTexCoord;
            vColor = aColor;
            gl_Position = uProjection * uView * worldPos;
        }
    )";
    
    const char* worldFrag = R"(
        #version 300 es
        precision mediump float;
        
        in vec3 vNormal;
        in vec2 vTexCoord;
        in vec3 vColor;
        in vec3 vFragPos;
        
        uniform sampler2D uTexture;
        uniform vec3 uLightDir;
        uniform vec3 uLightColor;
        uniform vec3 uViewPos;
        uniform float uAmbientStrength;
        
        out vec4 fragColor;
        
        void main() {
            vec4 texColor = texture(uTexture, vTexCoord);
            vec3 baseColor = texColor.rgb * vColor;
            
            // Ambient
            vec3 ambient = uAmbientStrength * uLightColor;
            
            // Diffuse
            vec3 norm = normalize(vNormal);
            float diff = max(dot(norm, uLightDir), 0.0);
            vec3 diffuse = diff * uLightColor;
            
            // Combine
            vec3 result = (ambient + diffuse) * baseColor;
            fragColor = vec4(result, texColor.a);
        }
    )";
    
    auto worldShader = std::make_unique<ShaderProgram>();
    if (worldShader->Load(worldVert, worldFrag)) {
        shaders_["world"] = std::move(worldShader);
        LOGI("World shader loaded");
    }
    
    // Entity shader
    const char* entityVert = R"(
        #version 300 es
        precision mediump float;
        
        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec3 aNormal;
        layout(location = 2) in vec2 aTexCoord;
        
        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        
        out vec3 vNormal;
        out vec2 vTexCoord;
        out vec3 vFragPos;
        
        void main() {
            vec4 worldPos = uModel * vec4(aPosition, 1.0);
            vFragPos = worldPos.xyz;
            vNormal = mat3(transpose(inverse(uModel))) * aNormal;
            vTexCoord = aTexCoord;
            gl_Position = uProjection * uView * worldPos;
        }
    )";
    
    const char* entityFrag = R"(
        #version 300 es
        precision mediump float;
        
        in vec3 vNormal;
        in vec2 vTexCoord;
        in vec3 vFragPos;
        
        uniform sampler2D uTexture;
        uniform vec4 uColor;
        uniform vec3 uLightDir;
        uniform vec3 uLightColor;
        uniform vec3 uViewPos;
        
        out vec4 fragColor;
        
        void main() {
            vec4 texColor = texture(uTexture, vTexCoord);
            vec4 finalColor = texColor * uColor;
            
            if (finalColor.a < 0.1) discard;
            
            // Simple lighting
            vec3 norm = normalize(vNormal);
            float diff = max(dot(norm, uLightDir), 0.0);
            vec3 diffuse = diff * uLightColor;
            
            vec3 ambient = vec3(0.3);
            vec3 result = (ambient + diffuse) * finalColor.rgb;
            
            fragColor = vec4(result, finalColor.a);
        }
    )";
    
    auto entityShader = std::make_unique<ShaderProgram>();
    if (entityShader->Load(entityVert, entityFrag)) {
        shaders_["entity"] = std::move(entityShader);
        LOGI("Entity shader loaded");
    }
    
    currentShader_ = shaders_["world"].get();
}

void Renderer::RenderChunk(const WorldChunk& chunk, const glm::mat4& viewProj) {
    // Get chunk geometry
    const auto& vertices = chunk.GetVertices();
    const auto& triangles = chunk.GetTriangles();
    
    if (vertices.empty() || triangles.empty()) return;
    
    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 
                 vertices.size() * sizeof(Vertex),
                 vertices.data(),
                 GL_STATIC_DRAW);
    
    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 triangles.size() * sizeof(Triangle),
                 triangles.data(),
                 GL_STATIC_DRAW);
    
    // Setup vertex attributes
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
                         (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(3);
    
    // Draw
    glDrawElements(GL_TRIANGLES, 
                   triangles.size() * 3,
                   GL_UNSIGNED_INT,
                   nullptr);
}