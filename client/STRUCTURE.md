gameserver-client/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── ClientApp.h
│   ├── ClientApp.cpp
│   ├── ClientFrame.h
│   ├── ClientFrame.cpp
│   ├── GLCanvas.h
│   ├── GLCanvas.cpp
│   ├── NetworkClient.h
│   ├── NetworkClient.cpp
│   ├── GameWorld.h
│   ├── GameWorld.cpp
│   ├── PythonScriptManager.h
│   └── PythonScriptManager.cpp
├── include/
│   ├── client/
│   │   ├── GameClient.h
│   │   ├── Player.h
│   │   ├── Camera.h
│   │   ├── InputManager.h
│   │   ├── RenderSystem.h
│   │   ├── UIComponents.h
│   │   └── ScriptBindings.h
│   └── python/
│       ├── PythonWrapper.h
│       ├── GameModule.h
│       └── ScriptEvents.h
├── resources/
│   ├── shaders/
│   │   ├── basic.vert
│   │   └── basic.frag
│   ├── textures/
│   └── config/
│       └── client_config.json
└── scripts/
    ├── game_scripts.py
    ├── ui_scripts.py
    └── event_handlers.py
