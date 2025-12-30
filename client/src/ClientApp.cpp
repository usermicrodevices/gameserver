#include <iostream>
#include <wx/config.h>
#include <wx/stdpaths.h>
#include <wx/log.h>

#include "../include/client/ClientApp.hpp"
#include "../include/client/ClientFrame.hpp"

wxIMPLEMENT_APP(ClientApp);

ClientApp::ClientApp() : gameClient_(nullptr), mainFrame_(nullptr) {
}

ClientApp::~ClientApp() {
}

bool ClientApp::Initialize() {
    // Set application name
    SetAppName("GameServerClient");
    SetAppDisplayName("3D Game Client");
    
    // Initialize logging
    wxLog::SetActiveTarget(new wxLogStderr());
    wxLog::SetLogLevel(wxLOG_Message);
    
    // Load configuration
    if (!LoadConfig()) {
        std::cerr << "Failed to load configuration" << std::endl;
        return false;
    }
    
    // Initialize Python scripting
    if (!InitializeScripting()) {
        std::cerr << "Warning: Python scripting initialization failed" << std::endl;
    }
    
    return true;
}

bool ClientApp::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }
    
    // Create and show main frame
    mainFrame_ = new ClientFrame("3D Game Client");
    mainFrame_->Initialize();
    mainFrame_->Show(true);
    SetTopWindow(mainFrame_);
    
    return true;
}

int ClientApp::OnExit() {
    Shutdown();
    return wxApp::OnExit();
}

void ClientApp::Shutdown() {
    if (gameClient_) {
        gameClient_->Shutdown();
        gameClient_.reset();
    }
    
    if (scriptManager_) {
        scriptManager_->Shutdown();
        scriptManager_.reset();
    }
    
    SaveConfig();
}

bool ClientApp::LoadConfig() {
    wxConfigBase* config = wxConfigBase::Get();
    if (!config) {
        config = new wxConfig("GameClient");
        wxConfigBase::Set(config);
    }
    
    config_ = ClientConfig();
    
    config_.username = config->Read("/User/Username", "Player").ToStdString();
    config_.mouseSensitivity = config->ReadDouble("/Controls/MouseSensitivity", 0.1f);
    config_.movementSpeed = config->ReadDouble("/Controls/MovementSpeed", 5.0f);
    config_.renderDistance = config->ReadDouble("/Graphics/RenderDistance", 500.0f);
    config_.vsync = config->ReadBool("/Graphics/VSync", true);
    config_.fullscreen = config->ReadBool("/Graphics/Fullscreen", false);
    config_.windowWidth = config->ReadLong("/Graphics/Width", 1280);
    config_.windowHeight = config->ReadLong("/Graphics/Height", 720);
    
    // Server connection
    lastServer_ = config->Read("/Connection/LastServer", "localhost").ToStdString();
    lastPort_ = config->ReadLong("/Connection/LastPort", 12345);
    
    return true;
}

bool ClientApp::SaveConfig() {
    wxConfigBase* config = wxConfigBase::Get();
    if (!config) return false;
    
    config->Write("/User/Username", wxString(config_.username));
    config->WriteDouble("/Controls/MouseSensitivity", config_.mouseSensitivity);
    config->WriteDouble("/Controls/MovementSpeed", config_.movementSpeed);
    config->WriteDouble("/Graphics/RenderDistance", config_.renderDistance);
    config->WriteBool("/Graphics/VSync", config_.vsync);
    config->WriteBool("/Graphics/Fullscreen", config_.fullscreen);
    config->WriteLong("/Graphics/Width", config_.windowWidth);
    config->WriteLong("/Graphics/Height", config_.windowHeight);
    
    config->Write("/Connection/LastServer", wxString(lastServer_));
    config->WriteLong("/Connection/LastPort", lastPort_);
    
    config->Flush();
    return true;
}

bool ClientApp::InitializeScripting() {
    scriptManager_ = std::make_unique<PythonScriptManager>();
    if (!scriptManager_->Initialize()) {
        return false;
    }
    
    // Load default scripts
    wxString scriptPath = wxStandardPaths::Get().GetDataDir() + "/scripts";
    scriptManager_->LoadScript("game", (scriptPath + "/game_scripts.py").ToStdString());
    scriptManager_->LoadScript("ui", (scriptPath + "/ui_scripts.py").ToStdString());
    
    return true;
}

void ClientApp::ConnectToServer(const std::string& host, uint16_t port) {
    if (!gameClient_) {
        gameClient_ = std::make_unique<GameClient>();
    }
    
    if (gameClient_->Initialize(host, port)) {
        lastServer_ = host;
        lastPort_ = port;
        
        // Trigger connection event in Python
        if (scriptManager_) {
            nlohmann::json eventData;
            eventData["host"] = host;
            eventData["port"] = port;
            scriptManager_->TriggerEvent("client_connecting", eventData);
        }
    }
}

void ClientApp::DisconnectFromServer() {
    if (gameClient_) {
        gameClient_->Disconnect();
        
        // Trigger disconnection event in Python
        if (scriptManager_) {
            scriptManager_->TriggerEvent("client_disconnected", nlohmann::json());
        }
    }
}

bool ClientApp::IsConnected() const {
    return gameClient_ && gameClient_->IsConnected();
}
