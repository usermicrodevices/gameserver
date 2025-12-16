#include "python_embedder.hpp"
#include <stdexcept>
#include <iostream>

PythonEmbedder* PythonEmbedder::instance_ = nullptr;

PythonEmbedder::PythonEmbedder() {
    Py_Initialize();
    PyEval_InitThreads();

    // Save main thread state
    main_thread_state_ = PyEval_SaveThread();

    // Add script directory to Python path
    {
        GilLock lock;
        PyObject* sys_path = PySys_GetObject("path");
        PyObject* path = PyUnicode_FromString(script_path_.c_str());
        PyList_Append(sys_path, path);
        Py_DECREF(path);
    }
}

PythonEmbedder::~PythonEmbedder() {
    {
        GilLock lock;
        for (auto& pair : loaded_modules_) {
            Py_DECREF(pair.second);
        }
        loaded_modules_.clear();
    }

    PyEval_RestoreThread(main_thread_state_);
    Py_Finalize();
}

PythonEmbedder& PythonEmbedder::get_instance() {
    if (!instance_) {
        throw std::runtime_error("PythonEmbedder not initialized");
    }
    return *instance_;
}

void PythonEmbedder::initialize(const std::string& script_path) {
    if (!instance_) {
        instance_ = new PythonEmbedder();
        instance_->script_path_ = script_path;
    }
}

void PythonEmbedder::shutdown() {
    if (instance_) {
        delete instance_;
        instance_ = nullptr;
    }
}

std::vector<char> PythonEmbedder::call_python_function(
    const std::string& module_name,
    const std::string& function_name,
    const char* input_data,
    size_t input_size) {

    GilLock lock;

    try {
        PyObject* module = get_module(module_name);
        if (!module) {
            throw std::runtime_error("Module not found: " + module_name);
        }

        PyObject* func = PyObject_GetAttrString(module, function_name.c_str());
        if (!func || !PyCallable_Check(func)) {
            Py_XDECREF(func);
            throw std::runtime_error("Function not found: " + function_name);
        }

        // Convert binary data to Python bytes
        PyObject* input_bytes = PyBytes_FromStringAndSize(input_data, input_size);

        // Call the function
        PyObject* args = PyTuple_New(1);
        PyTuple_SetItem(args, 0, input_bytes);

        PyObject* result = PyObject_CallObject(func, args);
        Py_DECREF(args);
        Py_DECREF(func);

        if (!result) {
            PyErr_Print();
            throw std::runtime_error("Python function call failed");
        }

        std::vector<char> output = python_to_binary(result);
        Py_DECREF(result);

        return output;

    } catch (const std::exception& e) {
        std::cerr << "Python call error: " << e.what() << std::endl;
        return {};
    }
}

PyObject* PythonEmbedder::get_module(const std::string& module_name) {
    auto it = loaded_modules_.find(module_name);
    if (it != loaded_modules_.end()) {
        return it->second;
    }

    PyObject* module_name_obj = PyUnicode_FromString(module_name.c_str());
    PyObject* module = PyImport_Import(module_name_obj);
    Py_DECREF(module_name_obj);

    if (module) {
        loaded_modules_[module_name] = module;
    }

    return module;
}

std::vector<char> PythonEmbedder::python_to_binary(PyObject* obj) {
    std::vector<char> result;

    if (PyBytes_Check(obj)) {
        char* buffer;
        Py_ssize_t length;
        PyBytes_AsStringAndSize(obj, &buffer, &length);
        result.assign(buffer, buffer + length);
    } else if (PyByteArray_Check(obj)) {
        char* buffer = PyByteArray_AsString(obj);
        Py_ssize_t length = PyByteArray_Size(obj);
        result.assign(buffer, buffer + length);
    } else {
        // Try to convert to bytes
        PyObject* bytes = PyBytes_FromObject(obj);
        if (bytes) {
            char* buffer;
            Py_ssize_t length;
            PyBytes_AsStringAndSize(bytes, &buffer, &length);
            result.assign(buffer, buffer + length);
            Py_DECREF(bytes);
        }
    }

    return result;
}
