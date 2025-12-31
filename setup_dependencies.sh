# Clone the repository
git clone https://github.com/usermicrodevices/gameserver.git
cd gameserver

# Create thirdparty directory
mkdir -p thirdparty

# Clone ASIO (standalone)
git clone https://github.com/chriskohlhoff/asio.git thirdparty/asio

# Clone spdlog
git clone https://github.com/gabime/spdlog.git thirdparty/spdlog

# Clone nlohmann/json
git clone https://github.com/nlohmann/json.git thirdparty/json

# Clone glm
git clone https://github.com/g-truc/glm.git thirdparty/glm