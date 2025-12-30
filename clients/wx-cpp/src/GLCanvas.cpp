#include "GLCanvas.h"
#include "GameClient.h"
#include <wx/dcclient.h>
#include <iostream>

wxBEGIN_EVENT_TABLE(GLCanvas, wxGLCanvas)
    EVT_PAINT(GLCanvas::OnPaint)
    EVT_SIZE(GLCanvas::OnSize)
    EVT_ERASE_BACKGROUND(GLCanvas::OnEraseBackground)
    EVT_MOUSE_EVENTS(GLCanvas::OnMouseEvents)
    EVT_KEY_DOWN(GLCanvas::OnKeyEvents)
    EVT_KEY_UP(GLCanvas::OnKeyEvents)
wxEND_EVENT_TABLE()

GLCanvas::GLCanvas(wxWindow* parent, wxWindowID id,
                   const int* attribList,
                   const wxPoint& pos,
                   const wxSize& size,
                   long style,
                   const wxString& name)
    : wxGLCanvas(parent, id, attribList, pos, size, style, name),
      glContext_(nullptr),
      gameClient_(nullptr),
      mouseCaptured_(false) {
    
    // Set initial size
    SetMinSize(wxSize(400, 300));
    
    // Bind to parent window for focus
    parent->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        if (gameClient_) {
            SetFocus();
        }
        event.Skip();
    });
}

GLCanvas::~GLCanvas() {
    if (glContext_) {
        delete glContext_;
    }
}

void GLCanvas::Initialize() {
    if (!InitOpenGL()) {
        wxLogError("Failed to initialize OpenGL");
        return;
    }
    
    // Setup initial OpenGL state
    SetupGLContext();
    
    // Set background color
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // Enable face culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Check for errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        wxLogError("OpenGL error after initialization: %d", error);
    }
    
    // Capture mouse for first-person controls
    CaptureMouse();
    mouseCaptured_ = true;
}

void GLCanvas::Render() {
    if (!IsShownOnScreen()) return;
    
    wxPaintDC dc(this);
    SetCurrent(*glContext_);
    
    // Clear buffers
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    
    // Render game world if client exists
    if (gameClient_) {
        gameClient_->Render();
    } else {
        // Render placeholder
        glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Draw a simple triangle as placeholder
        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f);
        glVertex3f(-0.5f, -0.5f, 0.0f);
        glColor3f(0.0f, 1.0f, 0.0f);
        glVertex3f(0.5f, -0.5f, 0.0f);
        glColor3f(0.0f, 0.0f, 1.0f);
        glVertex3f(0.0f, 0.5f, 0.0f);
        glEnd();
    }
    
    // Swap buffers
    SwapBuffers();
}

void GLCanvas::OnPaint(wxPaintEvent& event) {
    Render();
}

void GLCanvas::OnSize(wxSizeEvent& event) {
    event.Skip();
    
    if (!IsShownOnScreen()) return;
    
    wxSize size = event.GetSize();
    int width = size.GetWidth();
    int height = size.GetHeight();
    
    if (width <= 0 || height <= 0) return;
    
    SetCurrent(*glContext_);
    
    // Update viewport
    glViewport(0, 0, width, height);
    
    // Update game client aspect ratio if it exists
    if (gameClient_) {
        // This would update camera projection matrix
    }
    
    // Request redraw
    Refresh();
}

void GLCanvas::OnEraseBackground(wxEraseEvent& event) {
    // Do nothing to avoid flickering
    // OpenGL handles its own background
}

void GLCanvas::OnMouseEvents(wxMouseEvent& event) {
    static wxPoint lastPos;
    wxPoint currentPos = event.GetPosition();
    
    if (event.Entering()) {
        lastPos = currentPos;
    }
    
    if (event.Leaving()) {
        // Handle mouse leaving
    }
    
    if (event.Moving()) {
        // Handle mouse movement
    }
    
    if (event.Dragging()) {
        if (mouseCaptured_ && gameClient_) {
            // Calculate mouse delta
            wxPoint delta = currentPos - lastPos;
            
            // Convert to rotation
            float yawDelta = delta.x * 0.1f;
            float pitchDelta = delta.y * 0.1f;
            
            // Update camera rotation
            // This would be passed to gameClient_->LookAt
        }
        
        lastPos = currentPos;
    }
    
    if (event.LeftDown()) {
        if (!mouseCaptured_) {
            CaptureMouse();
            mouseCaptured_ = true;
        }
        
        // Handle left click
        if (gameClient_) {
            // gameClient_->Interact with something
        }
    }
    
    if (event.RightDown()) {
        // Handle right click
    }
    
    if (event.MiddleDown()) {
        // Handle middle click
    }
    
    if (event.GetWheelRotation() != 0) {
        // Handle mouse wheel
        float zoomDelta = event.GetWheelRotation() > 0 ? 1.0f : -1.0f;
        if (gameClient_) {
            // gameClient_->Zoom(zoomDelta);
        }
    }
    
    event.Skip();
}

void GLCanvas::OnKeyEvents(wxKeyEvent& event) {
    int keyCode = event.GetKeyCode();
    
    if (event.GetEventType() == wxEVT_KEY_DOWN) {
        switch (keyCode) {
            case WXK_ESCAPE:
                if (mouseCaptured_) {
                    ReleaseMouse();
                    mouseCaptured_ = false;
                }
                break;
                
            case 'W':
            case WXK_UP:
                if (gameClient_) {
                    // gameClient_->MovePlayer(glm::vec3(0, 0, -1));
                }
                break;
                
            case 'S':
            case WXK_DOWN:
                if (gameClient_) {
                    // gameClient_->MovePlayer(glm::vec3(0, 0, 1));
                }
                break;
                
            case 'A':
            case WXK_LEFT:
                if (gameClient_) {
                    // gameClient_->MovePlayer(glm::vec3(-1, 0, 0));
                }
                break;
                
            case 'D':
            case WXK_RIGHT:
                if (gameClient_) {
                    // gameClient_->MovePlayer(glm::vec3(1, 0, 0));
                }
                break;
                
            case WXK_SPACE:
                if (gameClient_) {
                    // gameClient_->Jump();
                }
                break;
                
            case 'E':
                // Interact key
                break;
                
            case 'I':
                // Inventory key
                break;
                
            case 'M':
                // Map key
                break;
                
            case WXK_TAB:
                // Scoreboard or menu
                break;
        }
    }
    
    event.Skip();
}

bool GLCanvas::InitOpenGL() {
    if (!glContext_) {
        glContext_ = new wxGLContext(this);
    }
    
    if (!glContext_->IsOK()) {
        wxLogError("Failed to create OpenGL context");
        delete glContext_;
        glContext_ = nullptr;
        return false;
    }
    
    SetCurrent(*glContext_);
    
    // Initialize GLAD for OpenGL function pointers
    if (!gladLoadGL()) {
        wxLogError("Failed to initialize GLAD");
        return false;
    }
    
    // Check OpenGL version
    const GLubyte* version = glGetString(GL_VERSION);
    wxLogMessage("OpenGL Version: %s", version);
    
    // Check for required extensions
    if (!GLAD_GL_VERSION_3_3) {
        wxLogError("OpenGL 3.3 is required");
        return false;
    }
    
    return true;
}

void GLCanvas::SetupGLContext() {
    if (!glContext_) return;
    
    SetCurrent(*glContext_);
    
    // Set default OpenGL state
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0f);
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    
    // Enable smooth shading
    glShadeModel(GL_SMOOTH);
    
    // Nice perspective correction
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    
    // Set polygon mode to fill by default
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}