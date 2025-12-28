#pragma once

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <glad/glad.h>
#include <memory>

class GLCanvas : public wxGLCanvas {
public:
    GLCanvas(wxWindow* parent, wxWindowID id = wxID_ANY,
             const int* attribList = nullptr,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxString& name = "GLCanvas");
    ~GLCanvas();
    
    void Initialize();
    void Render();
    
    void SetGameClient(class GameClient* client) { gameClient_ = client; }
    
protected:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    void OnMouseEvents(wxMouseEvent& event);
    void OnKeyEvents(wxKeyEvent& event);
    
private:
    bool InitOpenGL();
    void SetupGLContext();
    
    wxGLContext* glContext_{nullptr};
    GameClient* gameClient_{nullptr};
    
    wxPoint lastMousePos_;
    bool mouseCaptured_{false};
    
    wxDECLARE_EVENT_TABLE();
};