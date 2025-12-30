#include "PythonScriptManager.h"
#include <iostream>
#include <fstream>
#include <sstream>

PythonScriptManager::PythonScriptManager()
    : initialized_(false) {
}

PythonScriptManager::~PythonScriptManager() {
    Shutdown();
}

bool PythonScriptManager::Initialize() {
    if (initialized_) {
        return true;
    }
    
    if (!InitializePythonInterpreter()) {
        std::cerr << "Failed to initialize Python interpreter" << std::endl;
        return false;
    }
    
    // Import main modules
    PyObject* mainModule = PyImport_AddModule("__main__");
    if (!mainModule) {
        std::cerr << "Failed to get main module" << std::endl;
        return false;
    }
    
    // Initialize game modules
    PyObject* gameModule = PyImport_ImportModule("game");
    if (gameModule) {
        modules_["game"] = gameModule;
        Py_DECREF(gameModule);
    }
    
    PyObject* clientModule = PyImport_ImportModule("client");
    if (clientModule) {
        modules_["client"] = clientModule;
        Py_DECREF(clientModule);
    }
    
    initialized_ = true;
    return true;
}

void PythonScriptManager::Shutdown() {
    if (!initialized_) return;
    
    // Clear event handlers
    eventHandlers_.clear();
    
    // Clear modules
    for (auto& pair : modules_) {
        Py_XDECREF(pair.second);
    }
    modules_.clear();
    
    CleanupPythonInterpreter();
    initialized_ = false;
}

bool PythonScriptManager::LoadScript(const std::string& moduleName, const std::string& filePath) {
    if (!initialized_) {
        if (!Initialize()) {
            return false;
        }
    }
    
    // Read script file
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open script file: " << filePath << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string script = buffer.str();
    file.close();
    
    // Create module
    PyObject* module = PyImport_AddModule(moduleName.c_str());
    if (!module) {
        std::cerr << "Failed to create module: " << moduleName << std::endl;
        return false;
    }
    
    // Execute script
    PyObject* globals = PyModule_GetDict(module);
    PyObject* locals = PyDict_New();
    
    PyObject* result = PyRun_String(script.c_str(), Py_file_input, globals, locals);
    
    if (!result) {
        PyErr_Print();
        Py_DECREF(locals);
        return false;
    }
    
    Py_DECREF(result);
    Py_DECREF(locals);
    
    // Store module reference
    modules_[moduleName] = module;
    Py_INCREF(module);
    
    std::cout << "Loaded script: " << moduleName << std::endl;
    return true;
}

bool PythonScriptManager::ReloadScript(const std::string& moduleName) {
    auto it = modules_.find(moduleName);
    if (it == modules_.end()) {
        std::cerr << "Module not found: " << moduleName << std::endl;
        return false;
    }
    
    // Get file path from module
    // This would need to track file paths
    
    // For now, just re-import
    PyObject* newModule = PyImport_ReloadModule(it->second);
    if (!newModule) {
        PyErr_Print();
        return false;
    }
    
    // Update module reference
    Py_DECREF(it->second);
    it->second = newModule;
    
    // Re-register event handlers for this module
    // This would need to be implemented
    
    std::cout << "Reloaded script: " << moduleName << std::endl;
    return true;
}

bool PythonScriptManager::UnloadScript(const std::string& moduleName) {
    // Remove event handlers for this module
    for (auto& pair : eventHandlers_) {
        auto& handlers = pair.second;
        handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
            [&moduleName](const EventHandler& handler) {
                return handler.moduleName == moduleName;
            }),
            handlers.end());
    }
    
    // Remove module
    auto it = modules_.find(moduleName);
    if (it != modules_.end()) {
        Py_DECREF(it->second);
        modules_.erase(it);
        
        std::cout << "Unloaded script: " << moduleName << std::endl;
        return true;
    }
    
    return false;
}

