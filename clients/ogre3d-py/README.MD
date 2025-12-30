# 1. Clone the repository
git clone https://github.com/usermicrodevices/gameserver.git
cd gameserver/clients/ogre3d-py

# 2. Run setup
python setup.py

# 3. Configure the client
# Edit config/client_config.yaml with your server details

# 4. Run the client
python main.py --host 127.0.0.1 --port 8080 --player YourName

# Or use the run script
./run_client.sh --host 127.0.0.1 --port 8080
