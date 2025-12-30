#pragma once

#include <Python.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

// Python module definitions
extern "C" {
    PyMODINIT_FUNC PyInit_game(void);
    PyMODINIT_FUNC PyInit_client(void);
    PyMODINIT_FUNC PyInit_world(void);
}

// C++ to Python binding functions
void RegisterGameBindings(PyObject* module);
void RegisterClientBindings(PyObject* module);
void RegisterWorldBindings(PyObject* module);

// Type converters
PyObject* Vec3ToPyObject(const glm::vec3& vec);
glm::vec3 PyObjectToVec3(PyObject* obj);

PyObject* JsonToPyObject(const nlohmann::json& json);
nlohmann::json PyObjectToJson(PyObject* obj);

// Game API for Python
namespace PythonAPI {
    // Network functions
    void SendMessage(const nlohmann::json& message);
    void SendChat(const std::string& message);
    
    // Player functions
    glm::vec3 GetPlayerPosition();
    void SetPlayerPosition(const glm::vec3& position);
    void MovePlayer(const glm::vec3& direction);
    
    // World functions
    nlohmann::json GetEntitiesInRadius(float radius);
    nlohmann::json GetEntity(uint64_t entityId);
    void SpawnEntity(const std::string& type, const glm::vec3& position);
    void DestroyEntity(uint64_t entityId);
    
    // UI functions
    void ShowMessage(const std::string& title, const std::string& message);
    void UpdateUI(const std::string& elementId, const nlohmann::json& data);
    
    // Event system
    void RegisterEvent(const std::string& eventName, PyObject* callback);
    void UnregisterEvent(const std::string& eventName, PyObject* callback);
}