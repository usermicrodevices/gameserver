#pragma once

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <memory>

class GLCanvas;

class ClientFrame : public wxFrame {
public:
    ClientFrame(const wxString& title);
    ~ClientFrame();
    
    void Initialize();
    void OnClose(wxCloseEvent& event);
    
    // Menu event handlers
    void OnFileExit(wxCommandEvent& event);
    void OnConnectToServer(wxCommandEvent& event);
    void OnDisconnect(wxCommandEvent& event);
    void OnSettings(wxCommandEvent& event);
    void OnToggleFullscreen(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    
    // Timer for game updates
    void OnUpdateTimer(wxTimerEvent& event);
    
    // Status bar updates
    void UpdateStatusBar(const wxString& status);
    void UpdateFPS(int fps);
    void UpdatePing(int ping);
    
private:
    void CreateMenuBar();
    void CreateToolBar();
    void CreateStatusBar();
    void CreateUIComponents();
    
    std::unique_ptr<class GameClient> gameClient_;
    GLCanvas* glCanvas_{nullptr};
    wxTimer* updateTimer_{nullptr};
    
    // UI Components
    wxTextCtrl* chatInput_{nullptr};
    wxListBox* chatLog_{nullptr};
    wxListBox* playerList_{nullptr};
    wxPanel* inventoryPanel_{nullptr};
    wxStaticText* fpsLabel_{nullptr};
    wxStaticText* pingLabel_{nullptr};
    wxStaticText* positionLabel_{nullptr};
    
    // Menu IDs
    enum {
        ID_CONNECT = wxID_HIGHEST + 1,
        ID_DISCONNECT,
        ID_SETTINGS,
        ID_FULLSCREEN,
        ID_CHAT_INPUT
    };
    
    wxDECLARE_EVENT_TABLE();
};