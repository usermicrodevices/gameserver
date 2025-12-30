#include "ClientApp.h"
#include <wx/wx.h>
#include <memory>

// Application entry point
int main(int argc, char** argv) {
    // Initialize wxWidgets
    wxApp::SetInstance(new ClientApp());
    wxEntryStart(argc, argv);
    
    // Initialize the application
    ClientApp* app = wxDynamicCast(wxApp::GetInstance(), ClientApp);
    if (app && app->Initialize()) {
        wxAppConsole::CallOnInit();
        
        // Run the main loop
        app->OnRun();
        
        // Cleanup
        app->Shutdown();
    }
    
    wxEntryCleanup();
    return 0;
}