nlohmann::json PythonScriptManager::CallFunction(const std::string& moduleName,
                                                const std::string& functionName,
                                                const nlohmann::json& args) {
    if (!initialized_) {
        return nlohmann::json{{"error", "Python not initialized"}};
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    try {
        // Get module
        auto it = modules_.find(moduleName);
        if (it == modules_.end()) {
            PyGILState_Release(gstate);
            return nlohmann::json{{"error", "Module not found"}};
        }
        
        // Get function
        PyObject* func = PyObject_GetAttrString(it->second, functionName.c_str());
        if (!func || !PyCallable_Check(func)) {
            Py_XDECREF(func);
            PyGILState_Release(gstate);
            return nlohmann::json{{"error", "Function not found or not callable"}};
        }
        
        // Convert arguments to Python objects
        PyObject* pyArgs = nullptr;
        if (!args.is_null()) {
            pyArgs = JsonToPyObject(args);
        } else {
            pyArgs = PyTuple_New(0);
        }
        
        if (!pyArgs) {
            Py_DECREF(func);
            PyGILState_Release(gstate);
            return nlohmann::json{{"error", "Failed to convert arguments"}};
        }
        
        // Call function
        PyObject* result = PyObject_CallObject(func, pyArgs);
        
        Py_DECREF(func);
        Py_DECREF(pyArgs);
        
        // Convert result to JSON
        nlohmann::json jsonResult;
        if (result) {
            jsonResult = PyObjectToJson(result);
            Py_DECREF(result);
        } else {
            PyErr_Print();
            jsonResult = nlohmann::json{{"error", "Python function call failed"}};
        }
        
        PyGILState_Release(gstate);
        return jsonResult;
    }
    catch (const std::exception& e) {
        PyGILState_Release(gstate);
        return nlohmann::json{{"error", e.what()}};
    }
}

void PythonScriptManager::RegisterEventHandler(const std::string& eventName,
                                              const std::string& moduleName,
                                              const std::string& functionName) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get function object
    PyObject* func = GetFunction(moduleName, functionName);
    if (!func) {
        std::cerr << "Failed to get function: " << moduleName << "." << functionName << std::endl;
        return;
    }
    
    EventHandler handler;
    handler.moduleName = moduleName;
    handler.functionName = functionName;
    handler.function = func;
    
    eventHandlers_[eventName].push_back(handler);
    
    std::cout << "Registered event handler: " << eventName << " -> "
              << moduleName << "." << functionName << std::endl;
}

void PythonScriptManager::UnregisterEventHandler(const std::string& eventName,
                                                const std::string& moduleName) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = eventHandlers_.find(eventName);
    if (it != eventHandlers_.end()) {
        auto& handlers = it->second;
        
        // Remove handlers for this module
        handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
            [&moduleName](const EventHandler& handler) {
                return handler.moduleName == moduleName;
            }),
            handlers.end());
        
        // Remove empty event
        if (handlers.empty()) {
            eventHandlers_.erase(it);
        }
    }
}

void PythonScriptManager::TriggerEvent(const std::string& eventName, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = eventHandlers_.find(eventName);
    if (it == eventHandlers_.end()) {
        return; // No handlers for this event
    }
    
    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    for (const auto& handler : it->second) {
        if (!handler.function) {
            continue;
        }
        
        // Convert data to Python object
        PyObject* pyData = JsonToPyObject(data);
        if (!pyData) {
            continue;
        }
        
        // Create argument tuple
        PyObject* args = PyTuple_New(1);
        PyTuple_SetItem(args, 0, pyData);
        
        // Call function
        PyObject* result = PyObject_CallObject(handler.function, args);
        
        Py_DECREF(args);
        
        if (result) {
            Py_DECREF(result);
        } else {
            PyErr_Print();
            std::cerr << "Event handler failed: " << eventName << std::endl;
        }
    }
    
    PyGILState_Release(gstate);
}

