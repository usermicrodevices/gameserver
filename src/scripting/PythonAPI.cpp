#include "scripting/PythonScripting.hpp"
#include "game/PlayerManager.hpp"
#include "database/CitusClient.hpp"
#include "network/ConnectionManager.hpp"
#include <random>
#include <uuid/uuid.h>

namespace PythonScripting {

// =============== Python C API Functions ===============

// Helper to convert nlohmann::json to Python object
PyObject* JsonToPython(const nlohmann::json& json) {
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
            PyObject* item = JsonToPython(json[i]);
            PyList_SET_ITEM(list, i, item);
        }
        return list;
    }

    if (json.is_object()) {
        PyObject* dict = PyDict_New();
        for (const auto& [key, value] : json.items()) {
            PyObject* pyValue = JsonToPython(value);
            PyDict_SetItemString(dict, key.c_str(), pyValue);
            Py_DECREF(pyValue);
        }
        return dict;
    }

    Py_RETURN_NONE;
}

// Helper to convert Python object to nlohmann::json
nlohmann::json PythonToJson(PyObject* obj) {
    if (!obj || obj == Py_None) {
        return nlohmann::json();
    }

    if (PyBool_Check(obj)) {
        return nlohmann::json(obj == Py_True);
    }

    if (PyLong_Check(obj)) {
        return nlohmann::json(PyLong_AsLong(obj));
    }

    if (PyFloat_Check(obj)) {
        return nlohmann::json(PyFloat_AsDouble(obj));
    }

    if (PyUnicode_Check(obj)) {
        PyObject* bytes = PyUnicode_AsUTF8String(obj);
        const char* str = PyBytes_AsString(bytes);
        nlohmann::json result = str;
        Py_DECREF(bytes);
        return result;
    }

    if (PyBytes_Check(obj)) {
        const char* str = PyBytes_AsString(obj);
        return nlohmann::json(str);
    }

    if (PyList_Check(obj)) {
        nlohmann::json::array_t array;
        Py_ssize_t size = PyList_Size(obj);
        for (Py_ssize_t i = 0; i < size; ++i) {
            PyObject* item = PyList_GetItem(obj, i);
            array.push_back(PythonToJson(item));
        }
        return nlohmann::json(array);
    }

    if (PyDict_Check(obj)) {
        nlohmann::json object;
        PyObject* key;
        PyObject* value;
        Py_ssize_t pos = 0;

        while (PyDict_Next(obj, &pos, &key, &value)) {
            std::string keyStr;
            if (PyUnicode_Check(key)) {
                PyObject* bytes = PyUnicode_AsUTF8String(key);
                keyStr = PyBytes_AsString(bytes);
                Py_DECREF(bytes);
            }
            object[keyStr] = PythonToJson(value);
        }
        return object;
    }

    return nlohmann::json();
}

// Python function wrappers
static PyObject* py_log_debug(PyObject* self, PyObject* args) {
    const char* message;

    if (!PyArg_ParseTuple(args, "s", &message)) {
        return nullptr;
    }

    Logger::Debug("[Python] {}", message);
    Py_RETURN_NONE;
}

static PyObject* py_log_info(PyObject* self, PyObject* args) {
    const char* message;

    if (!PyArg_ParseTuple(args, "s", &message)) {
        return nullptr;
    }

    Logger::Info("[Python] {}", message);
    Py_RETURN_NONE;
}

static PyObject* py_log_warning(PyObject* self, PyObject* args) {
    const char* message;

    if (!PyArg_ParseTuple(args, "s", &message)) {
        return nullptr;
    }

    Logger::Warn("[Python] {}", message);
    Py_RETURN_NONE;
}

static PyObject* py_log_error(PyObject* self, PyObject* args) {
    const char* message;

    if (!PyArg_ParseTuple(args, "s", &message)) {
        return nullptr;
    }

    Logger::Error("[Python] {}", message);
    Py_RETURN_NONE;
}

static PyObject* py_log_critical(PyObject* self, PyObject* args) {
    const char* message;

    if (!PyArg_ParseTuple(args, "s", &message)) {
        return nullptr;
    }

    Logger::Critical("[Python] {}", message);
    Py_RETURN_NONE;
}

