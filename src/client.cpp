#include "fastdb/client.hpp"
#include "fastdb/crypto.hpp"

#include <iostream>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>

namespace fastdb {
namespace client {

ClientConnection& ClientConnection::instance() {
    static ClientConnection s_inst;
    return s_inst;
}

ClientConnection::ClientConnection() {
    const char* env_url = std::getenv("DB_SERVER_URL");
    if (env_url && *env_url) {
        std::string url(env_url);
        // parse http://host:port
        size_t proto = url.find("://");
        std::string hostport = (proto != std::string::npos) ? url.substr(proto + 3) : url;
        size_t colon = hostport.find(':');
        if (colon != std::string::npos) {
            host = hostport.substr(0, colon);
            try {
                port = std::stoi(hostport.substr(colon + 1));
            } catch (...) {}
        } else {
            host = hostport;
        }
    }
}

ClientConnection::~ClientConnection() {
    disconnect();
}

void ClientConnection::setHostAndPort(const std::string& h, int p) {
    if (connected && (h != host || p != port)) {
        disconnect();
    }
    host = h;
    port = p;
}

std::string ClientConnection::getHost() const {
    return host;
}

int ClientConnection::getPort() const {
    return port;
}

bool ClientConnection::connect(const std::string& h, int p) {
    if (!h.empty()) host = h;
    if (p > 0) port = p;

    if (connected && socketFd >= 0) return true;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        return false;
    }

