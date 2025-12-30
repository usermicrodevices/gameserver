#include "ClientFrame.h"
#include "GLCanvas.h"
#include "GameClient.h"
#include <wx/aui/aui.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/aboutdlg.h>
#include <wx/statusbr.h>
#include <wx/toolbar.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/listbox.h>
#include <wx/panel.h>

wxBEGIN_EVENT_TABLE(ClientFrame, wxFrame)
    EVT_MENU(wxID_EXIT, ClientFrame::OnFileExit)
    EVT_MENU(ID_CONNECT, ClientFrame::OnConnectToServer)
    EVT_MENU(ID_DISCONNECT, ClientFrame::OnDisconnect)
    EVT_MENU(ID_SETTINGS, ClientFrame::OnSettings)
    EVT_MENU(ID_FULLSCREEN, ClientFrame::OnToggleFullscreen)
    EVT_MENU(wxID_ABOUT, ClientFrame::OnAbout)
    EVT_CLOSE(ClientFrame::OnClose)
    EVT_TIMER(wxID_ANY, ClientFrame::OnUpdateTimer)
wxEND_EVENT_TABLE()

ClientFrame::ClientFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(1280, 720)),
      updateTimer_(nullptr) {
    
    CreateMenuBar();
    CreateToolBar();
    CreateStatusBar();
    CreateUIComponents();
    
    // Create update timer (60 FPS)
    updateTimer_ = new wxTimer(this, wxID_ANY);
    updateTimer_->Start(16); // ~60 FPS
    
    // Create game client
    gameClient_ = std::make_unique<GameClient>();
}

ClientFrame::~ClientFrame() {
    if (updateTimer_) {
        updateTimer_->Stop();
        delete updateTimer_;
    }
}

void ClientFrame::Initialize() {
    // Center the window
    Centre();
    
    // Initialize OpenGL canvas
    if (glCanvas_) {
        glCanvas_->Initialize();
        glCanvas_->SetGameClient(gameClient_.get());
    }
    
    // Update status bar
    UpdateStatusBar("Ready");
    UpdateFPS(0);
    UpdatePing(0);
}

void ClientFrame::OnClose(wxCloseEvent& event) {
    if (gameClient_ && gameClient_->IsConnected()) {
        wxMessageDialog dialog(this,
            "You are currently connected to a server. Are you sure you want to exit?",
            "Confirm Exit",
            wxYES_NO | wxICON_QUESTION);
        
        if (dialog.ShowModal() != wxID_YES) {
            event.Veto();
            return;
        }
    }
    
    // Stop the update timer
    if (updateTimer_) {
        updateTimer_->Stop();
    }
    
    // Cleanup game client
    if (gameClient_) {
        gameClient_->Shutdown();
    }
    
    event.Skip(); // Allow the window to close
}

void ClientFrame::CreateMenuBar() {
    wxMenuBar* menuBar = new wxMenuBar();
    
    // File menu
    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append(ID_CONNECT, "&Connect\tCtrl+C", "Connect to server");
    fileMenu->Append(ID_DISCONNECT, "&Disconnect\tCtrl+D", "Disconnect from server");
    fileMenu->AppendSeparator();
    fileMenu->Append(ID_SETTINGS, "&Settings\tCtrl+S", "Client settings");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit\tAlt+X", "Exit application");
    
    // View menu
    wxMenu* viewMenu = new wxMenu();
    viewMenu->Append(ID_FULLSCREEN, "&Fullscreen\tF11", "Toggle fullscreen mode");
    viewMenu->AppendCheckItem(wxID_ANY, "Show &Chat", "Show/hide chat window")->Check(true);
    viewMenu->AppendCheckItem(wxID_ANY, "Show &Inventory", "Show/hide inventory")->Check(true);
    viewMenu->AppendCheckItem(wxID_ANY, "Show &Minimap", "Show/hide minimap")->Check(true);
    
    // Help menu
    wxMenu* helpMenu = new wxMenu();
    helpMenu->Append(wxID_ABOUT, "&About\tF1", "About this application");
    
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(viewMenu, "&View");
    menuBar->Append(helpMenu, "&Help");
    
    SetMenuBar(menuBar);
}

