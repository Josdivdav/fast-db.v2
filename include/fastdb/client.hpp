#pragma once

#include <string>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace fastdb {
namespace client {

class SocketConnection;

// Callback type alias
using Callback = std::function<void(const nlohmann::json&)>;
using ErrorCallback = std::function<void(const std::string&, const nlohmann::json&)>;

class ClientConnection {
public:
    static ClientConnection& instance();

    bool connect(const std::string& host = "", int port = 0);
    void disconnect();
    bool isConnected() const;

    nlohmann::json request(const std::string& event,
                           const nlohmann::json& data = nlohmann::json::object(),
                           Callback callback = nullptr);

    void setHostAndPort(const std::string& host, int port);
    std::string getHost() const;
    int getPort() const;

private:
    ClientConnection();
    ~ClientConnection();

    std::string host{"127.0.0.1"};
    int port{5000};
    int socketFd{-1};
    bool connected{false};
};

// Top-level API functions matching socket.helper.js
nlohmann::json Register(const nlohmann::json& params, Callback cb = nullptr);
nlohmann::json Login(const nlohmann::json& params, Callback cb = nullptr);
nlohmann::json FindUser(const nlohmann::json& params, Callback cb = nullptr);
nlohmann::json DeleteUser(const nlohmann::json& params, Callback cb = nullptr);

nlohmann::json CreateDB(const nlohmann::json& params, Callback cb = nullptr);
nlohmann::json ListDB(const nlohmann::json& params, Callback cb = nullptr);
nlohmann::json GetDB(const nlohmann::json& params, Callback cb = nullptr);
nlohmann::json DeleteDB(const nlohmann::json& params, Callback cb = nullptr);

nlohmann::json ListCollections(const nlohmann::json& params, Callback cb = nullptr);
nlohmann::json CreateCollection(const nlohmann::json& params, Callback cb = nullptr);
nlohmann::json DeleteCollection(const nlohmann::json& params, Callback cb = nullptr);

class ClientDocumentRef {
public:
    ClientDocumentRef(const std::string& collectionName,
                      const std::string& docId,
                      const nlohmann::json& creds);

    nlohmann::json read(Callback cb = nullptr);
    nlohmann::json get(Callback cb = nullptr) { return read(cb); }

    nlohmann::json set(const nlohmann::json& data, Callback cb = nullptr);
    nlohmann::json update(const nlohmann::json& data, Callback cb = nullptr);
    nlohmann::json deleteDoc(Callback cb = nullptr);
    nlohmann::json deleteField(const std::string& fieldName, Callback cb = nullptr);

private:
    std::string collectionName;
    std::string docId;
    nlohmann::json creds;
};

class ClientCollectionRef {
public:
    ClientCollectionRef(const std::string& collectionName, const nlohmann::json& creds);

    nlohmann::json read(const nlohmann::json& query = nullptr, Callback cb = nullptr);
    nlohmann::json get(const nlohmann::json& query = nullptr, Callback cb = nullptr) { return read(query, cb); }

    nlohmann::json set(const nlohmann::json& data, const std::string& customId = "", Callback cb = nullptr);
    nlohmann::json add(const nlohmann::json& data, Callback cb = nullptr);

    nlohmann::json deleteQuery(const nlohmann::json& query = nullptr, Callback cb = nullptr);

    ClientDocumentRef id(const std::string& docId) const;
    ClientDocumentRef doc(const std::string& docId) const { return id(docId); }

private:
    std::string collectionName;
    nlohmann::json creds;
};

class Database {
public:
    struct Options {
        std::string databaseId;
        std::string userEmail;
        std::string author;
        std::string apiKey;
        std::string host = "";
        int port = 0;
    };

    explicit Database(const Options& options);
    Database(const std::string& databaseId,
             const std::string& userEmail,
             const std::string& author,
             const std::string& apiKey,
             const std::string& host = "",
             int port = 0);

    bool connect(Callback cb = nullptr);
    bool disconnect(Callback cb = nullptr);
    bool isConnected() const;

    nlohmann::json listCollections(Callback cb = nullptr);
    nlohmann::json collections(Callback cb = nullptr) { return listCollections(cb); }

    nlohmann::json createCollection(const std::string& name, Callback cb = nullptr);
    nlohmann::json deleteCollection(const std::string& name, Callback cb = nullptr);

    ClientCollectionRef collection(const std::string& name) const;
    ClientCollectionRef document(const std::string& name) const { return collection(name); }

private:
    std::string databaseId;
    std::string userEmail;
    std::string author;
    std::string apiKey;
    std::string host;
    int port;
    bool access{false};

    nlohmann::json getCreds() const;
};

} // namespace client
} // namespace fastdb