    for (int retry = 0; retry < 15; ++retry) {
        if (socketFd >= 0) {
            close(socketFd);
        }
        socketFd = socket(AF_INET, SOCK_STREAM, 0);
        if (socketFd < 0) return false;

        if (::connect(socketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            connected = true;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (socketFd >= 0) {
        close(socketFd);
        socketFd = -1;
    }
    return false;
}

void ClientConnection::disconnect() {
    if (socketFd >= 0) {
        close(socketFd);
        socketFd = -1;
    }
    connected = false;
}

bool ClientConnection::isConnected() const {
    return connected;
}

nlohmann::json ClientConnection::request(const std::string& event,
                                        const nlohmann::json& data,
                                        Callback callback) {
    if (!connected || socketFd < 0) {
        if (!connect()) {
            throw std::runtime_error("Failed to connect to FastDB server at " + host + ":" + std::to_string(port));
        }
    }

    std::string reqId = crypto::random_uuid();
    nlohmann::json payload = data.is_object() ? data : nlohmann::json::object();
    payload["_reqId"] = reqId;

    nlohmann::json reqMsg = {
        {"event", event},
        {"data", payload}
    };

    std::string line = reqMsg.dump() + "\n";
    ssize_t sent = send(socketFd, line.data(), line.size(), 0);
    if (sent < 0) {
        disconnect();
        throw std::runtime_error("Failed to send message to FastDB server");
    }

    // Read response line
    std::string resLine;
    char buf[1];
    while (true) {
        ssize_t n = recv(socketFd, buf, 1, 0);
        if (n <= 0) {
            disconnect();
            throw std::runtime_error("Connection closed by server while waiting for response");
        }
        if (buf[0] == '\n') break;
        resLine.push_back(buf[0]);
    }

    while (!resLine.empty() && (resLine.back() == '\r' || resLine.back() == ' ')) {
        resLine.pop_back();
    }

    nlohmann::json response;
    try {
        auto envelope = nlohmann::json::parse(resLine);
        response = envelope.contains("data") ? envelope["data"] : envelope;
    } catch (const std::exception& e) {
        throw std::runtime_error("Malformed response from server: " + std::string(e.what()));
    }

    if (response.is_object() && response.contains("error")) {
        std::string err = response["error"].is_string() ? response["error"].get<std::string>() : response["error"].dump();
        if (callback) {
            try { callback(response); } catch (...) {}
        }
        throw std::runtime_error(err);
    }

    nlohmann::json clean = response;
    if (clean.is_object() && clean.contains("_reqId")) {
        clean.erase("_reqId");
    }

    if (callback) {
        callback(clean);
    }
    return clean;
}

// ---------------------------------------------------------------------------
// Standalone request helpers
// ---------------------------------------------------------------------------
nlohmann::json Register(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("user:register", params, cb);
}

nlohmann::json Login(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("user:login", params, cb);
}

nlohmann::json FindUser(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("user:find", params, cb);
}

nlohmann::json DeleteUser(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("user:delete", params, cb);
}

nlohmann::json CreateDB(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("database:create", params, cb);
}

nlohmann::json ListDB(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("database:list", params, cb);
}

nlohmann::json GetDB(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("database:get", params, cb);
}

nlohmann::json DeleteDB(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("database:delete", params, cb);
}

nlohmann::json ListCollections(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("collection:list", params, cb);
}

nlohmann::json CreateCollection(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("collection:create", params, cb);
}

nlohmann::json DeleteCollection(const nlohmann::json& params, Callback cb) {
    return ClientConnection::instance().request("collection:delete", params, cb);
}

// ---------------------------------------------------------------------------
// ClientDocumentRef
// ---------------------------------------------------------------------------
ClientDocumentRef::ClientDocumentRef(const std::string& collectionName,
                                     const std::string& docId,
                                     const nlohmann::json& creds)
    : collectionName(collectionName), docId(docId), creds(creds) {}

nlohmann::json ClientDocumentRef::read(Callback cb) {
    nlohmann::json p = creds;
    p["collection"] = collectionName;
    p["id"] = docId;
    auto res = ClientConnection::instance().request("document:get", p);
    auto data = (res.is_object() && res.contains("data")) ? res["data"] : res;
    if (cb) cb(data);
    return data;
}

nlohmann::json ClientDocumentRef::set(const nlohmann::json& data, Callback cb) {
    nlohmann::json p = creds;
    p["collection"] = collectionName;
    p["id"] = docId;
    p["data"] = data;
    auto res = ClientConnection::instance().request("document:set", p);
    auto result = (res.is_object() && res.contains("data")) ? res["data"] : res;
    if (cb) cb(result);
    return result;
}

nlohmann::json ClientDocumentRef::update(const nlohmann::json& data, Callback cb) {
    nlohmann::json p = creds;
    p["collection"] = collectionName;
    p["id"] = docId;
    p["data"] = data;
    auto res = ClientConnection::instance().request("document:update", p);
    auto result = (res.is_object() && res.contains("data")) ? res["data"] : res;
    if (cb) cb(result);
    return result;
}

nlohmann::json ClientDocumentRef::deleteDoc(Callback cb) {
    nlohmann::json p = creds;
    p["collection"] = collectionName;
    p["id"] = docId;
    auto res = ClientConnection::instance().request("document:delete", p);
    auto result = (res.is_object() && res.contains("result")) ? res["result"] : res;
    if (cb) cb(result);
    return result;
}

nlohmann::json ClientDocumentRef::deleteField(const std::string& fieldName, Callback cb) {
    nlohmann::json p = creds;
    p["collection"] = collectionName;
    p["id"] = docId;
    p["fieldName"] = fieldName;
    auto res = ClientConnection::instance().request("document:deleteField", p);
    auto result = (res.is_object() && res.contains("data")) ? res["data"] : res;
    if (cb) cb(result);
    return result;
}

// ---------------------------------------------------------------------------
// ClientCollectionRef
// ---------------------------------------------------------------------------
ClientCollectionRef::ClientCollectionRef(const std::string& collectionName, const nlohmann::json& creds)
    : collectionName(collectionName), creds(creds) {}

ClientDocumentRef ClientCollectionRef::id(const std::string& docId) const {
    return ClientDocumentRef(collectionName, docId, creds);
}

nlohmann::json ClientCollectionRef::read(const nlohmann::json& query, Callback cb) {
    nlohmann::json p = creds;
    p["collection"] = collectionName;
    if (!query.is_null()) {
        p["query"] = query;
    }
    auto res = ClientConnection::instance().request("document:read", p);
    auto data = (res.is_object() && res.contains("data")) ? res["data"] : res;
    if (cb) cb(data);
    return data;
}

nlohmann::json ClientCollectionRef::set(const nlohmann::json& data, const std::string& customId, Callback cb) {
    nlohmann::json p = creds;
    p["collection"] = collectionName;
    p["data"] = data;
    if (!customId.empty()) {
        p["id"] = customId;
    }
    auto res = ClientConnection::instance().request("document:set", p);
    auto result = (res.is_object() && res.contains("data")) ? res["data"] : res;
    if (cb) cb(result);
    return result;
}

nlohmann::json ClientCollectionRef::add(const nlohmann::json& data, Callback cb) {
    return set(data, "", cb);
}

nlohmann::json ClientCollectionRef::deleteQuery(const nlohmann::json& query, Callback cb) {
    nlohmann::json p = creds;
    p["collection"] = collectionName;
    if (!query.is_null()) {
        p["query"] = query;
    }
    auto res = ClientConnection::instance().request("document:delete", p);
    auto result = (res.is_object() && res.contains("result")) ? res["result"] : res;
    if (cb) cb(result);
    return result;
}

// ---------------------------------------------------------------------------
// Database Client Class
// ---------------------------------------------------------------------------
Database::Database(const Options& options)
    : databaseId(options.databaseId), userEmail(options.userEmail),
      author(options.author), apiKey(options.apiKey),
      host(options.host.empty() ? ClientConnection::instance().getHost() : options.host),
      port(options.port <= 0 ? ClientConnection::instance().getPort() : options.port) {
    if (databaseId.empty() || userEmail.empty() || author.empty() || apiKey.empty()) {
        throw std::runtime_error("Missing required parameters");
    }
}

Database::Database(const std::string& databaseId,
                   const std::string& userEmail,
                   const std::string& author,
                   const std::string& apiKey,
                   const std::string& host,
                   int port)
    : Database(Options{databaseId, userEmail, author, apiKey, host, port}) {}

nlohmann::json Database::getCreds() const {
    return {
        {"databaseId", databaseId},
        {"userEmail", userEmail},
        {"author", author},
        {"apiKey", apiKey}
    };
}

bool Database::connect(Callback cb) {
    ClientConnection::instance().setHostAndPort(host, port);
    if (!ClientConnection::instance().connect()) {
        access = false;
        throw std::runtime_error("Could not connect to server socket");
    }

    try {
        auto res = ClientConnection::instance().request("database:connect", getCreds());
        access = true;
        if (cb) cb(res);
        return true;
    } catch (const std::exception& e) {
        access = false;
        throw;
    }
}

bool Database::disconnect(Callback cb) {
    try {
        auto res = ClientConnection::instance().request("database:disconnect", nlohmann::json::object());
        access = false;
        if (cb) cb(res);
        return true;
    } catch (...) {
        access = false;
        return true;
    }
}

bool Database::isConnected() const {
    return access;
}

nlohmann::json Database::listCollections(Callback cb) {
    if (!access) throw std::runtime_error("Database isn't connected");
    auto res = ClientConnection::instance().request("collection:list", getCreds());
    auto list = (res.is_object() && res.contains("collections")) ? res["collections"] : nlohmann::json::array();
    if (cb) cb(list);
    return list;
}

nlohmann::json Database::createCollection(const std::string& name, Callback cb) {
    if (!access) throw std::runtime_error("Database isn't connected");
    nlohmann::json p = getCreds();
    p["name"] = name;
    auto res = ClientConnection::instance().request("collection:create", p);
    if (cb) cb(res);
    return res;
}

nlohmann::json Database::deleteCollection(const std::string& name, Callback cb) {
    if (!access) throw std::runtime_error("Database isn't connected");
    nlohmann::json p = getCreds();
    p["name"] = name;
    auto res = ClientConnection::instance().request("collection:delete", p);
    if (cb) cb(res);
    return res;
}

ClientCollectionRef Database::collection(const std::string& name) const {
    if (!access) throw std::runtime_error("Database isn't connected");
    nlohmann::json creds = getCreds();
    return ClientCollectionRef(name, creds);
}

} // namespace client
} // namespace fastdb
