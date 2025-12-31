#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <GLES3/gl3.h>

class TextureManager {
public:
    TextureManager();
    ~TextureManager();
    
    GLuint LoadTexture(const std::string& name, const std::vector<uint8_t>& data,
                      int width, int height, int channels = 4);
    GLuint LoadTextureFromFile(const std::string& path);
    GLuint GetTexture(const std::string& name) const;
    
    void BindTexture(const std::string& name, GLuint unit = 0);
    void UnbindTexture(GLuint unit = 0);
    
    void ReleaseTexture(const std::string& name);
    void ReleaseAllTextures();
    
    // Texture parameters
    void SetFiltering(GLenum minFilter, GLenum magFilter);
    void SetWrapping(GLenum sWrap, GLenum tWrap);
    void GenerateMipmaps(const std::string& name);
    
    // Texture atlas
    struct AtlasRegion {
        std::string name;
        glm::vec2 uvMin;
        glm::vec2 uvMax;
        glm::vec2 size;
    };
    
    GLuint CreateTextureAtlas(const std::string& atlasName,
                            const std::vector<std::pair<std::string, std::vector<uint8_t>>>& textures,
                            int textureSize);
    AtlasRegion GetAtlasRegion(const std::string& atlasName, const std::string& regionName) const;
    
private:
    struct TextureInfo {
        GLuint id{0};
        int width{0};
        int height{0};
        int channels{0};
        GLenum format{GL_RGBA};
    };
    
    struct TextureAtlas {
        GLuint textureId{0};
        int size{0};
        std::unordered_map<std::string, AtlasRegion> regions;
    };
    
    std::unordered_map<std::string, TextureInfo> textures_;
    std::unordered_map<std::string, TextureAtlas> atlases_;
    
    GLenum defaultMinFilter_{GL_LINEAR};
    GLenum defaultMagFilter_{GL_LINEAR};
    GLenum defaultSWrap_{GL_REPEAT};
    GLenum defaultTWrap_{GL_REPEAT};
    
    GLuint CreateTexture(int width, int height, int channels, const void* data);
    GLenum GetFormat(int channels) const;
};