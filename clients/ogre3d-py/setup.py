#!/usr/bin/env python3
"""
Setup script for Ogre3D Game Client
"""

import os
import sys
import subprocess
import platform
from pathlib import Path

def check_python_version():
    """Check Python version"""
    if sys.version_info < (3, 8):
        print("Python 3.8 or higher is required")
        return False
    return True

def install_dependencies():
    """Install required Python packages"""
    print("Installing dependencies...")

    requirements = [
        "kivy==2.3.0",
        "kivy-garden",
        "ogre-python==1.12.6",
        "python-ogre==1.12.6",
        "pyyaml==6.0.1",
        "websocket-client==1.7.0",
        "msgpack==1.0.7",
        "numpy==1.26.4",
        "pillow==10.2.0",
        "pyopengl==3.1.7"
    ]

    for req in requirements:
        try:
            subprocess.check_call([sys.executable, "-m", "pip", "install", req])
        except subprocess.CalledProcessError as e:
            print(f"Failed to install {req}: {e}")
            return False

    return True

def setup_directories():
    """Create necessary directories"""
    directories = [
        "assets/models",
        "assets/textures",
        "assets/shaders",
        "assets/ui",
        "config",
        "logs",
        "saves"
    ]

    for directory in directories:
        Path(directory).mkdir(parents=True, exist_ok=True)
        print(f"Created directory: {directory}")

    return True

def setup_ogre():
    """Setup Ogre3D resources"""
    # Create basic material file
    materials = """material BasicMaterial
{
    technique
    {
        pass
        {
            ambient 0.5 0.5 0.5 1.0
            diffuse 0.8 0.8 0.8 1.0
            specular 0.2 0.2 0.2 1.0
        }
    }
}

material TerrainMaterial
{
    technique
    {
        pass
        {
            texture_unit
            {
                texture grass.jpg
            }
        }
    }
}
"""

    materials_path = Path("assets/materials")
    materials_path.mkdir(exist_ok=True)

    with open(materials_path / "Basic.material", "w") as f:
        f.write(materials)

    print("Created basic materials")
    return True

def create_sample_assets():
    """Create sample assets for testing"""
    # Create placeholder texture
    from PIL import Image
    import numpy as np

    # Create checkerboard texture
    size = 256
    checker = np.zeros((size, size, 3), dtype=np.uint8)
    for i in range(size):
        for j in range(size):
            if (i // 32 + j // 32) % 2 == 0:
                checker[i, j] = [100, 100, 100]
            else:
                checker[i, j] = [150, 150, 150]

    texture_path = Path("assets/textures")
    texture_path.mkdir(exist_ok=True)

    img = Image.fromarray(checker, 'RGB')
    img.save(texture_path / "placeholder.png")

    print("Created placeholder texture")
    return True

def check_system_requirements():
    """Check system requirements"""
    system = platform.system()

    print(f"System: {system}")
    print(f"Architecture: {platform.machine()}")
    print(f"Python: {platform.python_version()}")

    if system == "Windows":
        print("Windows detected - ensure Visual C++ Redistributables are installed")
    elif system == "Linux":
        print("Linux detected - ensure OpenGL drivers are installed")
    elif system == "Darwin":
        print("macOS detected - ensure XQuartz is installed for X11 support")

    return True

def main():
    """Main setup function"""
    print("=" * 50)
    print("Ogre3D Game Client Setup")
    print("=" * 50)

    # Check Python version
    if not check_python_version():
        return 1

    # Check system
    check_system_requirements()

    # Setup directories
    if not setup_directories():
        return 1

    # Install dependencies
    if not install_dependencies():
        print("Warning: Some dependencies may not have installed correctly")

    # Setup Ogre3D
    setup_ogre()

    # Create sample assets
    create_sample_assets()

    print("\n" + "=" * 50)
    print("Setup complete!")
    print("Next steps:")
    print("1. Edit config/client_config.yaml")
    print("2. Run: python main.py")
    print("3. Connect to your game server")
    print("=" * 50)

    return 0

if __name__ == "__main__":
    sys.exit(main())
