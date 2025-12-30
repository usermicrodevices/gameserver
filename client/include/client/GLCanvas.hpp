// GLCanvas.hpp
#pragma once

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <glad/glad.h>
#include <memory>

#include "../include/client/InputManager.hpp"

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
    void SetInputManager(std::shared_ptr<InputManager> inputManager) {
        inputManager_ = inputManager;
    }

protected:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnEraseBackground(wxEraseEvent& event);

    // Minimal event forwarding - just convert to raw input
    void OnMouseEvents(wxMouseEvent& event);
    void OnKeyEvents(wxKeyEvent& event);
    void OnCharEvents(wxKeyEvent& event);  // For text input

private:
    bool InitOpenGL();
    void SetupGLContext();

    // Convert wxWidgets events to raw input
    Input::Key WxKeyToGameKey(int wxKeyCode) const;
    int WxMouseButtonToGameButton(int wxButton) const;

    wxGLContext* glContext_{nullptr};
    GameClient* gameClient_{nullptr};
    std::shared_ptr<InputManager> inputManager_;

    wxPoint lastMousePos_;
    bool mouseCaptured_{false};

    wxDECLARE_EVENT_TABLE();
};

// GLCanvas.cpp - Key event forwarding example
void GLCanvas::OnKeyEvents(wxKeyEvent& event) {
    if (!inputManager_) return;

    int keyCode = event.GetKeyCode();
    bool pressed = event.GetEventType() == wxEVT_KEY_DOWN;

    // Convert and forward
    Input::Key gameKey = WxKeyToGameKey(keyCode);
    if (gameKey != Input::Key::Count) {
        inputManager_->ProcessRawKeyEvent(static_cast<int>(gameKey), pressed);
    }

    // Skip for processed keys to prevent double handling
    if (keyCode != WXK_TAB && keyCode != WXK_ESCAPE) {
        event.Skip();
    }
}

void GLCanvas::OnCharEvents(wxKeyEvent& event) {
    if (!inputManager_) return;

    // Handle text input for chat
    wxChar ch = event.GetUnicodeKey();
    if (ch >= 32 && ch < 127) {  // Printable ASCII
        std::string text(1, static_cast<char>(ch));
        inputManager_->ProcessRawTextInput(text);
    }
    event.Skip();
}
