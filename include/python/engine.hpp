// python/engine.hpp
#pragma once
#include <Python.h>
#include <nlohmann/json.hpp>
#include <string>
#include <mutex>

class PythonEngine {
private:
	static std::mutex python_mutex_;
	PyThreadState* main_thread_state_;

public:
	static void init(const std::string& python_path);
	PythonEngine();
	~PythonEngine();

	// Execute Python scripts
	nlohmann::json call_function(const std::string& module_name,
								 const std::string& function_name,
								 const nlohmann::json& args);

	// Load game logic modules
	void load_module(const std::string& module_path);

	// Thread-safe Python execution
	class ScopedGIL {
	private:
		PyGILState_STATE gstate_;
	public:
		ScopedGIL();
		~ScopedGIL();
	};
};