void ClientFrame::CreateToolBar() {
    wxToolBar* toolBar = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT);
    
    // Add toolbar buttons
    toolBar->AddTool(ID_CONNECT, "Connect", wxArtProvider::GetBitmap(wxART_GO_FORWARD));
    toolBar->AddTool(ID_DISCONNECT, "Disconnect", wxArtProvider::GetBitmap(wxART_GO_BACK));
    toolBar->AddSeparator();
    toolBar->AddTool(wxID_REFRESH, "Refresh", wxArtProvider::GetBitmap(wxART_REDO));
    toolBar->AddSeparator();
    toolBar->AddTool(wxID_HELP, "Help", wxArtProvider::GetBitmap(wxART_HELP));
    
    toolBar->Realize();
}

void ClientFrame::CreateStatusBar() {
    wxStatusBar* statusBar = CreateStatusBar(4);
    int widths[] = {-1, 100, 100, 150};
    statusBar->SetFieldsCount(4, widths);
    
    statusBar->SetStatusText("Ready", 0);
    statusBar->SetStatusText("FPS: 0", 1);
    statusBar->SetStatusText("Ping: 0ms", 2);
    statusBar->SetStatusText("Position: (0, 0, 0)", 3);
    
    fpsLabel_ = nullptr;
    pingLabel_ = nullptr;
    positionLabel_ = nullptr;
}

