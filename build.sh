#!/bin/bash
# build.sh

# Clone dependencies
mkdir -p thirdparty
cd thirdparty
git clone https://github.com/chriskohlhoff/asio.git
git clone https://github.com/nlohmann/json.git
git clone https://github.com/gabime/spdlog.git
git clone https://github.com/g-truc/glm.git
cd ..

# Install system dependencies
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libpq-dev \
    python3-dev \
    libssl-dev \
    zlib1g-dev \
    postgresql-15 \
    postgresql-15-citus-12

# Build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "Build complete!"