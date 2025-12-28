#include <filesystem>
#include <chrono>
#include <thread>

#include "../../include/scripting/PythonScripting.hpp"

namespace fs = std::filesystem;

namespace PythonScripting {

// =============== PythonModule Implementation ===============

PythonModule::PythonModule(const std::string& moduleName, const std::string& filePath)
    : moduleName_(moduleName), filePath_(filePath), module_(nullptr) {
}

PythonModule::~PythonModule() {
    Unload();
}

PythonModule::PythonModule(PythonModule&& other) noexcept
    : moduleName_(std::move(other.moduleName_)),
      filePath_(std::move(other.filePath_)),
      module_(other.module_),
      lastError_(std::move(other.lastError_)) {
    other.module_ = nullptr;
}

PythonModule& PythonModule::operator=(PythonModule&& other) noexcept {
    if (this != &other) {
        Unload();
        moduleName_ = std::move(other.moduleName_);
        filePath_ = std::move(other.filePath_);
        module_ = other.module_;
        lastError_ = std::move(other.lastError_);
        other.module_ = nullptr;
    }
    return *this;
}

bool PythonModule::Load() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (module_) {
        SetError("Module already loaded");
        return false;
    }

    PyGILGuard gil;

    try {
        // Convert module name to Python string
        PyObject* pModuleName = PyUnicode_FromString(moduleName_.c_str());
        if (!pModuleName) {
            SetError("Failed to create module name");
            return false;
        }
        PyObjectRef moduleNameRef(pModuleName);

        // Check if module is already imported
        PyObject* sysModules = PyImport_GetModuleDict();
        PyObject* existingModule = PyDict_GetItemString(sysModules, moduleName_.c_str());
        if (existingModule) {
            // Module already exists, reload it
            module_ = PyImport_ReloadModule(existingModule);
        } else {
            // Import module
            module_ = PyImport_Import(pModuleName);
        }

        if (!module_) {
            CheckPythonError();
            return false;
        }

        Logger::Info("Python module loaded: {}", moduleName_);
        return true;

    } catch (const std::exception& e) {
        SetError(std::string("Exception loading module: ") + e.what());
        return false;
    }
}

bool PythonModule::Reload() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!module_) {
        SetError("Module not loaded");
        return false;
    }

    PyGILGuard gil;

    try {
        PyObject* reloadedModule = PyImport_ReloadModule(module_);
        if (!reloadedModule) {
            CheckPythonError();
            return false;
        }

        // Swap module reference
        Py_DECREF(module_);
        module_ = reloadedModule;

        Logger::Info("Python module reloaded: {}", moduleName_);
        return true;

    } catch (const std::exception& e) {
        SetError(std::string("Exception reloading module: ") + e.what());
        return false;
    }
}

void PythonModule::Unload() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (module_) {
        PyGILGuard gil;
        Py_DECREF(module_);
        module_ = nullptr;
        Logger::Debug("Python module unloaded: {}", moduleName_);
    }
}

bool PythonModule::CallFunction(const std::string& funcName, const nlohmann::json& args) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!module_) {
        SetError("Module not loaded");
        return false;
    }

    PyGILGuard gil;

    try {
        // Get function from module
        PyObject* pFunc = PyObject_GetAttrString(module_, funcName.c_str());
        if (!pFunc) {
            SetError("Function not found: " + funcName);
            return false;
        }
        PyObjectRef funcRef(pFunc);

        // Check if callable
        if (!PyCallable_Check(pFunc)) {
            SetError("Object is not callable: " + funcName);
            return false;
        }

        // Create arguments
        PyObject* pArgs = CreatePyArgs(args);
        if (!pArgs && !args.empty()) {
            SetError("Failed to create arguments");
            return false;
        }
        PyObjectRef argsRef(pArgs);

        // Call function
        PyObject* pResult = PyObject_CallObject(pFunc, pArgs);
        if (!pResult) {
            CheckPythonError();
            return false;
        }
        PyObjectRef resultRef(pResult);

        return true;

    } catch (const std::exception& e) {
        SetError(std::string("Exception calling function: ") + e.what());
        return false;
    }
}

