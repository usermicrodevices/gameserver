#pragma once
#include "logger.hpp"
#include "debug.hpp"
#include <cstdint>
#include <vector>
#include <string>
#include <asio.hpp>
#include <stdexcept>
#include <system_error>
#include <memory>

class BinaryProtocol {
private:
	Logger& logger_;

public:
	BinaryProtocol() : logger_(Logger::get_logger("binary_protocol")) {
		DEBUG_LOG(DebugCategory::PROTOCOL, LogLevel::INFO,
				  "BinaryProtocol initialized");
	}

	std::vector<char> serialize(const MessageHeader& header, const void* data) {
		DEBUG_PROFILE_FUNCTION();

		try {
			if (!header.validate()) {
				LOG_ERROR(logger_, "Invalid header for serialization");
				throw BinaryProtocolException("Invalid header",
											  make_error_code(BinaryProtocolError::INVALID_HEADER_SIZE));
			}

			std::vector<char> buffer(sizeof(MessageHeader) + header.body_size);

			// Copy header
			memcpy(buffer.data(), &header, sizeof(MessageHeader));

			// Copy body
			if (data && header.body_size > 0) {
				memcpy(buffer.data() + sizeof(MessageHeader), data, header.body_size);
			}

			LOG_TRACE(logger_, "Serialized message: id=" + std::to_string(header.message_id) +
			", type=" + std::to_string(static_cast<int>(header.type)) +
			", size=" + std::to_string(buffer.size()));

			return buffer;

		} catch (const std::exception& e) {
			LOG_ERROR(logger_, "Serialization failed: " + std::string(e.what()));
			throw;
		}
	}

	MessageHeader deserialize_header(const char* data, size_t size) {
		DEBUG_PROFILE_FUNCTION();

		try {
			if (size < sizeof(MessageHeader)) {
				LOG_ERROR(logger_, "Insufficient data for header deserialization");
				throw BinaryProtocolException("Insufficient data for header",
											  make_error_code(BinaryProtocolError::INVALID_HEADER_SIZE));
			}

			MessageHeader header;
			memcpy(&header, data, sizeof(MessageHeader));

			if (!header.validate()) {
				LOG_WARN(logger_, "Header validation failed during deserialization");
				throw BinaryProtocolException("Header validation failed",
											  make_error_code(BinaryProtocolError::INVALID_HEADER_SIZE));
			}

			if (header.body_size > MessageHeader::MAX_MESSAGE_SIZE) {
				LOG_ERROR(logger_, "Message too large: " + std::to_string(header.body_size));
				throw BinaryProtocolException("Message too large",
											  make_error_code(BinaryProtocolError::MESSAGE_TOO_LARGE));
			}

			LOG_TRACE(logger_, "Deserialized header: id=" + std::to_string(header.message_id) +
			", type=" + std::to_string(static_cast<int>(header.type)) +
			", body_size=" + std::to_string(header.body_size));

			return header;

		} catch (const std::exception& e) {
			LOG_ERROR(logger_, "Header deserialization failed: " + std::string(e.what()));
			throw;
		}
	}

	// Connection class with logging
	class BinaryConnection : public std::enable_shared_from_this<BinaryConnection> {
	private:
		Logger& logger_;
		asio::ip::tcp::socket socket_;
		asio::streambuf buffer_;

	public:
		BinaryConnection(asio::ip::tcp::socket socket)
		: logger_(Logger::get_logger("binary_connection")),
		socket_(std::move(socket)) {

			DEBUG_LOG(DebugCategory::NETWORK, LogLevel::DEBUG,
					  "BinaryConnection created for " +
					  socket_.remote_endpoint().address().to_string());
		}

		void async_read_header(std::function<void(const std::shared_ptr<MessageHeader>&,
												  const std::error_code&)> callback) {
			DEBUG_PROFILE_FUNCTION();

			asio::async_read(socket_, asio::buffer(buffer_.prepare(sizeof(MessageHeader))),
							 [self = shared_from_this(), callback](const std::error_code& ec, size_t bytes) {
								 DEBUG_PROFILE_SCOPE("async_read_header_callback");

								 if (ec) {
									 LOG_ERROR(self->logger_, "Read header failed: " + ec.message());
									 if (callback) callback(nullptr, ec);
									 return;
								 }

								 self->buffer_.commit(bytes);

								 try {
									 const char* data = asio::buffer_cast<const char*>(self->buffer_.data());
									 MessageHeader header = self->deserialize_header(data, self->buffer_.size());
									 self->buffer_.consume(sizeof(MessageHeader));

									 LOG_TRACE(self->logger_, "Successfully read header for message " +
									 std::to_string(header.message_id));

									 if (callback) {
										 callback(std::make_shared<MessageHeader>(header), std::error_code{});
									 }
								 } catch (const std::exception& e) {
									 LOG_ERROR(self->logger_, "Failed to parse header: " + std::string(e.what()));
									 if (callback) {
										 callback(nullptr, std::make_error_code(std::errc::protocol_error));
									 }
								 }
							 });
												  }

												  void async_write(const MessageHeader& header, const std::vector<char>& body,
																   std::function<void(const std::error_code&, size_t)> callback) {
													  DEBUG_PROFILE_FUNCTION();

													  // Combine header and body
													  std::vector<asio::const_buffer> buffers;
													  buffers.push_back(asio::buffer(&header, sizeof(header)));

													  if (!body.empty()) {
														  buffers.push_back(asio::buffer(body));
													  }

													  LOG_TRACE(logger_, "Writing message " + std::to_string(header.message_id) +
													  " (" + std::to_string(sizeof(header) + body.size()) + " bytes)");

													  asio::async_write(socket_, buffers,
																		[self = shared_from_this(), callback, header](const std::error_code& ec, size_t bytes) {
																			DEBUG_PROFILE_SCOPE("async_write_callback");

																			if (ec) {
																				LOG_ERROR(self->logger_,
																						  "Write failed for message " + std::to_string(header.message_id) +
																						  ": " + ec.message());
																			} else {
																				LOG_TRACE(self->logger_,
																						  "Successfully wrote message " + std::to_string(header.message_id) +
																						  " (" + std::to_string(bytes) + " bytes)");

																				// Update metrics
																				DebugSystem::get_instance().increment_metric("bytes_sent", bytes);
																				DebugSystem::get_instance().increment_metric("messages_sent");
																			}

																			if (callback) callback(ec, bytes);
																		});
																   }
	};
};
