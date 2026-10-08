#include "fastdb/server.hpp"
#include "fastdb/user.hpp"
#include "fastdb/database.hpp"
#include "fastdb/crypto.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace fs = std::filesystem;

namespace fastdb {

static const std::unordered_map<std::string, std::string> MIME_TYPES = {
    {".html", "text/html; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".js", "application/javascript; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".svg", "image/svg+xml"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".ico", "image/x-icon"}
};

std::string Server::resolvePublicDir() {
    const char* env = std::getenv("FASTDB_PUBLIC_DIR");
    if (env && *env) return std::string(env);
    if (fs::exists("./public")) return "./public";
    if (fs::exists("../public")) return "../public";
    return "./public";
}

Server::Server(int port, const std::string& publicDir)
    : port(port), publicDir(publicDir.empty() ? resolvePublicDir() : publicDir) {}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (running.load()) return true;

    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::cerr << "[FastDB Server] Failed to create socket\n";
        return false;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[FastDB Server] Failed to bind to port " << port << "\n";
        close(serverFd);
        serverFd = -1;
        return false;
    }

    if (listen(serverFd, 128) < 0) {
        std::cerr << "[FastDB Server] Failed to listen on socket\n";
        close(serverFd);
        serverFd = -1;
        return false;
    }

    running.store(true);
    listenThread = std::thread(&Server::listenLoop, this);
    std::cout << "[FastDB Server] FastDB C++ server listening on port " << port << std::endl;
    return true;
}

void Server::stop() {
    if (!running.exchange(false)) return;

    if (serverFd >= 0) {
        shutdown(serverFd, SHUT_RDWR);
        close(serverFd);
        serverFd = -1;
    }

    if (listenThread.joinable()) {
        listenThread.join();
    }

    std::lock_guard<std::mutex> lock(sessionsMutex);
    for (auto& [fd, session] : sessions) {
        close(fd);
    }
    sessions.clear();
}

void Server::run() {
    if (!running.load()) {
        if (!start()) return;
    }
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void Server::listenLoop() {
    while (running.load()) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (!running.load()) break;
            continue;
        }

        std::string sid = crypto::random_uuid();
        {
            std::lock_guard<std::mutex> lock(sessionsMutex);
            sessions[clientFd] = ClientSession{
                .socketId = sid,
                .connectedAt = crypto::current_time_ms(),
                .user = nullptr,
                .activeDatabase = nullptr,
                .isWebSocket = false,
                .isSocketIO = false
            };
        }

        std::thread([this, clientFd]() {
            this->handleClient(clientFd);
        }).detach();
    }
}

