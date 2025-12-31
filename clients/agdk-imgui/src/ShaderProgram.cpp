#include "ShaderProgram.hpp"
#include <android/log.h>
#include <fstream>
#include <sstream>

#define LOG_TAG "ShaderProgram"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

ShaderProgram::ShaderProgram() {
}

ShaderProgram::~ShaderProgram() {
    if (programId_ != 0) {
        glDeleteProgram(programId_);
    }
}

bool ShaderProgram::Load(const std::string& vertexSource, const std::string& fragmentSource) {
    // Compile shaders
    vertexShaderId_ = CompileShader(GL_VERTEX_SHADER, vertexSource);
    if (vertexShaderId_ == 0) {
        LOGE("Failed to compile vertex shader");
        return false;
    }
    
    fragmentShaderId_ = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (fragmentShaderId_ == 0) {
        LOGE("Failed to compile fragment shader");
        glDeleteShader(vertexShaderId_);
        return false;
    }
    
    // Create program
    programId_ = glCreateProgram();
    if (programId_ == 0) {
        LOGE("Failed to create shader program");
        glDeleteShader(vertexShaderId_);
        glDeleteShader(fragmentShaderId_);
        return false;
    }
    
    // Attach shaders
    glAttachShader(programId_, vertexShaderId_);
    glAttachShader(programId_, fragmentShaderId_);
    
    // Link program
    if (!LinkProgram()) {
        LOGE("Failed to link shader program");
        glDeleteProgram(programId_);
        glDeleteShader(vertexShaderId_);
        glDeleteShader(fragmentShaderId_);
        programId_ = 0;
        return false;
    }
    
    // Clean up shaders (they're linked now)
    glDeleteShader(vertexShaderId_);
    glDeleteShader(fragmentShaderId_);
    vertexShaderId_ = 0;
    fragmentShaderId_ = 0;
    
    return true;
}

void ShaderProgram::Use() const {
    if (programId_ != 0) {
        glUseProgram(programId_);
    }
}

void ShaderProgram::Unuse() const {
    glUseProgram(0);
}

void ShaderProgram::SetUniform(const std::string& name, int value) {
    GLint location = GetUniformLocation(name);
    if (location != -1) {
        glUniform1i(location, value);
    }
}

void ShaderProgram::SetUniform(const std::string& name, float value) {
    GLint location = GetUniformLocation(name);
    if (location != -1) {
        glUniform1f(location, value);
    }
}

void ShaderProgram::SetUniform(const std::string& name, const glm::vec2& value) {
    GLint location = GetUniformLocation(name);
    if (location != -1) {
        glUniform2f(location, value.x, value.y);
    }
}

void ShaderProgram::SetUniform(const std::string& name, const glm::vec3& value) {
    GLint location = GetUniformLocation(name);
    if (location != -1) {
        glUniform3f(location, value.x, value.y, value.z);
    }
}

void ShaderProgram::SetUniform(const std::string& name, const glm::vec4& value) {
    GLint location = GetUniformLocation(name);
    if (location != -1) {
        glUniform4f(location, value.x, value.y, value.z, value.w);
    }
}

void ShaderProgram::SetUniform(const std::string& name, const glm::mat3& value) {
    GLint location = GetUniformLocation(name);
    if (location != -1) {
        glUniformMatrix3fv(location, 1, GL_FALSE, &value[0][0]);
    }
}

void ShaderProgram::SetUniform(const std::string& name, const glm::mat4& value) {
    GLint location = GetUniformLocation(name);
    if (location != -1) {
        glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
    }
}

void ShaderProgram::SetUniformArray(const std::string& name, const std::vector<glm::mat4>& values) {
    GLint location = GetUniformLocation(name);
    if (location != -1 && !values.empty()) {
        glUniformMatrix4fv(location, values.size(), GL_FALSE, &values[0][0][0]);
    }
}

GLint ShaderProgram::GetAttribLocation(const std::string& name) const {
    auto it = attribLocations_.find(name);
    if (it != attribLocations_.end()) {
        return it->second;
    }
    
    GLint location = glGetAttribLocation(programId_, name.c_str());
    if (location != -1) {
        attribLocations_[name] = location;
    }
    
    return location;
}

GLuint ShaderProgram::CompileShader(GLenum type, const std::string& source) {
    GLuint shaderId = glCreateShader(type);
    if (shaderId == 0) {
        LOGE("Failed to create shader");
        return 0;
    }
    
    const char* sourceStr = source.c_str();
    glShaderSource(shaderId, 1, &sourceStr, nullptr);
    glCompileShader(shaderId);
    
    // Check compilation status
    GLint success;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shaderId, sizeof(infoLog), nullptr, infoLog);
        LOGE("Shader compilation failed: %s", infoLog);
        glDeleteShader(shaderId);
        return 0;
    }
    
    return shaderId;
}

bool ShaderProgram::LinkProgram() {
    glLinkProgram(programId_);
    
    GLint success;
    glGetProgramiv(programId_, GL_LINK_STATUS, &success);
    
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(programId_, sizeof(infoLog), nullptr, infoLog);
        LOGE("Program linking failed: %s", infoLog);
        return false;
    }
    
    // Validate program
    glValidateProgram(programId_);
    glGetProgramiv(programId_, GL_VALIDATE_STATUS, &success);
    
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(programId_, sizeof(infoLog), nullptr, infoLog);
        LOGE("Program validation failed: %s", infoLog);
        return false;
    }
    
    return true;
}

GLint ShaderProgram::GetUniformLocation(const std::string& name) const {
    auto it = uniformLocations_.find(name);
    if (it != uniformLocations_.end()) {
        return it->second;
    }
    
    GLint location = glGetUniformLocation(programId_, name.c_str());
    if (location != -1) {
        uniformLocations_[name] = location;
    } else {
        LOGE("Uniform '%s' not found", name.c_str());
    }
    
    return location;
}

std::string ShaderProgram::GetShaderLog(GLuint shaderId) const {
    GLint length;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &length);
    
    if (length > 0) {
        std::vector<GLchar> log(length);
        glGetShaderInfoLog(shaderId, length, nullptr, log.data());
        return std::string(log.data());
    }
    
    return "";
}

std::string ShaderProgram::GetProgramLog() const {
    GLint length;
    glGetProgramiv(programId_, GL_INFO_LOG_LENGTH, &length);
    
    if (length > 0) {
        std::vector<GLchar> log(length);
        glGetProgramInfoLog(programId_, length, nullptr, log.data());
        return std::string(log.data());
    }
    
    return "";
}