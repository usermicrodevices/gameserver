#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera();
    
    void Update(float deltaTime);
    
    void SetPosition(const glm::vec3& position);
    void SetRotation(float yaw, float pitch);
    
    void Move(const glm::vec3& direction);
    void Rotate(float yawDelta, float pitchDelta);
    void Zoom(float amount);
    
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const;
    glm::vec3 GetPosition() const { return position_; }
    glm::vec3 GetFront() const { return front_; }
    glm::vec3 GetRight() const { return right_; }
    glm::vec3 GetUp() const { return up_; }
    
    void SetProjection(float fov, float aspectRatio, float nearPlane, float farPlane);
    
private:
    void UpdateVectors();
    
    glm::vec3 position_{0.0f, 0.0f, 3.0f};
    glm::vec3 front_{0.0f, 0.0f, -1.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};
    glm::vec3 worldUp_{0.0f, 1.0f, 0.0f};
    
    float yaw_{-90.0f};
    float pitch_{0.0f};
    
    float fov_{45.0f};
    float aspectRatio_{16.0f / 9.0f};
    float nearPlane_{0.1f};
    float farPlane_{1000.0f};
    
    glm::mat4 viewMatrix_;
    glm::mat4 projectionMatrix_;
};