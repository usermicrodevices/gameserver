#pragma once

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/listctrl.h>
#include <wx/treectrl.h>

// Custom UI Components for the game client

class ChatControl : public wxPanel {
public:
    ChatControl(wxWindow* parent, wxWindowID id = wxID_ANY);
    
    void AddMessage(const wxString& player, const wxString& message, const wxColour& color = *wxBLACK);
    void AddSystemMessage(const wxString& message);
    void ClearChat();
    
    wxString GetInputText() const;
    void ClearInput();
    
private:
    wxListBox* chatLog_{nullptr};
    wxTextCtrl* inputCtrl_{nullptr};
    
    void OnSendMessage(wxCommandEvent& event);
    wxDECLARE_EVENT_TABLE();
};

class InventoryGrid : public wxPanel {
public:
    InventoryGrid(wxWindow* parent, wxWindowID id = wxID_ANY, int rows = 5, int cols = 8);
    
    void SetItem(int slot, const wxString& itemName, const wxBitmap& icon, int quantity = 1);
    void ClearSlot(int slot);
    void UpdateQuantity(int slot, int quantity);
    
    int GetSelectedSlot() const;
    void SetSelectedSlot(int slot);
    
    // Drag and drop support
    void StartDrag(int slot);
    void DropItem(int targetSlot);
    
private:
    struct Slot {
        wxStaticBitmap* icon{nullptr};
        wxStaticText* label{nullptr};
        wxString itemName;
        int quantity{0};
    };
    
    std::vector<Slot> slots_;
    int rows_{5};
    int cols_{8};
    int selectedSlot_{-1};
    int dragSourceSlot_{-1};
    
    void CreateGrid();
    void OnSlotClick(wxMouseEvent& event);
    void OnSlotRightClick(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    
    wxDECLARE_EVENT_TABLE();
};

class PlayerList : public wxListCtrl {
public:
    PlayerList(wxWindow* parent, wxWindowID id = wxID_ANY);
    
    void AddPlayer(uint64_t playerId, const wxString& name, const wxString& guild = wxEmptyString, int level = 1);
    void RemovePlayer(uint64_t playerId);
    void UpdatePlayer(uint64_t playerId, const wxString& name, const wxString& guild, int level);
    void ClearPlayers();
    
    uint64_t GetSelectedPlayerId() const;
    wxString GetSelectedPlayerName() const;
    
    void SortPlayers(int column, bool ascending = true);
    
private:
    struct PlayerInfo {
        uint64_t id{0};
        wxString name;
        wxString guild;
        int level{1};
        int listIndex{-1};
    };
    
    std::unordered_map<uint64_t, PlayerInfo> players_;
    int sortColumn_{0};
    bool sortAscending_{true};
    
    void OnColumnClick(wxListEvent& event);
    void OnItemActivated(wxListEvent& event);
    void OnContextMenu(wxContextMenuEvent& event);
    
    void RefreshList();
    
    wxDECLARE_EVENT_TABLE();
};

class Minimap : public wxPanel {
public:
    Minimap(wxWindow* parent, wxWindowID id = wxID_ANY, int size = 200);
    
    void SetPlayerPosition(const glm::vec2& position);
    void SetPlayerRotation(float rotation);
    void AddEntity(uint64_t entityId, const glm::vec2& position, const wxColour& color, const wxString& label = wxEmptyString);
    void RemoveEntity(uint64_t entityId);
    void ClearEntities();
    
    void SetWorldBounds(float minX, float minZ, float maxX, float maxZ);
    void SetZoom(float zoom);
    
protected:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseEvents(wxMouseEvent& event);
    
private:
    struct MapEntity {
        glm::vec2 position;
        wxColour color;
        wxString label;
        float size{5.0f};
    };
    
    glm::vec2 playerPosition_{0.0f, 0.0f};
    float playerRotation_{0.0f};
    std::unordered_map<uint64_t, MapEntity> entities_;
    
    float worldMinX_{-100.0f};
    float worldMinZ_{-100.0f};
    float worldMaxX_{100.0f};
    float worldMaxZ_{100.0f};
    float zoom_{1.0f};
    
    wxPoint WorldToScreen(const glm::vec2& worldPos) const;
    glm::vec2 ScreenToWorld(const wxPoint& screenPos) const;
    
    wxDECLARE_EVENT_TABLE();
};