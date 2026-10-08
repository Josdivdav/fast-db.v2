#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace fastdb {

class Database;

class DocumentRef {
public:
    DocumentRef(const std::string& databaseId,
                const std::string& author,
                const std::string& collectionName,
                const std::string& docId,
                const std::string& binPath);

    // Read single document by ID
    nlohmann::json read() const;
    nlohmann::json get() const { return read(); }

    // Write / overwrite document by ID
    nlohmann::json set(const nlohmann::json& data);

    // Partially update document by ID (merges data with existing fields)
    nlohmann::json update(const nlohmann::json& data);

    // Delete single document by ID
    bool deleteDoc();
    bool delete_doc() { return deleteDoc(); }

    // Remove a single field from document
    nlohmann::json deleteField(const std::string& fieldName);
    nlohmann::json delete_field(const std::string& fieldName) { return deleteField(fieldName); }

private:
    std::string databaseId;
    std::string author;
    std::string collectionName;
    std::string docId;
    std::string binPath;
};

class CollectionRef {
public:
    CollectionRef(const std::string& databaseId,
                  const std::string& author,
                  const std::string& collectionName,
                  const std::string& binPath);

    // Read all documents or filter matching query object
    nlohmann::json read(const nlohmann::json& query = nullptr) const;
    nlohmann::json get(const nlohmann::json& query = nullptr) const { return read(query); }

    // Set document with customId or auto-generated UUID
    nlohmann::json set(const nlohmann::json& data, const std::string& customId = "");

    // Add document with auto-generated ID (alias for set(data, ""))
    nlohmann::json add(const nlohmann::json& data);

    // Delete all matching documents or all documents if query is null
    nlohmann::json deleteQuery(const nlohmann::json& query = nullptr);
    nlohmann::json delete_query(const nlohmann::json& query = nullptr) { return deleteQuery(query); }

    // Document reference by ID
    DocumentRef id(const std::string& docId) const;
    DocumentRef doc(const std::string& docId) const { return id(docId); }

private:
    std::string databaseId;
    std::string author;
    std::string collectionName;
    std::string binPath;
};

class CreateDB {
public:
    explicit CreateDB(const std::string& uid, const std::string& customBinPath = "");

    CreateDB& name(const std::string& name);

    // Creates new database; returns { databaseId, apiKey, author, name }
    nlohmann::json create();

    // List all databases created by uid
    nlohmann::json list() const;

    // Get database info by ID
    nlohmann::json get(const std::string& databaseId) const;

    // Delete database by ID
    bool deleteDb(const std::string& databaseId);
    bool remove(const std::string& databaseId) { return deleteDb(databaseId); }

    // Base bin directory path configuration
    static void setDefaultBinPath(const std::string& path);
    static std::string getDefaultBinPath();

private:
    std::string uid;
    std::string binaryEntry;
    std::string pid;
    std::string confPath;
    std::string dbName;

    nlohmann::json readConfig() const;
    void writeConfig(const nlohmann::json& data);
};

class Database {
public:
    struct Options {
        std::string databaseId;
        std::string userEmail;
        std::string author;
        std::string apiKey;
        std::string customBinPath = "";
    };

    explicit Database(const Options& options);
    Database(const std::string& databaseId,
             const std::string& userEmail,
             const std::string& author,
             const std::string& apiKey,
             const std::string& customBinPath = "");

    // Connection lifecycle
    bool connect();
    bool disconnect();
    bool isConnected() const;

    // Collection management
    nlohmann::json listCollections() const;
    nlohmann::json collections() const { return listCollections(); }

    nlohmann::json createCollection(const std::string& name);
    nlohmann::json deleteCollection(const std::string& name);

    // Access collection
    CollectionRef collection(const std::string& name) const;
    CollectionRef document(const std::string& name) const { return collection(name); }

    // Configuration
    static void setDefaultBinPath(const std::string& path);
    static std::string getDefaultBinPath();

private:
    std::string databaseId;
    std::string userEmail;
    std::string author;
    std::string apiKey;
    std::string userEntry;
    std::string binPath;
    std::string id;
    bool access{false};

    void checkConnected() const;
};

} // namespace fastdb
