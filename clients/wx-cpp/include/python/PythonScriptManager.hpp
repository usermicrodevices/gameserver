#pragma once

#include <Python.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

class PythonScriptManager {
public:
    PythonScriptManager();
    ~PythonScriptManager();
    
    bool Initialize();
    void Shutdown();
    
    // Script loading and execution
    bool LoadScript(const std::string& moduleName, const std::string& filePath);
    bool ReloadScript(const std::string& moduleName);
    bool UnloadScript(const std::string& moduleName);
    
    // Function calling
    nlohmann::json CallFunction(const std::string& moduleName,
                               const std::string& functionName,
                               const nlohmann::json& args = {});
    
    // Event system
    void RegisterEventHandler(const std::string& eventName,
                            const std::string& moduleName,
                            const std::string& functionName);
    void UnregisterEventHandler(const std::string& eventName,
                               const std::string& moduleName);
    void TriggerEvent(const std::string& eventName, const nlohmann::json& data);
    
    // Python object conversion
    static PyObject* JsonToPyObject(const nlohmann::json& json);
    static nlohmann::json PyObjectToJson(PyObject* obj);
    
    // Module management
    bool ModuleExists(const std::string& moduleName) const;
    std::vector<std::string> GetLoadedModules() const;
    
private:
    struct EventHandler {
        std::string moduleName;
        std::string functionName;
        PyObject* function{nullptr};
    };
    
    bool InitializePythonInterpreter();
    void CleanupPythonInterpreter();
    
    PyObject* GetFunction(const std::string& moduleName, const std::string& functionName);
    
    std::unordered_map<std::string, PyObject*> modules_;
    std::unordered_map<std::string, std::vector<EventHandler>> eventHandlers_;
    
    mutable std::mutex mutex_;
    bool initialized_{false};
    
    // Python threading
    PyGILState_STATE gilState_;
};