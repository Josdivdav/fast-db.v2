#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>

namespace fastdb {

struct ClientSession {
    std::string socketId;
    int64_t connectedAt{0};
    nlohmann::json user = nullptr;
    nlohmann::json activeDatabase = nullptr;
    bool isWebSocket{false};
    bool isSocketIO{false};
};

class Server {
public:
    explicit Server(int port = 5000, const std::string& publicDir = "");
    ~Server();

    // Start server in background thread
    bool start();

    // Stop server
    void stop();

    // Run blocking event loop
    void run();

    int getPort() const { return port; }
    bool isRunning() const { return running.load(); }

    // Execute command event on database engine
    nlohmann::json handleCommand(const std::string& event,
                                 const nlohmann::json& payload,
                                 ClientSession& session);

    static std::string resolvePublicDir();

private:
    int port{5000};
    std::string publicDir;
    int serverFd{-1};
    std::atomic<bool> running{false};
    std::thread listenThread;

    std::mutex sessionsMutex;
    std::unordered_map<int, ClientSession> sessions;

    void listenLoop();
    void handleClient(int clientFd);

    // Protocol handling
    void serveHttpRequest(int clientFd, const std::string& requestStr);
    void handleWebSocket(int clientFd, ClientSession& session, const std::string& initialRequest);
    void sendHttpResponse(int clientFd, int statusCode, const std::string& contentType, const std::string& body);
    void sendWsFrame(int clientFd, const std::string& text, bool isSocketIO = false);
    void sendTcpJson(int clientFd, const std::string& event, const nlohmann::json& data);
};

} // namespace fastdb