nlohmann::json PythonModule::CallFunctionWithResult(const std::string& funcName,
                                                   const nlohmann::json& args) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!module_) {
        SetError("Module not loaded");
        return nlohmann::json();
    }

    PyGILGuard gil;

    try {
        // Get function from module
        PyObject* pFunc = PyObject_GetAttrString(module_, funcName.c_str());
        if (!pFunc) {
            SetError("Function not found: " + funcName);
            return nlohmann::json();
        }
        PyObjectRef funcRef(pFunc);

        // Check if callable
        if (!PyCallable_Check(pFunc)) {
            SetError("Object is not callable: " + funcName);
            return nlohmann::json();
        }

        // Create arguments
        PyObject* pArgs = CreatePyArgs(args);
        if (!pArgs && !args.empty()) {
            SetError("Failed to create arguments");
            return nlohmann::json();
        }
        PyObjectRef argsRef(pArgs);

        // Call function
        PyObject* pResult = PyObject_CallObject(pFunc, pArgs);
        if (!pResult) {
            CheckPythonError();
            return nlohmann::json();
        }
        PyObjectRef resultRef(pResult);

        // Convert result to JSON
        return PyObjectToJson(pResult);

    } catch (const std::exception& e) {
        SetError(std::string("Exception calling function: ") + e.what());
        return nlohmann::json();
    }
}

bool PythonModule::HasFunction(const std::string& funcName) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!module_) {
        return false;
    }

    PyGILGuard gil;

    PyObject* pFunc = PyObject_GetAttrString(module_, funcName.c_str());
    if (!pFunc) {
        PyErr_Clear();
        return false;
    }

    bool isCallable = PyCallable_Check(pFunc) != 0;
    Py_DECREF(pFunc);

    return isCallable;
}

std::string PythonModule::GetLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

PyObject* PythonModule::CreatePyArgs(const nlohmann::json& args) {
    if (args.is_null() || args.empty()) {
        return nullptr;
    }

    if (args.is_array()) {
        PyObject* pList = PyList_New(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            PyObject* pItem = JsonToPyObject(args[i]);
            if (!pItem) {
                Py_DECREF(pList);
                return nullptr;
            }
            PyList_SET_ITEM(pList, i, pItem);
        }
        return pList;
    }

    if (args.is_object()) {
        PyObject* pDict = PyDict_New();
        for (const auto& [key, value] : args.items()) {
            PyObject* pKey = PyUnicode_FromString(key.c_str());
            PyObject* pValue = JsonToPyObject(value);
            if (!pKey || !pValue) {
                if (pKey) Py_DECREF(pKey);
                if (pValue) Py_DECREF(pValue);
                Py_DECREF(pDict);
                return nullptr;
            }
            PyDict_SetItem(pDict, pKey, pValue);
            Py_DECREF(pKey);
            Py_DECREF(pValue);
        }
        return pDict;
    }

    // Single value
    PyObject* pValue = JsonToPyObject(args);
    if (!pValue) {
        return nullptr;
    }

    PyObject* pArgs = PyTuple_New(1);
    PyTuple_SET_ITEM(pArgs, 0, pValue);
    return pArgs;
}

