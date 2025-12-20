#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

class GameSession : public std::enable_shared_from_this<GameSession> {
public:
	using Pointer = std::shared_ptr<GameSession>;

	GameSession(asio::ip::tcp::socket socket);
	~GameSession();

	void Start();
	void Stop();

	void Send(const nlohmann::json& message);
	void SendRaw(const std::string& data);

	uint64_t GetSessionId() const { return sessionId_; }
	asio::ip::tcp::endpoint GetRemoteEndpoint() const;

	// Callback setters
	void SetMessageHandler(std::function<void(const nlohmann::json&)> handler);
	void SetCloseHandler(std::function<void()> handler);

private:
	void DoRead();
	void DoWrite();
	void HandleMessage(const std::string& message);

	asio::ip::tcp::socket socket_;
	asio::streambuf readBuffer_;
	std::queue<std::string> writeQueue_;
	std::mutex writeMutex_;

	uint64_t sessionId_;
	static std::atomic<uint64_t> nextSessionId_;

	std::function<void(const nlohmann::json&)> messageHandler_;
	std::function<void()> closeHandler_;

	std::atomic<bool> connected_{false};
	std::atomic<bool> closing_{false};

	// Heartbeat
	asio::steady_timer heartbeatTimer_;
	void StartHeartbeat();
	void CheckHeartbeat();
};
