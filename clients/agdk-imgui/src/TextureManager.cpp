#include "TextureManager.hpp"
#include <android/log.h>
#include <android/asset_manager.h>
#include <vector>

#define LOG_TAG "TextureManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern AAssetManager* g_assetManager;

TextureManager::TextureManager() {
}

TextureManager::~TextureManager() {
    ReleaseAllTextures();
}

GLuint TextureManager::LoadTexture(const std::string& name, const std::vector<uint8_t>& data,
                                  int width, int height, int channels) {
    if (textures_.find(name) != textures_.end()) {
        ReleaseTexture(name);
    }
    
    GLuint textureId = CreateTexture(width, height, channels, data.data());
    if (textureId == 0) {
        LOGE("Failed to create texture: %s", name.c_str());
        return 0;
    }
    
    TextureInfo info;
    info.id = textureId;
    info.width = width;
    info.height = height;
    info.channels = channels;
    info.format = GetFormat(channels);
    
    textures_[name] = info;
    LOGI("Loaded texture: %s (%dx%d, %d channels)", 
         name.c_str(), width, height, channels);
    
    return textureId;
}

GLuint TextureManager::LoadTextureFromFile(const std::string& path) {
    // Note: On Android, load from assets
    // This is a simplified version
    if (g_assetManager == nullptr) {
        LOGE("Asset manager not initialized");
        return 0;
    }
    
    AAsset* asset = AAssetManager_open(g_assetManager, path.c_str(), AASSET_MODE_STREAMING);
    if (!asset) {
        LOGE("Failed to open asset: %s", path.c_str());
        return 0;
    }
    
    off_t length = AAsset_getLength(asset);
    std::vector<uint8_t> buffer(length);
    AAsset_read(asset, buffer.data(), length);
    AAsset_close(asset);
    
    // For now, create a placeholder texture
    // In reality, you'd use stb_image or similar to decode
    std::vector<uint8_t> placeholder(64 * 64 * 4, 255);
    return LoadTexture(path, placeholder, 64, 64, 4);
}

GLuint TextureManager::GetTexture(const std::string& name) const {
    auto it = textures_.find(name);
    if (it != textures_.end()) {
        return it->second.id;
    }
    return 0;
}

void TextureManager::BindTexture(const std::string& name, GLuint unit) {
    auto it = textures_.find(name);
    if (it != textures_.end()) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, it->second.id);
    }
}

void TextureManager::UnbindTexture(GLuint unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureManager::ReleaseTexture(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) {
        glDeleteTextures(1, &it->second.id);
        textures_.erase(it);
        LOGI("Released texture: %s", name.c_str());
    }
}

void TextureManager::ReleaseAllTextures() {
    for (auto& pair : textures_) {
        glDeleteTextures(1, &pair.second.id);
    }
    textures_.clear();
    
    for (auto& pair : atlases_) {
        glDeleteTextures(1, &pair.second.textureId);
    }
    atlases_.clear();
    
    LOGI("All textures released");
}

void TextureManager::SetFiltering(GLenum minFilter, GLenum magFilter) {
    defaultMinFilter_ = minFilter;
    defaultMagFilter_ = magFilter;
    
    // Update existing textures
    for (auto& pair : textures_) {
        glBindTexture(GL_TEXTURE_2D, pair.second.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    }
}

void TextureManager::SetWrapping(GLenum sWrap, GLenum tWrap) {
    defaultSWrap_ = sWrap;
    defaultTWrap_ = tWrap;
    
    for (auto& pair : textures_) {
        glBindTexture(GL_TEXTURE_2D, pair.second.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sWrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tWrap);
    }
}

void TextureManager::GenerateMipmaps(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) {
        glBindTexture(GL_TEXTURE_2D, it->second.id);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
}

GLuint TextureManager::CreateTextureAtlas(const std::string& atlasName,
                                         const std::vector<std::pair<std::string, std::vector<uint8_t>>>& textures,
                                         int textureSize) {
    // Create texture atlas
    int atlasSize = textureSize * static_cast<int>(ceil(sqrt(textures.size())));
    
    // Create empty texture
    std::vector<uint8_t> atlasData(atlasSize * atlasSize * 4, 0);
    
    // TODO: Pack textures into atlas
    // This is a simplified placeholder
    
    GLuint textureId = CreateTexture(atlasSize, atlasSize, 4, atlasData.data());
    if (textureId == 0) {
        return 0;
    }
    
    TextureAtlas atlas;
    atlas.textureId = textureId;
    atlas.size = atlasSize;
    
    // Create regions (placeholder)
    int currentX = 0;
    int currentY = 0;
    
    for (const auto& tex : textures) {
        AtlasRegion region;
        region.name = tex.first;
        region.uvMin = glm::vec2(
            static_cast<float>(currentX) / atlasSize,
            static_cast<float>(currentY) / atlasSize
        );
        region.uvMax = glm::vec2(
            static_cast<float>(currentX + textureSize) / atlasSize,
            static_cast<float>(currentY + textureSize) / atlasSize
        );
        region.size = glm::vec2(textureSize, textureSize);
        
        atlas.regions[tex.first] = region;
        
        currentX += textureSize;
        if (currentX + textureSize > atlasSize) {
            currentX = 0;
            currentY += textureSize;
        }
    }
    
    atlases_[atlasName] = atlas;
    return textureId;
}

TextureManager::AtlasRegion TextureManager::GetAtlasRegion(const std::string& atlasName,
                                                          const std::string& regionName) const {
    auto atlasIt = atlases_.find(atlasName);
    if (atlasIt != atlases_.end()) {
        auto regionIt = atlasIt->second.regions.find(regionName);
        if (regionIt != atlasIt->second.regions.end()) {
            return regionIt->second;
        }
    }
    
    return AtlasRegion();
}

GLuint TextureManager::CreateTexture(int width, int height, int channels, const void* data) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    
    if (textureId == 0) {
        return 0;
    }
    
    glBindTexture(GL_TEXTURE_2D, textureId);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, defaultMinFilter_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, defaultMagFilter_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, defaultSWrap_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, defaultTWrap_);
    
    // Upload texture data
    GLenum format = GetFormat(channels);
    GLenum internalFormat = format;
    
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    
    // Generate mipmaps if using mipmap filtering
    if (defaultMinFilter_ == GL_LINEAR_MIPMAP_LINEAR || 
        defaultMinFilter_ == GL_NEAREST_MIPMAP_NEAREST) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return textureId;
}

GLenum TextureManager::GetFormat(int channels) const {
    switch (channels) {
        case 1: return GL_RED;
        case 2: return GL_RG;
        case 3: return GL_RGB;
        case 4: return GL_RGBA;
        default: return GL_RGBA;
    }
}