static PyObject* py_get_player(PyObject* self, PyObject* args) {
    long player_id;

    if (!PyArg_ParseTuple(args, "l", &player_id)) {
        return nullptr;
    }

    auto& playerMgr = PlayerManager::GetInstance();
    auto player = playerMgr.GetPlayer(player_id);

    if (!player) {
        Py_RETURN_NONE;
    }

    return JsonToPython(player->ToJson());
}

static PyObject* py_set_player_position(PyObject* self, PyObject* args) {
    long player_id;
    double x, y, z;

    if (!PyArg_ParseTuple(args, "lddd", &player_id, &x, &y, &z)) {
        return nullptr;
    }

    auto& playerMgr = PlayerManager::GetInstance();
    auto player = playerMgr.GetPlayer(player_id);

    if (!player) {
        Py_RETURN_FALSE;
    }

    player->UpdatePosition(x, y, z);

    // Update database
    auto& dbClient = CitusClient::GetInstance();
    dbClient.UpdatePlayerPosition(player_id, x, y, z);

    Py_RETURN_TRUE;
}

static PyObject* py_give_player_item(PyObject* self, PyObject* args) {
    long player_id;
    const char* item_id;
    int count;

    if (!PyArg_ParseTuple(args, "lsi", &player_id, &item_id, &count)) {
        return nullptr;
    }

    auto& playerMgr = PlayerManager::GetInstance();
    if (playerMgr.GiveItemToPlayer(player_id, item_id, count)) {
        Py_RETURN_TRUE;
    }

    Py_RETURN_FALSE;
}

static PyObject* py_add_player_experience(PyObject* self, PyObject* args) {
    long player_id;
    long long amount;

    if (!PyArg_ParseTuple(args, "lL", &player_id, &amount)) {
        return nullptr;
    }

    auto& playerMgr = PlayerManager::GetInstance();
    auto player = playerMgr.GetPlayer(player_id);

    if (!player) {
        Py_RETURN_FALSE;
    }

    player->AddExperience(amount);
    Py_RETURN_TRUE;
}

static PyObject* py_send_message_to_player(PyObject* self, PyObject* args) {
    long player_id;
    const char* message;

    if (!PyArg_ParseTuple(args, "ls", &player_id, &message)) {
        return nullptr;
    }

    auto& playerMgr = PlayerManager::GetInstance();

    nlohmann::json msg = {
        {"type", "system_message"},
        {"message", message},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    playerMgr.SendToPlayer(player_id, msg);
    Py_RETURN_TRUE;
}

static PyObject* py_broadcast_to_nearby(PyObject* self, PyObject* args) {
    long player_id;
    const char* message;
    double radius;

    if (!PyArg_ParseTuple(args, "lsd", &player_id, &message, &radius)) {
        return nullptr;
    }

    auto& playerMgr = PlayerManager::GetInstance();

    nlohmann::json msg = {
        {"type", "broadcast_message"},
        {"message", message},
        {"source_player_id", player_id},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    playerMgr.BroadcastToNearbyPlayers(player_id, msg);
    Py_RETURN_TRUE;
}

static PyObject* py_query_database(PyObject* self, PyObject* args) {
    const char* query;

    if (!PyArg_ParseTuple(args, "s", &query)) {
        return nullptr;
    }

    auto& dbClient = CitusClient::GetInstance();
    auto result = dbClient.Query(query);

    return JsonToPython(result);
}

static PyObject* py_execute_database(PyObject* self, PyObject* args) {
    const char* query;

    if (!PyArg_ParseTuple(args, "s", &query)) {
        return nullptr;
    }

    auto& dbClient = CitusClient::GetInstance();
    bool success = dbClient.Execute(query);

    if (success) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject* py_fire_event(PyObject* self, PyObject* args) {
    const char* event_name;
    PyObject* data_obj;

    if (!PyArg_ParseTuple(args, "sO", &event_name, &data_obj)) {
        return nullptr;
    }

    nlohmann::json data = PythonToJson(data_obj);
    auto& scripting = PythonScripting::GetInstance();
    scripting.FireEvent(event_name, data);

    Py_RETURN_NONE;
}

static PyObject* py_schedule_event(PyObject* self, PyObject* args) {
    int delay_ms;
    const char* event_name;
    PyObject* data_obj;

    if (!PyArg_ParseTuple(args, "isO", &delay_ms, &event_name, &data_obj)) {
        return nullptr;
    }

    // This would be implemented with a timer/scheduler
    // For now, just fire the event immediately
    nlohmann::json data = PythonToJson(data_obj);
    auto& scripting = PythonScripting::GetInstance();

    std::thread([delay_ms, event_name, data, &scripting]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        scripting.FireEvent(event_name, data);
    }).detach();

    Py_RETURN_NONE;
}

static PyObject* py_get_current_time(PyObject* self, PyObject* args) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    return PyLong_FromLongLong(now);
}

static PyObject* py_generate_uuid(PyObject* self, PyObject* args) {
    uuid_t uuid;
    char uuid_str[37];

    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);

    return PyUnicode_FromString(uuid_str);
}