nlohmann::json Server::handleCommand(const std::string& event,
                                     const nlohmann::json& payload,
                                     ClientSession& session) {
    std::string reqId = payload.value("_reqId", "");

    auto makeReply = [&](nlohmann::json data) {
        if (!reqId.empty()) {
            data["_reqId"] = reqId;
        }
        return data;
    };

    auto makeError = [&](const std::string& errMsg) {
        nlohmann::json err = {{"error", errMsg}};
        if (!reqId.empty()) err["_reqId"] = reqId;
        return err;
    };

    try {
        if (event == "user:register") {
            std::string name = payload.value("name", "");
            std::string email = payload.value("email", "");
            std::string password = payload.value("password", "");
            if (name.empty() || email.empty() || password.empty()) {
                return makeError("email, password and name are required");
            }
            CreateUser user;
            user.name(name).email(email).password(password);
            auto feedback = user.save();
            std::string uid = user.getId();
            nlohmann::json userRecord = feedback.contains(uid) ? feedback[uid] : nlohmann::json{
                {"uid", uid}, {"name", name}, {"email", email}
            };
            feedback["uid"] = uid;
            feedback["user"] = userRecord;
            return makeReply(feedback);
        }

        if (event == "user:login") {
            std::string email = payload.value("email", "");
            std::string password = payload.value("password", "");
            if (email.empty() || password.empty()) {
                return makeError("email and password are required");
            }
            CreateUser user;
            auto result = user.login(email, password);
            session.user = result["user"];
            return makeReply(result);
        }

        if (event == "user:find") {
            std::string email = payload.value("email", "");
            std::string uid = payload.value("uid", "");
            CreateUser user;
            nlohmann::json foundUser;
            if (!email.empty()) {
                foundUser = user.find(email);
            } else if (!uid.empty()) {
                foundUser = user.findById(uid);
            }
            if (foundUser.is_null()) {
                return makeError("User not found");
            }
            return makeReply({{"user", foundUser}});
        }

        if (event == "user:delete") {
            std::string uid = payload.value("uid", "");
            CreateUser user;
            user.deleteUser(uid);
            return makeReply({{"success", true}, {"uid", uid}});
        }

        if (event == "database:create") {
            std::string userId = payload.value("userId", "");
            std::string name = payload.value("name", "");
            if (userId.empty()) {
                return makeError("userId is required");
            }
            CreateDB db(userId);
            if (!name.empty()) db.name(name);
            auto res = db.create();
            return makeReply(res);
        }

        if (event == "database:list") {
            std::string userId = payload.value("userId", "");
            if (userId.empty()) {
                return makeError("userId is required");
            }
            CreateDB db(userId);
            auto list = db.list();
            return makeReply({{"databases", list}});
        }

        if (event == "database:get") {
            std::string userId = payload.value("userId", "");
            std::string databaseId = payload.value("databaseId", "");
            if (userId.empty() || databaseId.empty()) {
                return makeError("userId and databaseId are required");
            }
            CreateDB db(userId);
            auto info = db.get(databaseId);
            if (info.is_null()) {
                return makeError("Database not found");
            }
            return makeReply({{"database", info}});
        }

        if (event == "database:delete") {
            std::string userId = payload.value("userId", "");
            std::string databaseId = payload.value("databaseId", "");
            if (userId.empty() || databaseId.empty()) {
                return makeError("userId and databaseId are required");
            }
            CreateDB db(userId);
            bool ok = db.deleteDb(databaseId);
            return makeReply({{"success", ok}, {"databaseId", databaseId}});
        }

        if (event == "database:connect") {
            std::string databaseId = payload.value("databaseId", "");
            std::string userEmail = payload.value("userEmail", "");
            std::string author = payload.value("author", "");
            std::string apiKey = payload.value("apiKey", "");

            Database db(databaseId, userEmail, author, apiKey);
            bool ok = db.connect();
            if (ok) {
                session.activeDatabase = {
                    {"databaseId", databaseId},
                    {"userEmail", userEmail},
                    {"author", author},
                    {"apiKey", apiKey}
                };
                return makeReply({
                    {"success", true},
                    {"message", "Connected to database"},
                    {"databaseId", databaseId}
                });
            } else {
                return makeError("Connection failed");
            }
        }

        if (event == "database:disconnect") {
            session.activeDatabase = nullptr;
            return makeReply({{"success", true}, {"message", "Disconnected"}});
        }

        // Helper to resolve creds
        auto resolveCreds = [&]() {
            nlohmann::json active = session.activeDatabase.is_object() ? session.activeDatabase : nlohmann::json::object();
            std::string dbId = payload.value("databaseId", active.value("databaseId", ""));
            std::string author = payload.value("author", active.value("author", ""));
            std::string apiKey = payload.value("apiKey", active.value("apiKey", ""));
            std::string email = payload.value("userEmail", active.value("userEmail", "user@fastdb.com"));
            return Database(dbId, email, author, apiKey);
        };

        if (event == "collection:list") {
            auto db = resolveCreds();
            db.connect();
            auto list = db.listCollections();
            return makeReply({{"collections", list}});
        }

        if (event == "collection:create") {
            std::string name = payload.value("name", "");
            auto db = resolveCreds();
            db.connect();
            auto res = db.createCollection(name);
            return makeReply(res);
        }

        if (event == "collection:delete") {
            std::string name = payload.value("name", "");
            auto db = resolveCreds();
            db.connect();
            auto res = db.deleteCollection(name);
            return makeReply(res);
        }

        if (event == "document:set") {
            std::string collection = payload.value("collection", "");
            std::string customId = payload.value("id", "");
            nlohmann::json data = payload.value("data", nlohmann::json::object());
            auto db = resolveCreds();
            db.connect();
            auto result = db.collection(collection).set(data, customId);
            nlohmann::json reply = result;
            reply["data"] = result;
            return makeReply(reply);
        }

        if (event == "document:read") {
            std::string collection = payload.value("collection", "");
            nlohmann::json query = payload.contains("query") ? payload["query"] : nullptr;
            auto db = resolveCreds();
            db.connect();
            auto result = db.collection(collection).read(query);
            return makeReply({{"data", result}});
        }

        if (event == "document:get") {
            std::string collection = payload.value("collection", "");
            std::string id = payload.value("id", "");
            auto db = resolveCreds();
            db.connect();
            auto result = db.collection(collection).id(id).read();
            return makeReply({{"data", result}});
        }

        if (event == "document:update") {
            std::string collection = payload.value("collection", "");
            std::string id = payload.value("id", "");
            nlohmann::json data = payload.value("data", nlohmann::json::object());
            auto db = resolveCreds();
            db.connect();
            auto result = db.collection(collection).id(id).update(data);
            nlohmann::json reply = result;
            reply["data"] = result;
            return makeReply(reply);
        }

        if (event == "document:deleteField") {
            std::string collection = payload.value("collection", "");
            std::string id = payload.value("id", "");
            std::string fieldName = payload.value("fieldName", "");
            auto db = resolveCreds();
            db.connect();
            auto result = db.collection(collection).id(id).deleteField(fieldName);
            return makeReply({{"data", result}, {"success", true}});
        }

        if (event == "document:delete") {
            std::string collection = payload.value("collection", "");
            std::string id = payload.value("id", "");
            nlohmann::json query = payload.contains("query") ? payload["query"] : nullptr;
            auto db = resolveCreds();
            db.connect();
            if (!id.empty()) {
                bool ok = db.collection(collection).id(id).deleteDoc();
                return makeReply({{"success", true}, {"result", ok}});
            } else {
                auto result = db.collection(collection).deleteQuery(query);
                return makeReply({{"success", true}, {"result", result}});
            }
        }

        return makeError("Unknown event: " + event);
    } catch (const std::exception& e) {
        return makeError(e.what());
    }
}