PyObject* PythonScriptManager::JsonToPyObject(const nlohmann::json& json) {
    if (json.is_null()) {
        Py_RETURN_NONE;
    }
    else if (json.is_boolean()) {
        return PyBool_FromLong(json.get<bool>());
    }
    else if (json.is_number_integer()) {
        return PyLong_FromLongLong(json.get<long long>());
    }
    else if (json.is_number_float()) {
        return PyFloat_FromDouble(json.get<double>());
    }
    else if (json.is_string()) {
        return PyUnicode_FromString(json.get<std::string>().c_str());
    }
    else if (json.is_array()) {
        PyObject* list = PyList_New(json.size());
        for (size_t i = 0; i < json.size(); ++i) {
            PyObject* item = JsonToPyObject(json[i]);
            if (item) {
                PyList_SetItem(list, i, item);
            }
        }
        return list;
    }
    else if (json.is_object()) {
        PyObject* dict = PyDict_New();
        for (auto it = json.begin(); it != json.end(); ++it) {
            PyObject* key = PyUnicode_FromString(it.key().c_str());
            PyObject* value = JsonToPyObject(it.value());
            if (key && value) {
                PyDict_SetItem(dict, key, value);
            }
            Py_XDECREF(key);
            Py_XDECREF(value);
        }
        return dict;
    }
    
    Py_RETURN_NONE;
}

nlohmann::json PythonScriptManager::PyObjectToJson(PyObject* obj) {
    if (!obj) {
        return nlohmann::json();
    }
    
    if (obj == Py_None) {
        return nlohmann::json();
    }
    else if (PyBool_Check(obj)) {
        return nlohmann::json(PyLong_AsLong(obj) != 0);
    }
    else if (PyLong_Check(obj)) {
        return nlohmann::json(PyLong_AsLongLong(obj));
    }
    else if (PyFloat_Check(obj)) {
        return nlohmann::json(PyFloat_AsDouble(obj));
    }
    else if (PyUnicode_Check(obj)) {
        PyObject* utf8 = PyUnicode_AsUTF8String(obj);
        const char* str = PyBytes_AsString(utf8);
        nlohmann::json result = nlohmann::json(str);
        Py_DECREF(utf8);
        return result;
    }
    else if (PyList_Check(obj) || PyTuple_Check(obj)) {
        PyObject* seq = PySequence_Fast(obj, "expected a sequence");
        Py_ssize_t length = PySequence_Fast_GET_SIZE(seq);
        
        nlohmann::json array = nlohmann::json::array();
        for (Py_ssize_t i = 0; i < length; ++i) {
            PyObject* item = PySequence_Fast_GET_ITEM(seq, i);
            array.push_back(PyObjectToJson(item));
        }
        
        Py_DECREF(seq);
        return array;
    }
    else if (PyDict_Check(obj)) {
        nlohmann::json object = nlohmann::json::object();
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        
        while (PyDict_Next(obj, &pos, &key, &value)) {
            if (PyUnicode_Check(key)) {
                PyObject* utf8 = PyUnicode_AsUTF8String(key);
                const char* keyStr = PyBytes_AsString(utf8);
                object[keyStr] = PyObjectToJson(value);
                Py_DECREF(utf8);
            }
        }
        
        return object;
    }
    
    return nlohmann::json();
}

bool PythonScriptManager::ModuleExists(const std::string& moduleName) const {
    return modules_.find(moduleName) != modules_.end();
}

std::vector<std::string> PythonScriptManager::GetLoadedModules() const {
    std::vector<std::string> result;
    for (const auto& pair : modules_) {
        result.push_back(pair.first);
    }
    return result;
}

bool PythonScriptManager::InitializePythonInterpreter() {
    if (Py_IsInitialized()) {
        return true;
    }
    
    // Initialize Python
    Py_Initialize();
    if (!Py_IsInitialized()) {
        return false;
    }
    
    // Initialize threads
    PyEval_InitThreads();
    
    // Save thread state and release GIL
    PyEval_SaveThread();
    
    return true;
}

void PythonScriptManager::CleanupPythonInterpreter() {
    if (Py_IsInitialized()) {
        // Ensure we have GIL
        PyGILState_Ensure();
        
        // Clean up Python
        Py_Finalize();
    }
}

PyObject* PythonScriptManager::GetFunction(const std::string& moduleName, const std::string& functionName) {
    auto it = modules_.find(moduleName);
    if (it == modules_.end()) {
        return nullptr;
    }
    
    PyObject* func = PyObject_GetAttrString(it->second, functionName.c_str());
    if (!func || !PyCallable_Check(func)) {
        Py_XDECREF(func);
        return nullptr;
    }
    
    return func;
}