static PyObject* py_random_float(PyObject* self, PyObject* args) {
    double min, max;

    if (!PyArg_ParseTuple(args, "dd", &min, &max)) {
        return nullptr;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min, max);

    return PyFloat_FromDouble(dis(gen));
}

static PyObject* py_random_int(PyObject* self, PyObject* args) {
    long min, max;

    if (!PyArg_ParseTuple(args, "ll", &min, &max)) {
        return nullptr;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(min, max);

    return PyLong_FromLong(dis(gen));
}

static PyObject* py_distance(PyObject* self, PyObject* args) {
    double x1, y1, z1, x2, y2, z2;

    if (!PyArg_ParseTuple(args, "dddddd", &x1, &y1, &z1, &x2, &y2, &z2)) {
        return nullptr;
    }

    double dx = x2 - x1;
    double dy = y2 - y1;
    double dz = z2 - z1;

    double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

    return PyFloat_FromDouble(distance);
}

static PyObject* py_get_config(PyObject* self, PyObject* args) {
    const char* key;

    if (!PyArg_ParseTuple(args, "s", &key)) {
        return nullptr;
    }

    auto& config = ConfigManager::GetInstance();

    // Try different getter methods
    nlohmann::json value;

    if (config.HasKey(key)) {
        // Try to get as string first
        std::string strValue = config.GetString(key, "");
        if (!strValue.empty()) {
            value = strValue;
        } else {
            // Try other types
            try {
                int intValue = config.GetInt(key, 0);
                value = intValue;
            } catch (...) {
                try {
                    bool boolValue = config.GetBool(key, false);
                    value = boolValue;
                } catch (...) {
                    value = nlohmann::json();
                }
            }
        }
    }

    return JsonToPython(value);
}

// Method definitions
static PyMethodDef GameServerMethods[] = {
    // Logging
    {"log_debug", py_log_debug, METH_VARARGS, "Log debug message"},
    {"log_info", py_log_info, METH_VARARGS, "Log info message"},
    {"log_warning", py_log_warning, METH_VARARGS, "Log warning message"},
    {"log_error", py_log_error, METH_VARARGS, "Log error message"},
    {"log_critical", py_log_critical, METH_VARARGS, "Log critical message"},

    // Player functions
    {"get_player", py_get_player, METH_VARARGS, "Get player data"},
    {"set_player_position", py_set_player_position, METH_VARARGS, "Set player position"},
    {"give_player_item", py_give_player_item, METH_VARARGS, "Give item to player"},
    {"add_player_experience", py_add_player_experience, METH_VARARGS, "Add experience to player"},
    {"send_message_to_player", py_send_message_to_player, METH_VARARGS, "Send message to player"},
    {"broadcast_to_nearby", py_broadcast_to_nearby, METH_VARARGS, "Broadcast message to nearby players"},

    // Database functions
    {"query_database", py_query_database, METH_VARARGS, "Execute database query"},
    {"execute_database", py_execute_database, METH_VARARGS, "Execute database command"},

    // Event functions
    {"fire_event", py_fire_event, METH_VARARGS, "Fire game event"},
    {"schedule_event", py_schedule_event, METH_VARARGS, "Schedule delayed event"},

    // Utility functions
    {"get_current_time", py_get_current_time, METH_VARARGS, "Get current timestamp"},
    {"generate_uuid", py_generate_uuid, METH_VARARGS, "Generate UUID"},
    {"random_float", py_random_float, METH_VARARGS, "Generate random float"},
    {"random_int", py_random_int, METH_VARARGS, "Generate random integer"},
    {"distance", py_distance, METH_VARARGS, "Calculate distance between points"},

    // Configuration
    {"get_config", py_get_config, METH_VARARGS, "Get configuration value"},

    {nullptr, nullptr, 0, nullptr} // Sentinel
};

// Module definition
static struct PyModuleDef gameservermodule = {
    PyModuleDef_HEAD_INIT,
    "gameserver",  // Module name
    "Game Server Python API",  // Module documentation
    -1,  // Module keeps state in global variables
    GameServerMethods
};

// Module initialization
PyMODINIT_FUNC PyInit_gameserver(void) {
    return PyModule_Create(&gameservermodule);
}

// Initialize Python API
void PythonAPI::Initialize() {
    PyGILGuard gil;

    // Import the gameserver module
    PyObject* module = PyInit_gameserver();
    if (!module) {
        Logger::Error("Failed to initialize gameserver Python module");
        return;
    }

    // Add to sys.modules
    PyObject* sysModules = PyImport_GetModuleDict();
    PyDict_SetItemString(sysModules, "gameserver", module);
    Py_DECREF(module);

    // Also import as server for convenience
    PyDict_SetItemString(sysModules, "server", module);

    Logger::Debug("Python API initialized");
}

// C++ wrapper functions
void PythonAPI::LogDebug(const std::string& message) {
    Logger::Debug("[Python API] {}", message);
}

void PythonAPI::LogInfo(const std::string& message) {
    Logger::Info("[Python API] {}", message);
}

void PythonAPI::LogWarning(const std::string& message) {
    Logger::Warn("[Python API] {}", message);
}

void PythonAPI::LogError(const std::string& message) {
    Logger::Error("[Python API] {}", message);
}

void PythonAPI::LogCritical(const std::string& message) {
    Logger::Critical("[Python API] {}", message);
}

nlohmann::json PythonAPI::GetPlayer(int64_t playerId) {
    auto& playerMgr = PlayerManager::GetInstance();
    auto player = playerMgr.GetPlayer(playerId);

    if (player) {
        return player->ToJson();
    }

    return nlohmann::json();
}

bool PythonAPI::SetPlayerPosition(int64_t playerId, float x, float y, float z) {
    auto& playerMgr = PlayerManager::GetInstance();
    auto player = playerMgr.GetPlayer(playerId);

    if (!player) {
        return false;
    }

    player->UpdatePosition(x, y, z);
    return true;
}

bool PythonAPI::GivePlayerItem(int64_t playerId, const std::string& itemId, int count) {
    auto& playerMgr = PlayerManager::GetInstance();
    return playerMgr.GiveItemToPlayer(playerId, itemId, count);
}

bool PythonAPI::TakePlayerItem(int64_t playerId, const std::string& itemId, int count) {
    auto& playerMgr = PlayerManager::GetInstance();

    // This function would need to be added to PlayerManager
    // For now, return false
    return false;
}

bool PythonAPI::AddPlayerExperience(int64_t playerId, int64_t amount) {
    auto& playerMgr = PlayerManager::GetInstance();
    auto player = playerMgr.GetPlayer(playerId);

    if (!player) {
        return false;
    }

    player->AddExperience(amount);
    return true;
}

bool PythonAPI::SetPlayerHealth(int64_t playerId, int health) {
    auto& playerMgr = PlayerManager::GetInstance();
    auto player = playerMgr.GetPlayer(playerId);

    if (!player) {
        return false;
    }

    player->SetHealth(health);
    return true;
}

bool PythonAPI::SetPlayerMana(int64_t playerId, int mana) {
    auto& playerMgr = PlayerManager::GetInstance();
    auto player = playerMgr.GetPlayer(playerId);

    if (!player) {
        return false;
    }

    player->SetMana(mana);
    return true;
}

bool PythonAPI::TeleportPlayer(int64_t playerId, float x, float y, float z) {
    auto& playerMgr = PlayerManager::GetInstance();
    playerMgr.TeleportPlayer(playerId, x, y, z);
    return true;
}

bool PythonAPI::SendMessageToPlayer(int64_t playerId, const std::string& message) {
    auto& playerMgr = PlayerManager::GetInstance();

    nlohmann::json msg = {
        {"type", "system_message"},
        {"message", message},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    playerMgr.SendToPlayer(playerId, msg);
    return true;
}

bool PythonAPI::BroadcastToNearby(int64_t playerId, const std::string& message, float radius) {
    auto& playerMgr = PlayerManager::GetInstance();

    nlohmann::json msg = {
        {"type", "broadcast_message"},
        {"message", message},
        {"source_player_id", playerId},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    playerMgr.BroadcastToNearbyPlayers(playerId, msg);
    return true;
}

nlohmann::json PythonAPI::QueryDatabase(const std::string& query) {
    auto& dbClient = CitusClient::GetInstance();
    return dbClient.Query(query);
}

bool PythonAPI::ExecuteDatabase(const std::string& query) {
    auto& dbClient = CitusClient::GetInstance();
    return dbClient.Execute(query);
}

nlohmann::json PythonAPI::GetPlayerFromDB(int64_t playerId) {
    auto& dbClient = CitusClient::GetInstance();
    return dbClient.GetPlayer(playerId);
}

bool PythonAPI::SavePlayerToDB(int64_t playerId, const nlohmann::json& data) {
    auto& dbClient = CitusClient::GetInstance();
    return dbClient.UpdatePlayer(playerId, data);
}

void PythonAPI::FireEvent(const std::string& eventName, const nlohmann::json& data) {
    auto& scripting = PythonScripting::GetInstance();
    scripting.FireEvent(eventName, data);
}

void PythonAPI::ScheduleEvent(int delayMs, const std::string& eventName, const nlohmann::json& data) {
    std::thread([delayMs, eventName, data]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

        auto& scripting = PythonScripting::GetInstance();
        scripting.FireEvent(eventName, data);
    }).detach();
}

int64_t PythonAPI::GetCurrentTime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string PythonAPI::GenerateUUID() {
    uuid_t uuid;
    char uuid_str[37];

    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);

    return std::string(uuid_str);
}

nlohmann::json PythonAPI::ParseJSON(const std::string& jsonStr) {
    try {
        return nlohmann::json::parse(jsonStr);
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse JSON: {}", e.what());
        return nlohmann::json();
    }
}

std::string PythonAPI::StringifyJSON(const nlohmann::json& json) {
    return json.dump();
}

float PythonAPI::RandomFloat(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min, max);
    return dis(gen);
}

int PythonAPI::RandomInt(int min, int max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

float PythonAPI::Distance(float x1, float y1, float z1, float x2, float y2, float z2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

nlohmann::json PythonAPI::GetConfig(const std::string& key) {
    auto& config = ConfigManager::GetInstance();

    if (config.HasKey(key)) {
        // Try different getter methods
        try {
            return nlohmann::json::parse(config.GetString(key, ""));
        } catch (...) {
            try {
                return nlohmann::json(config.GetInt(key, 0));
            } catch (...) {
                try {
                    return nlohmann::json(config.GetBool(key, false));
                } catch (...) {
                    return nlohmann::json();
                }
            }
        }
    }

    return nlohmann::json();
}

bool PythonAPI::SetConfig(const std::string& key, const nlohmann::json& value) {
    // This would need to be implemented in ConfigManager
    // For now, return false
    return false;
}

} // namespace PythonScripting