nlohmann::json PythonModule::PyObjectToJson(PyObject* obj) {
    if (!obj) {
        return nlohmann::json();
    }

    // Handle None
    if (obj == Py_None) {
        return nlohmann::json();
    }

    // Handle bool
    if (PyBool_Check(obj)) {
        return nlohmann::json(obj == Py_True);
    }

    // Handle int
    if (PyLong_Check(obj)) {
        long value = PyLong_AsLong(obj);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return nlohmann::json();
        }
        return nlohmann::json(value);
    }

    // Handle float
    if (PyFloat_Check(obj)) {
        double value = PyFloat_AsDouble(obj);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return nlohmann::json();
        }
        return nlohmann::json(value);
    }

    // Handle string
    if (PyUnicode_Check(obj)) {
        PyObject* utf8 = PyUnicode_AsUTF8String(obj);
        if (!utf8) {
            PyErr_Clear();
            return nlohmann::json();
        }
        const char* str = PyBytes_AsString(utf8);
        nlohmann::json result = nlohmann::json(str);
        Py_DECREF(utf8);
        return result;
    }

    // Handle bytes
    if (PyBytes_Check(obj)) {
        const char* str = PyBytes_AsString(obj);
        return nlohmann::json(str);
    }

    // Handle list
    if (PyList_Check(obj)) {
        nlohmann::json::array_t array;
        Py_ssize_t size = PyList_Size(obj);
        for (Py_ssize_t i = 0; i < size; ++i) {
            PyObject* item = PyList_GetItem(obj, i);
            array.push_back(PyObjectToJson(item));
        }
        return nlohmann::json(array);
    }

    // Handle tuple
    if (PyTuple_Check(obj)) {
        nlohmann::json::array_t array;
        Py_ssize_t size = PyTuple_Size(obj);
        for (Py_ssize_t i = 0; i < size; ++i) {
            PyObject* item = PyTuple_GetItem(obj, i);
            array.push_back(PyObjectToJson(item));
        }
        return nlohmann::json(array);
    }

    // Handle dict
    if (PyDict_Check(obj)) {
        nlohmann::json object;
        PyObject* key;
        PyObject* value;
        Py_ssize_t pos = 0;

        while (PyDict_Next(obj, &pos, &key, &value)) {
            std::string keyStr = PyObjectToString(key);
            if (!keyStr.empty()) {
                object[keyStr] = PyObjectToJson(value);
            }
        }
        return object;
    }

    // Try to convert to string as fallback
    PyObject* strObj = PyObject_Str(obj);
    if (strObj) {
        std::string str = PyObjectToString(strObj);
        Py_DECREF(strObj);
        return nlohmann::json(str);
    }

    return nlohmann::json();
}

PyObject* PythonModule::JsonToPyObject(const nlohmann::json& json) {
    if (json.is_null()) {
        Py_RETURN_NONE;
    }

    if (json.is_boolean()) {
        return PyBool_FromLong(json.get<bool>());
    }

    if (json.is_number_integer()) {
        return PyLong_FromLong(json.get<long>());
    }

    if (json.is_number_float()) {
        return PyFloat_FromDouble(json.get<double>());
    }

    if (json.is_string()) {
        return PyUnicode_FromString(json.get<std::string>().c_str());
    }

    if (json.is_array()) {
        PyObject* list = PyList_New(json.size());
        for (size_t i = 0; i < json.size(); ++i) {
            PyObject* item = JsonToPyObject(json[i]);
            if (!item) {
                Py_DECREF(list);
                return nullptr;
            }
            PyList_SET_ITEM(list, i, item);
        }
        return list;
    }

    if (json.is_object()) {
        PyObject* dict = PyDict_New();
        for (const auto& [key, value] : json.items()) {
            PyObject* pyValue = JsonToPyObject(value);
            if (!pyValue) {
                Py_DECREF(dict);
                return nullptr;
            }
            PyDict_SetItemString(dict, key.c_str(), pyValue);
            Py_DECREF(pyValue);
        }
        return dict;
    }

    return nullptr;
}

std::string PythonModule::PyObjectToString(PyObject* obj) {
    if (!obj) {
        return "";
    }

    PyObject* strObj = PyObject_Str(obj);
    if (!strObj) {
        PyErr_Clear();
        return "";
    }

    PyObject* utf8 = PyUnicode_AsUTF8String(strObj);
    if (!utf8) {
        Py_DECREF(strObj);
        PyErr_Clear();
        return "";
    }

    const char* str = PyBytes_AsString(utf8);
    std::string result = str ? str : "";

    Py_DECREF(utf8);
    Py_DECREF(strObj);

    return result;
}

void PythonModule::SetError(const std::string& error) {
    lastError_ = error;
    Logger::Error("Python module error [{}]: {}", moduleName_, error);
}

void PythonModule::ClearError() {
    lastError_.clear();
}

bool PythonModule::CheckPythonError() {
    if (PyErr_Occurred()) {
        PyObject* type;
        PyObject* value;
        PyObject* traceback;

        PyErr_Fetch(&type, &value, &traceback);

        std::string errorMsg;
        if (value) {
            errorMsg = PyObjectToString(value);
        } else if (type) {
            errorMsg = PyObjectToString(type);
        } else {
            errorMsg = "Unknown Python error";
        }

        SetError(errorMsg);

        PyErr_Clear();

        if (traceback) {
            Py_DECREF(traceback);
        }
        if (value) {
            Py_DECREF(value);
        }
        if (type) {
            Py_DECREF(type);
        }

        return true;
    }

    return false;
}

