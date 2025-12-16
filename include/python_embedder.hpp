#pragma once
#include <Python.h>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <memory>

class PythonEmbedder {
private:
    static PythonEmbedder* instance_;
    std::mutex gil_mutex_;
    PyThreadState* main_thread_state_;
    std::string script_path_;

    // Script module cache
    std::unordered_map<std::string, PyObject*> loaded_modules_;

    PythonEmbedder();

public:
    ~PythonEmbedder();

    static PythonEmbedder& get_instance();
    static void initialize(const std::string& script_path);
    static void shutdown();

    // Call Python function with binary data
    std::vector<char> call_python_function(const std::string& module_name,
                                         const std::string& function_name,
                                         const char* input_data,
                                         size_t input_size);

    // Register C++ callback for Python to call
    void register_cpp_callback(const std::string& name,
                              std::function<std::vector<char>(const char*, size_t)> callback);

    // Execute Python script
    bool execute_script(const std::string& script_code);

    // GIL management
    class GilLock {
    private:
        PyGILState_STATE gstate_;
    public:
        GilLock() : gstate_(PyGILState_Ensure()) {}
        ~GilLock() { PyGILState_Release(gstate_); }
    };

private:
    PyObject* get_module(const std::string& module_name);
    std::vector<char> python_to_binary(PyObject* obj);
};
