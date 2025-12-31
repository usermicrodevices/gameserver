#pragma once

#include <string>
#include <unordered_map>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>

class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();
    
    bool Load(const std::string& vertexSource, const std::string& fragmentSource);
    void Use() const;
    void Unuse() const;
    
    // Uniform setters
    void SetUniform(const std::string& name, int value);
    void SetUniform(const std::string& name, float value);
    void SetUniform(const std::string& name, const glm::vec2& value);
    void SetUniform(const std::string& name, const glm::vec3& value);
    void SetUniform(const std::string& name, const glm::vec4& value);
    void SetUniform(const std::string& name, const glm::mat3& value);
    void SetUniform(const std::string& name, const glm::mat4& value);
    void SetUniformArray(const std::string& name, const std::vector<glm::mat4>& values);
    
    // Attribute locations
    GLint GetAttribLocation(const std::string& name) const;
    
    bool IsValid() const { return programId_ != 0; }
    
private:
    GLuint CompileShader(GLenum type, const std::string& source);
    bool LinkProgram();
    
    GLuint programId_{0};
    GLuint vertexShaderId_{0};
    GLuint fragmentShaderId_{0};
    
    mutable std::unordered_map<std::string, GLint> uniformLocations_;
    mutable std::unordered_map<std::string, GLint> attribLocations_;
    
    std::string GetShaderLog(GLuint shaderId) const;
    std::string GetProgramLog() const;
};