// =============== PythonScripting Implementation ===============

PythonScripting& PythonScripting::GetInstance() {
    static PythonScripting instance;
    return instance;
}

PythonScripting::PythonScripting()
    : initialized_(false) {
}

PythonScripting::~PythonScripting() {
    Shutdown();
}

bool PythonScripting::Initialize() {
    if (initialized_) {
        Logger::Warn("PythonScripting already initialized");
        return true;
    }

    Logger::Info("Initializing Python scripting engine...");

    // Get configuration
    auto& config = ConfigManager::GetInstance();
    std::string pythonHome = config.GetString("python.home", "");

    if (!pythonHome.empty()) {
        pythonHome_ = pythonHome;
    }

    // Initialize Python
    if (!InitializePython()) {
        Logger::Error("Failed to initialize Python interpreter");
        return false;
    }

    // Set Python paths
    std::vector<std::string> paths = {
        "./scripts",
        "./python",
        "./lib/python",
        "."
    };

    for (const auto& path : paths) {
        AddPythonPath(path);
    }

    // Add additional paths from config
    auto pythonPaths = config.GetStringArray("python.paths");
    for (const auto& path : pythonPaths) {
        AddPythonPath(path);
    }

    // Initialize Python API
    PythonAPI::Initialize();

    // Load default modules
    std::string scriptDir = config.GetString("python.script_dir", "./scripts");
    if (fs::exists(scriptDir)) {
        for (const auto& entry : fs::directory_iterator(scriptDir)) {
            if (entry.path().extension() == ".py") {
                std::string moduleName = entry.path().stem().string();
                std::string filePath = entry.path().string();

                if (LoadModule(moduleName, filePath)) {
                    Logger::Info("Loaded Python module: {}", moduleName);
                } else {
                    Logger::Warn("Failed to load Python module: {}", moduleName);
                }
            }
        }
    }

    initialized_ = true;
    Logger::Info("Python scripting engine initialized successfully");
    return true;
}

void PythonScripting::Shutdown() {
    if (!initialized_) {
        return;
    }

    Logger::Info("Shutting down Python scripting engine...");

    {
        std::unique_lock<std::shared_mutex> lock(modulesMutex_);
        modules_.clear();
    }

    {
        std::unique_lock<std::shared_mutex> lock(eventHandlersMutex_);
        eventHandlers_.clear();
    }

    {
        std::unique_lock<std::shared_mutex> lock(callbacksMutex_);
        callbacks_.clear();
    }

    ShutdownPython();

    initialized_ = false;
    Logger::Info("Python scripting engine shutdown complete");
}

bool PythonScripting::InitializePython() {
    try {
        // Set Python home if specified
        if (!pythonHome_.empty()) {
            Py_SetPythonHome(Py_DecodeLocale(pythonHome_.c_str(), nullptr));
        }

        // Initialize Python
        Py_Initialize();
        if (!Py_IsInitialized()) {
            Logger::Error("Failed to initialize Python interpreter");
            return false;
        }

        // Initialize threads
        PyEval_InitThreads();

        // Release GIL - we'll acquire it when needed
        PyThreadState* mainThread = PyEval_SaveThread();
        if (!mainThread) {
            Logger::Error("Failed to save Python thread state");
            return false;
        }

        // Import essential modules
        PyGILGuard gil;

        PyRun_SimpleString("import sys");
        PyRun_SimpleString("import os");
        PyRun_SimpleString("import json");
        PyRun_SimpleString("import math");
        PyRun_SimpleString("import random");
        PyRun_SimpleString("import time");

        Logger::Debug("Python interpreter initialized (version: {})", Py_GetVersion());
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Exception initializing Python: {}", e.what());
        return false;
    }
}

void PythonScripting::ShutdownPython() {
    try {
        PyGILGuard gil;
        Py_FinalizeEx();
        Logger::Debug("Python interpreter finalized");
    } catch (const std::exception& e) {
        Logger::Error("Exception finalizing Python: {}", e.what());
    }
}