void Server::sendHttpResponse(int clientFd, int statusCode, const std::string& contentType, const std::string& body) {
    std::string statusText = (statusCode == 200) ? "OK" :
                             (statusCode == 204) ? "No Content" :
                             (statusCode == 404) ? "Not Found" : "Error";

    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    oss << "Server: FastDB-CPP\r\n";
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    oss << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    if (!contentType.empty()) {
        oss << "Content-Type: " << contentType << "\r\n";
    }
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n\r\n";
    oss << body;

    std::string full = oss.str();
    send(clientFd, full.data(), full.size(), 0);
}

void Server::serveHttpRequest(int clientFd, const std::string& reqStr) {
    std::istringstream stream(reqStr);
    std::string method, uri, version;
    stream >> method >> uri >> version;

    if (method == "OPTIONS") {
        sendHttpResponse(clientFd, 204, "", "");
        return;
    }

    // Serve Socket.IO Client Library
    if (uri.rfind("/socket.io/socket.io", 0) == 0) {
        std::vector<std::string> paths = {
            "./public/socket.io/socket.io.js",
            "../public/socket.io/socket.io.js",
            "./public/socket.io.js",
            "../public/socket.io.js",
            "./node_modules/socket.io/client-dist/socket.io.min.js",
            "../node_modules/socket.io/client-dist/socket.io.min.js",
            "./node_modules/socket.io-client/dist/socket.io.min.js",
            "../node_modules/socket.io-client/dist/socket.io.min.js"
        };
        for (const auto& p : paths) {
            if (fs::exists(p)) {
                std::ifstream ifs(p, std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                sendHttpResponse(clientFd, 200, "application/javascript; charset=utf-8", content);
                return;
            }
        }
    }

    // Engine.IO / Socket.IO Polling
    if (uri.rfind("/socket.io/", 0) == 0 && uri.find("transport=polling") != std::string::npos) {
        bool hasSid = (uri.find("sid=") != std::string::npos);
        if (method == "GET") {
            if (!hasSid) {
                std::string sid = crypto::random_uuid();
                nlohmann::json handshake = {
                    {"sid", sid},
                    {"upgrades", nlohmann::json::array({"websocket"})},
                    {"pingInterval", 25000},
                    {"pingTimeout", 20000},
                    {"maxPayload", 1000000}
                };
                std::string payload = "0" + handshake.dump();
                sendHttpResponse(clientFd, 200, "text/plain; charset=UTF-8", payload);
                return;
            } else {
                size_t sidPos = uri.find("sid=");
                std::string sid = uri.substr(sidPos + 4);
                size_t ampPos = sid.find('&');
                if (ampPos != std::string::npos) sid = sid.substr(0, ampPos);

                nlohmann::json conn = {{"sid", sid}};
                std::string payload = "40" + conn.dump();
                sendHttpResponse(clientFd, 200, "text/plain; charset=UTF-8", payload);
                return;
            }
        } else if (method == "POST") {
            size_t bodyPos = reqStr.find("\r\n\r\n");
            std::string body = (bodyPos != std::string::npos) ? reqStr.substr(bodyPos + 4) : "";
            if (body.rfind("42", 0) == 0) {
                try {
                    auto arr = nlohmann::json::parse(body.substr(2));
                    if (arr.is_array() && arr.size() >= 2) {
                        std::string event = arr[0].get<std::string>();
                        nlohmann::json eventPayload = arr[1];
                        ClientSession session;
                        auto reply = handleCommand(event, eventPayload, session);
                        nlohmann::json outArr = nlohmann::json::array({event, reply});
                        sendHttpResponse(clientFd, 200, "text/plain; charset=UTF-8", "42" + outArr.dump());
                        return;
                    }
                } catch (...) {}
            }
            sendHttpResponse(clientFd, 200, "text/plain; charset=UTF-8", "ok");
            return;
        }
    }

    // HTTP REST / RPC POST endpoint
    if (method == "POST" && uri == "/api") {
        size_t bodyPos = reqStr.find("\r\n\r\n");
        std::string body = (bodyPos != std::string::npos) ? reqStr.substr(bodyPos + 4) : "";
        ClientSession dummy;
        try {
            auto j = nlohmann::json::parse(body);
            std::string event = j.value("event", "");
            nlohmann::json payload = j.contains("data") ? j["data"] : j;
            auto reply = handleCommand(event, payload, dummy);
            sendHttpResponse(clientFd, 200, "application/json; charset=utf-8", reply.dump());
        } catch (const std::exception& e) {
            nlohmann::json err = {{"error", e.what()}};
            sendHttpResponse(clientFd, 400, "application/json; charset=utf-8", err.dump());
        }
        return;
    }

    // Static File Serving
    std::string cleanPath = uri;
    size_t qpos = cleanPath.find('?');
    if (qpos != std::string::npos) cleanPath = cleanPath.substr(0, qpos);
    if (cleanPath == "/" || cleanPath.empty()) cleanPath = "/index.html";

    // Prevent directory traversal
    while (cleanPath.rfind("/../", 0) != std::string::npos || cleanPath.find("../") != std::string::npos) {
        size_t pos = cleanPath.find("../");
        cleanPath.erase(pos, 3);
    }

    std::string filePath = publicDir + cleanPath;
    if (fs::exists(filePath) && fs::is_regular_file(filePath)) {
        std::string ext = fs::path(filePath).extension().string();
        std::string mime = MIME_TYPES.contains(ext) ? MIME_TYPES.at(ext) : "application/octet-stream";
        std::ifstream ifs(filePath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        sendHttpResponse(clientFd, 200, mime, content);
        return;
    }

    // SPA fallback
    std::string indexFallback = publicDir + "/index.html";
    if (fs::exists(indexFallback) && fs::is_regular_file(indexFallback)) {
        std::ifstream ifs(indexFallback);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        sendHttpResponse(clientFd, 200, "text/html; charset=utf-8", content);
        return;
    }

    sendHttpResponse(clientFd, 404, "application/json", "{\"error\":\"Not Found\"}");
}

void Server::sendWsFrame(int clientFd, const std::string& text, bool isSocketIO) {
    std::string payload = text;
    if (isSocketIO && !text.rfind("42", 0) == 0 && !text.rfind("0", 0) == 0 && !text.rfind("40", 0) == 0) {
        payload = "42" + text;
    }

    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + opcode text

    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    frame.insert(frame.end(), payload.begin(), payload.end());
    send(clientFd, frame.data(), frame.size(), 0);
}

void Server::handleWebSocket(int clientFd, ClientSession& session, const std::string& initialRequest) {
    // Extract Sec-WebSocket-Key
    std::string keyHeader = "Sec-WebSocket-Key: ";
    size_t keyPos = initialRequest.find(keyHeader);
    if (keyPos == std::string::npos) {
        keyHeader = "sec-websocket-key: ";
        keyPos = initialRequest.find(keyHeader);
    }
    if (keyPos == std::string::npos) return;

    size_t endPos = initialRequest.find("\r\n", keyPos);
    std::string wsKey = initialRequest.substr(keyPos + keyHeader.size(), endPos - (keyPos + keyHeader.size()));
    while (!wsKey.empty() && wsKey.back() == ' ') wsKey.pop_back();

    std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string acceptVal = crypto::sha1_base64(wsKey + magic);

    std::ostringstream oss;
    oss << "HTTP/1.1 101 Switching Protocols\r\n";
    oss << "Upgrade: websocket\r\n";
    oss << "Connection: Upgrade\r\n";
    oss << "Sec-WebSocket-Accept: " << acceptVal << "\r\n\r\n";
    std::string handshake = oss.str();
    send(clientFd, handshake.data(), handshake.size(), 0);

    bool isSocketIO = (initialRequest.find("/socket.io/") != std::string::npos);
    session.isWebSocket = true;
    session.isSocketIO = isSocketIO;

    if (isSocketIO) {
        // Send Engine.IO open packet: 0{"sid":"...","upgrades":[],"pingInterval":25000,"pingTimeout":20000,"maxPayload":1000000}
        nlohmann::json openPacket = {
            {"sid", session.socketId},
            {"upgrades", nlohmann::json::array()},
            {"pingInterval", 25000},
            {"pingTimeout", 20000},
            {"maxPayload", 1000000}
        };
        sendWsFrame(clientFd, "0" + openPacket.dump(), false);
    }

    // Read WebSocket frames
    std::vector<uint8_t> buffer;
    uint8_t readBuf[4096];

    while (running.load()) {
        ssize_t n = recv(clientFd, readBuf, sizeof(readBuf), 0);
        if (n <= 0) break;
        buffer.insert(buffer.end(), readBuf, readBuf + n);

        while (buffer.size() >= 2) {
            uint8_t b1 = buffer[0];
            uint8_t b2 = buffer[1];
            uint8_t opcode = b1 & 0x0F;
            bool masked = (b2 & 0x80) != 0;
            uint64_t payloadLen = b2 & 0x7F;
            size_t headerLen = 2;

            if (payloadLen == 126) {
                if (buffer.size() < 4) break;
                payloadLen = (buffer[2] << 8) | buffer[3];
                headerLen = 4;
            } else if (payloadLen == 127) {
                if (buffer.size() < 10) break;
                payloadLen = 0;
                for (int i = 0; i < 8; ++i) {
                    payloadLen = (payloadLen << 8) | buffer[2 + i];
                }
                headerLen = 10;
            }

            size_t maskKeyLen = masked ? 4 : 0;
            if (buffer.size() < headerLen + maskKeyLen + payloadLen) {
                break; // Wait for full frame
            }

            uint8_t mask[4] = {0, 0, 0, 0};
            if (masked) {
                std::memcpy(mask, buffer.data() + headerLen, 4);
            }

            std::string payloadText;
            payloadText.resize(payloadLen);
            const uint8_t* pdata = buffer.data() + headerLen + maskKeyLen;
            for (size_t i = 0; i < payloadLen; ++i) {
                payloadText[i] = pdata[i] ^ (masked ? mask[i % 4] : 0);
            }

            buffer.erase(buffer.begin(), buffer.begin() + headerLen + maskKeyLen + payloadLen);

            if (opcode == 0x08) {
                // Close frame
                return;
            } else if (opcode == 0x09) {
                // Ping frame -> send Pong
                std::vector<uint8_t> pong = {0x8A, 0x00};
                send(clientFd, pong.data(), pong.size(), 0);
                continue;
            }

            // Process text frame
            if (isSocketIO) {
                if (payloadText == "2probe") {
                    // Engine.IO probe packet -> respond 3probe
                    sendWsFrame(clientFd, "3probe", false);
                } else if (payloadText == "2") {
                    // Engine.IO ping -> respond pong
                    sendWsFrame(clientFd, "3", false);
                } else if (payloadText == "5") {
                    // Engine.IO upgrade confirmed
                    // no-op
                } else if (payloadText.rfind("40", 0) == 0) {
                    // Socket.IO CONNECT -> respond 40{"sid":"..."}
                    nlohmann::json conn = {{"sid", session.socketId}};
                    sendWsFrame(clientFd, "40" + conn.dump(), false);
                } else if (payloadText.rfind("42", 0) == 0) {
                    // Socket.IO EVENT: 42["event", {...}]
                    try {
                        std::string jsonPart = payloadText.substr(2);
                        auto arr = nlohmann::json::parse(jsonPart);
                        if (arr.is_array() && arr.size() >= 2) {
                            std::string event = arr[0].get<std::string>();
                            nlohmann::json eventPayload = arr[1];
                            auto reply = handleCommand(event, eventPayload, session);
                            nlohmann::json outArr = nlohmann::json::array({event, reply});
                            sendWsFrame(clientFd, "42" + outArr.dump(), false);
                        }
                    } catch (...) {}
                }
            } else {
                // Raw WebSocket JSON frame
                try {
                    auto j = nlohmann::json::parse(payloadText);
                    std::string event = j.value("event", "");
                    nlohmann::json eventData = j.contains("data") ? j["data"] : j;
                    auto reply = handleCommand(event, eventData, session);
                    nlohmann::json responseMsg = {{"event", event}, {"data", reply}};
                    sendWsFrame(clientFd, responseMsg.dump(), false);
                } catch (...) {}
            }
        }
    }
}

void Server::sendTcpJson(int clientFd, const std::string& event, const nlohmann::json& data) {
    nlohmann::json msg = {{"event", event}, {"data", data}};
    std::string str = msg.dump() + "\n";
    send(clientFd, str.data(), str.size(), 0);
}

void Server::handleClient(int clientFd) {
    char peekBuf[1024];
    ssize_t peekN = recv(clientFd, peekBuf, sizeof(peekBuf) - 1, MSG_PEEK);
    if (peekN <= 0) {
        close(clientFd);
        std::lock_guard<std::mutex> lock(sessionsMutex);
        sessions.erase(clientFd);
        return;
    }
    peekBuf[peekN] = '\0';
    std::string peekStr(peekBuf, peekN);

    // Check if HTTP / WebSocket
    if (peekStr.rfind("GET ", 0) == 0 ||
        peekStr.rfind("POST ", 0) == 0 ||
        peekStr.rfind("OPTIONS ", 0) == 0 ||
        peekStr.rfind("HEAD ", 0) == 0) {

        // Read complete HTTP request headers
        std::string requestStr;
        char buf[4096];
        while (running.load()) {
            ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            requestStr.append(buf, n);
            if (requestStr.find("\r\n\r\n") != std::string::npos) break;
        }

        // Check for WebSocket upgrade
        if (requestStr.find("Upgrade: websocket") != std::string::npos ||
            requestStr.find("upgrade: websocket") != std::string::npos) {
            ClientSession session;
            {
                std::lock_guard<std::mutex> lock(sessionsMutex);
                session = sessions[clientFd];
            }
            handleWebSocket(clientFd, session, requestStr);
        } else {
            serveHttpRequest(clientFd, requestStr);
        }

        close(clientFd);
        std::lock_guard<std::mutex> lock(sessionsMutex);
        sessions.erase(clientFd);
        return;
    }

    // Otherwise, Native TCP Socket handling
    ClientSession session;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        session = sessions[clientFd];
    }

    std::string lineBuf;
    char buffer[4096];
    while (running.load()) {
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        lineBuf.append(buffer, n);

        size_t newlinePos;
        while ((newlinePos = lineBuf.find('\n')) != std::string::npos) {
            std::string line = lineBuf.substr(0, newlinePos);
            lineBuf.erase(0, newlinePos + 1);

            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;

            try {
                auto j = nlohmann::json::parse(line);
                std::string event = j.value("event", "");
                nlohmann::json payload = j.contains("data") ? j["data"] : j;
                auto reply = handleCommand(event, payload, session);
                sendTcpJson(clientFd, event, reply);
            } catch (const std::exception& e) {
                nlohmann::json err = {{"error", e.what()}};
                sendTcpJson(clientFd, "error", err);
            }
        }
    }

    close(clientFd);
    std::lock_guard<std::mutex> lock(sessionsMutex);
    sessions.erase(clientFd);
}

} // namespace fastdb