void ClientFrame::CreateUIComponents() {
    // Create AUI manager for dockable windows
    wxAuiManager* auiManager = new wxAuiManager(this);
    
    // Create main splitter
    wxSplitterWindow* splitter = new wxSplitterWindow(this, wxID_ANY);
    
    // Left panel (game view)
    wxPanel* gamePanel = new wxPanel(splitter);
    wxBoxSizer* gameSizer = new wxBoxSizer(wxVERTICAL);
    
    // Create OpenGL canvas
    int attribList[] = {
        WX_GL_RGBA,
        WX_GL_DOUBLEBUFFER,
        WX_GL_DEPTH_SIZE, 24,
        WX_GL_STENCIL_SIZE, 8,
        WX_GL_SAMPLE_BUFFERS, 1,
        WX_GL_SAMPLES, 4,
        0
    };
    
    glCanvas_ = new GLCanvas(gamePanel, wxID_ANY, attribList);
    gameSizer->Add(glCanvas_, 1, wxEXPAND);
    gamePanel->SetSizer(gameSizer);
    
    // Right panel (UI controls)
    wxPanel* uiPanel = new wxPanel(splitter);
    wxBoxSizer* uiSizer = new wxBoxSizer(wxVERTICAL);
    
    // Chat control
    wxStaticBoxSizer* chatSizer = new wxStaticBoxSizer(wxVERTICAL, uiPanel, "Chat");
    chatLog_ = new wxListBox(uiPanel, wxID_ANY);
    chatInput_ = new wxTextCtrl(uiPanel, ID_CHAT_INPUT, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    chatSizer->Add(chatLog_, 1, wxEXPAND | wxALL, 5);
    chatSizer->Add(chatInput_, 0, wxEXPAND | wxALL, 5);
    
    // Player list
    wxStaticBoxSizer* playersSizer = new wxStaticBoxSizer(wxVERTICAL, uiPanel, "Players Online");
    playerList_ = new wxListBox(uiPanel, wxID_ANY);
    playersSizer->Add(playerList_, 1, wxEXPAND | wxALL, 5);
    
    // Stats panel
    wxStaticBoxSizer* statsSizer = new wxStaticBoxSizer(wxVERTICAL, uiPanel, "Stats");
    wxGridSizer* statsGrid = new wxGridSizer(2, 5, 5);
    
    statsGrid->Add(new wxStaticText(uiPanel, wxID_ANY, "Health:"), 0, wxALIGN_RIGHT);
    statsGrid->Add(new wxStaticText(uiPanel, wxID_ANY, "100/100"), 0, wxALIGN_LEFT);
    
    statsGrid->Add(new wxStaticText(uiPanel, wxID_ANY, "Mana:"), 0, wxALIGN_RIGHT);
    statsGrid->Add(new wxStaticText(uiPanel, wxID_ANY, "50/50"), 0, wxALIGN_LEFT);
    
    statsGrid->Add(new wxStaticText(uiPanel, wxID_ANY, "Level:"), 0, wxALIGN_RIGHT);
    statsGrid->Add(new wxStaticText(uiPanel, wxID_ANY, "1"), 0, wxALIGN_LEFT);
    
    statsGrid->Add(new wxStaticText(uiPanel, wxID_ANY, "Experience:"), 0, wxALIGN_RIGHT);
    statsGrid->Add(new wxStaticText(uiPanel, wxID_ANY, "0/100"), 0, wxALIGN_LEFT);
    
    statsSizer->Add(statsGrid, 1, wxEXPAND | wxALL, 5);
    
    // Combine UI components
    uiSizer->Add(chatSizer, 2, wxEXPAND | wxALL, 5);
    uiSizer->Add(playersSizer, 1, wxEXPAND | wxALL, 5);
    uiSizer->Add(statsSizer, 0, wxEXPAND | wxALL, 5);
    uiPanel->SetSizer(uiSizer);
    
    // Set splitter proportions
    splitter->SplitVertically(gamePanel, uiPanel, 800);
    
    // Set up AUI manager
    auiManager->AddPane(splitter, wxAuiPaneInfo().CenterPane().Name("MainPane"));
    
    // Add minimap as floating window
    wxPanel* minimapPanel = new wxPanel(this, wxID_ANY);
    minimapPanel->SetSize(200, 200);
    
    auiManager->AddPane(minimapPanel,
        wxAuiPaneInfo()
        .Name("Minimap")
        .Caption("Minimap")
        .Float()
        .FloatingPosition(100, 100)
        .FloatingSize(200, 200)
        .Dockable(false)
        .CloseButton(true)
        .MaximizeButton(true)
        .PinButton(true));
    
    auiManager->Update();
}

void ClientFrame::OnFileExit(wxCommandEvent& event) {
    Close(true);
}

void ClientFrame::OnConnectToServer(wxCommandEvent& event) {
    wxTextEntryDialog dialog(this,
        "Enter server address:",
        "Connect to Server",
        "localhost:12345");
    
    if (dialog.ShowModal() == wxID_OK) {
        wxString input = dialog.GetValue();
        wxString host = input.BeforeFirst(':');
        wxString portStr = input.AfterFirst(':');
        
        long port = 12345;
        if (!portStr.IsEmpty()) {
            portStr.ToLong(&port);
        }
        
        // Connect to server
        wxGetApp().ConnectToServer(host.ToStdString(), port);
        UpdateStatusBar(wxString::Format("Connecting to %s:%ld...", host, port));
    }
}

void ClientFrame::OnDisconnect(wxCommandEvent& event) {
    if (gameClient_ && gameClient_->IsConnected()) {
        gameClient_->Disconnect();
        UpdateStatusBar("Disconnected");
    }
}

void ClientFrame::OnSettings(wxCommandEvent& event) {
    wxMessageBox("Settings dialog would open here.",
                "Settings",
                wxOK | wxICON_INFORMATION,
                this);
}

void ClientFrame::OnToggleFullscreen(wxCommandEvent& event) {
    static bool isFullscreen = false;
    ShowFullScreen(!isFullscreen);
    isFullscreen = !isFullscreen;
}

void ClientFrame::OnAbout(wxCommandEvent& event) {
    wxAboutDialogInfo info;
    info.SetName("3D Game Client");
    info.SetVersion("1.0.0");
    info.SetDescription("A 3D game client with Python scripting support");
    info.SetCopyright("(C) 2024");
    info.SetWebSite("https://github.com/usermicrodevices/gameserver");
    info.AddDeveloper("UserMicroDevices Team");
    
    wxAboutBox(info);
}

void ClientFrame::OnUpdateTimer(wxTimerEvent& event) {
    static int frameCount = 0;
    static wxStopWatch stopWatch;
    
    frameCount++;
    
    // Update FPS every second
    if (stopWatch.Time() >= 1000) {
        int fps = (frameCount * 1000) / stopWatch.Time();
        UpdateFPS(fps);
        frameCount = 0;
        stopWatch.Start();
    }
    
    // Update game client
    if (gameClient_) {
        gameClient_->Update(0.016f); // Fixed timestep for 60 FPS
        
        // Update position in status bar
        if (auto player = gameClient_->GetLocalPlayer()) {
            auto pos = player->GetPosition();
            wxString posStr = wxString::Format("Position: (%.1f, %.1f, %.1f)",
                                             pos.x, pos.y, pos.z);
            GetStatusBar()->SetStatusText(posStr, 3);
        }
    }
    
    // Refresh OpenGL canvas
    if (glCanvas_) {
        glCanvas_->Refresh();
    }
}

void ClientFrame::UpdateStatusBar(const wxString& status) {
    GetStatusBar()->SetStatusText(status, 0);
}

void ClientFrame::UpdateFPS(int fps) {
    GetStatusBar()->SetStatusText(wxString::Format("FPS: %d", fps), 1);
}

void ClientFrame::UpdatePing(int ping) {
    GetStatusBar()->SetStatusText(wxString::Format("Ping: %dms", ping), 2);
}