bool PythonScripting::AddPythonPath(const std::string& path) {
    if (!initialized_) {
        Logger::Error("Python not initialized");
        return false;
    }

    PyGILGuard gil;

    try {
        std::string code = "import sys\n";
        code += "if '" + path + "' not in sys.path:\n";
        code += "    sys.path.insert(0, '" + path + "')\n";

        PyRun_SimpleString(code.c_str());

        pythonPaths_.push_back(path);
        Logger::Debug("Added Python path: {}", path);
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Failed to add Python path {}: {}", path, e.what());
        return false;
    }
}

bool PythonScripting::ExecuteString(const std::string& code) {
    if (!initialized_) {
        Logger::Error("Python not initialized");
        return false;
    }

    PyGILGuard gil;

    try {
        int result = PyRun_SimpleString(code.c_str());
        if (result != 0) {
            Logger::Error("Python execution failed for code: {}", code);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Exception executing Python code: {}", e.what());
        return false;
    }
}

bool PythonScripting::LoadModule(const std::string& name, const std::string& path) {
    if (!initialized_) {
        Logger::Error("Python not initialized");
        return false;
    }

    {
        std::unique_lock<std::shared_mutex> lock(modulesMutex_);
        if (modules_.find(name) != modules_.end()) {
            Logger::Warn("Module already loaded: {}", name);
            return true;
        }

        auto module = std::make_unique<PythonModule>(name, path);
        if (!module->Load()) {
            Logger::Error("Failed to load Python module {}: {}", name, module->GetLastError());
            return false;
        }

        modules_[name] = std::move(module);
    }

    Logger::Info("Python module loaded: {}", name);
    return true;
}

bool PythonScripting::UnloadModule(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(modulesMutex_);

    auto it = modules_.find(name);
    if (it == modules_.end()) {
        Logger::Warn("Module not found: {}", name);
        return false;
    }

    // Remove any event handlers for this module
    {
        std::unique_lock<std::shared_mutex> eventLock(eventHandlersMutex_);
        for (auto& [eventName, handlers] : eventHandlers_) {
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(),
                    [&name](const auto& handler) {
                        return handler.first == name;
                    }),
                handlers.end()
            );
        }
    }

    it->second->Unload();
    modules_.erase(it);

    Logger::Info("Python module unloaded: {}", name);
    return true;
}

bool PythonScripting::ReloadModule(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(modulesMutex_);

    auto it = modules_.find(name);
    if (it == modules_.end()) {
        Logger::Warn("Module not found: {}", name);
        return false;
    }

    if (!it->second->Reload()) {
        Logger::Error("Failed to reload Python module {}: {}", name, it->second->GetLastError());
        return false;
    }

    Logger::Info("Python module reloaded: {}", name);
    return true;
}

void PythonScripting::RegisterEventHandler(const std::string& eventName,
                                         const std::string& moduleName,
                                         const std::string& functionName) {
    std::unique_lock<std::shared_mutex> lock(eventHandlersMutex_);

    // Check if module exists
    {
        std::shared_lock<std::shared_mutex> modulesLock(modulesMutex_);
        auto it = modules_.find(moduleName);
        if (it == modules_.end()) {
            Logger::Error("Cannot register event handler: module not found: {}", moduleName);
            return;
        }

        if (!it->second->HasFunction(functionName)) {
            Logger::Error("Cannot register event handler: function not found: {}.{}",
                         moduleName, functionName);
            return;
        }
    }

    eventHandlers_[eventName].emplace_back(moduleName, functionName);
    Logger::Debug("Registered event handler: {} -> {}.{}",
                 eventName, moduleName, functionName);
}

void PythonScripting::UnregisterEventHandler(const std::string& eventName,
                                           const std::string& moduleName) {
    std::unique_lock<std::shared_mutex> lock(eventHandlersMutex_);

    auto it = eventHandlers_.find(eventName);
    if (it == eventHandlers_.end()) {
        return;
    }

    auto& handlers = it->second;
    handlers.erase(
        std::remove_if(handlers.begin(), handlers.end(),
            [&moduleName](const auto& handler) {
                return handler.first == moduleName;
            }),
        handlers.end()
    );

    if (handlers.empty()) {
        eventHandlers_.erase(it);
    }

    Logger::Debug("Unregistered event handlers for {} from module {}",
                 eventName, moduleName);
}

bool PythonScripting::FireEvent(const GameEvent& event) {
    return FireEvent(event.name, event.ToJson());
}

bool PythonScripting::FireEvent(const std::string& eventName, const nlohmann::json& data) {
    if (!initialized_) {
        return false;
    }

    std::vector<std::pair<std::string, std::string>> handlers;

    {
        std::shared_lock<std::shared_mutex> lock(eventHandlersMutex_);
        auto it = eventHandlers_.find(eventName);
        if (it == eventHandlers_.end()) {
            return false;
        }
        handlers = it->second;
    }

    if (handlers.empty()) {
        return false;
    }

    bool anySuccess = false;
    for (const auto& [moduleName, functionName] : handlers) {
        std::shared_lock<std::shared_mutex> modulesLock(modulesMutex_);

        auto moduleIt = modules_.find(moduleName);
        if (moduleIt == modules_.end()) {
            Logger::Warn("Module not found for event handler: {}", moduleName);
            continue;
        }

        nlohmann::json eventData = {
            {"event", eventName},
            {"data", data},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        if (moduleIt->second->CallFunction(functionName, eventData)) {
            anySuccess = true;
        } else {
            Logger::Warn("Event handler failed: {}.{} for event {}",
                        moduleName, functionName, eventName);
        }
    }

    return anySuccess;
}

bool PythonScripting::CallFunction(const std::string& moduleName,
                                 const std::string& functionName,
                                 const nlohmann::json& args) {
    if (!initialized_) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(modulesMutex_);

    auto it = modules_.find(moduleName);
    if (it == modules_.end()) {
        Logger::Error("Module not found: {}", moduleName);
        return false;
    }

    return it->second->CallFunction(functionName, args);
}

nlohmann::json PythonScripting::CallFunctionWithResult(const std::string& moduleName,
                                                     const std::string& functionName,
                                                     const nlohmann::json& args) {
    if (!initialized_) {
        return nlohmann::json();
    }

    std::shared_lock<std::shared_mutex> lock(modulesMutex_);

    auto it = modules_.find(moduleName);
    if (it == modules_.end()) {
        Logger::Error("Module not found: {}", moduleName);
        return nlohmann::json();
    }

    return it->second->CallFunctionWithResult(functionName, args);
}

void PythonScripting::RegisterCallback(const std::string& callbackName, PyCallback callback) {
    std::unique_lock<std::shared_mutex> lock(callbacksMutex_);
    callbacks_[callbackName] = std::move(callback);
    Logger::Debug("Registered Python callback: {}", callbackName);
}

void PythonScripting::UnregisterCallback(const std::string& callbackName) {
    std::unique_lock<std::shared_mutex> lock(callbacksMutex_);
    callbacks_.erase(callbackName);
    Logger::Debug("Unregistered Python callback: {}", callbackName);
}

bool PythonScripting::HasCallback(const std::string& callbackName) const {
    std::shared_lock<std::shared_mutex> lock(callbacksMutex_);
    return callbacks_.find(callbackName) != callbacks_.end();
}

std::vector<std::string> PythonScripting::GetLoadedModules() const {
    std::vector<std::string> result;

    std::shared_lock<std::shared_mutex> lock(modulesMutex_);
    for (const auto& [name, module] : modules_) {
        result.push_back(name);
    }

    return result;
}

std::vector<std::string> PythonScripting::GetRegisteredEvents() const {
    std::vector<std::string> result;

    std::shared_lock<std::shared_mutex> lock(eventHandlersMutex_);
    for (const auto& [eventName, handlers] : eventHandlers_) {
        result.push_back(eventName);
    }

    return result;
}

std::vector<std::string> PythonScripting::GetRegisteredCallbacks() const {
    std::vector<std::string> result;

    std::shared_lock<std::shared_mutex> lock(callbacksMutex_);
    for (const auto& [callbackName, callback] : callbacks_) {
        result.push_back(callbackName);
    }

    return result;
}

} // namespace PythonScripting
