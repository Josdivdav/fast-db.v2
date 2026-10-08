#include "fastdb/database.hpp"
#include "fastdb/keygen.hpp"
#include "fastdb/crypto.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>

namespace fs = std::filesystem;

namespace fastdb {

static std::string s_defaultBinPath = "";

static std::string resolve_bin_path() {
    if (!s_defaultBinPath.empty()) {
        return s_defaultBinPath;
    }
    const char* env_path = std::getenv("FASTDB_BIN_PATH");
    if (env_path && *env_path) {
        return std::string(env_path);
    }
    if (fs::exists("../src/bin")) {
        return "../src/bin";
    }
    return "./src/bin";
}

void CreateDB::setDefaultBinPath(const std::string& path) {
    s_defaultBinPath = path;
}

std::string CreateDB::getDefaultBinPath() {
    return resolve_bin_path();
}

void Database::setDefaultBinPath(const std::string& path) {
    s_defaultBinPath = path;
}

std::string Database::getDefaultBinPath() {
    return resolve_bin_path();
}

// ---------------------------------------------------------------------------
// DocumentRef Implementation
// ---------------------------------------------------------------------------
DocumentRef::DocumentRef(const std::string& databaseId,
                         const std::string& author,
                         const std::string& collectionName,
                         const std::string& docId,
                         const std::string& binPath)
    : databaseId(databaseId), author(author), collectionName(collectionName),
      docId(docId), binPath(binPath) {}

nlohmann::json DocumentRef::read() const {
    std::string filePath = binPath + "/" + author + "/" + databaseId + "/" + collectionName + "/" + docId + ".json";
    if (!fs::exists(filePath)) {
        return nullptr;
    }
    try {
        std::ifstream ifs(filePath);
        if (!ifs.is_open()) return nullptr;
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        if (content.empty()) return nullptr;
        return nlohmann::json::parse(content);
    } catch (...) {
        return nullptr;
    }
}

nlohmann::json DocumentRef::set(const nlohmann::json& data) {
    CollectionRef coll(databaseId, author, collectionName, binPath);
    return coll.set(data, docId);
}

nlohmann::json DocumentRef::update(const nlohmann::json& data) {
    std::string collPath = binPath + "/" + author + "/" + databaseId + "/" + collectionName;
    fs::create_directories(collPath);

    std::string gatePath = collPath + "/gate.json";
    nlohmann::json gate = nlohmann::json::object();
    if (fs::exists(gatePath)) {
        try {
            std::ifstream ifs(gatePath);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());
            if (!content.empty()) gate = nlohmann::json::parse(content);
        } catch (...) {}
    }
    gate[docId] = docId;

    std::string docPath = collPath + "/" + docId + ".json";
    nlohmann::json ed = nlohmann::json::object();
    if (fs::exists(docPath)) {
        try {
            std::ifstream ifs(docPath);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());
            if (!content.empty()) ed = nlohmann::json::parse(content);
        } catch (...) {}
    }

    if (data.is_object()) {
        for (auto it = data.begin(); it != data.end(); ++it) {
            ed[it.key()] = it.value();
        }
    }
    ed["_id"] = docId;

    {
        std::ofstream ofs(docPath);
        ofs << ed.dump(2);
    }
    {
        std::ofstream ofs(gatePath);
        ofs << gate.dump(2);
    }

    nlohmann::json res = nlohmann::json::object();
    res[docId] = ed;
    return res;
}

bool DocumentRef::deleteDoc() {
    std::string collPath = binPath + "/" + author + "/" + databaseId + "/" + collectionName;
    std::string gatePath = collPath + "/gate.json";
    std::string docPath = collPath + "/" + docId + ".json";

    if (!fs::exists(gatePath)) return false;

    nlohmann::json gate = nlohmann::json::object();
    try {
        std::ifstream ifs(gatePath);
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        if (!content.empty()) gate = nlohmann::json::parse(content);
    } catch (...) {}

    if (!gate.contains(docId) && !fs::exists(docPath)) return false;

    if (gate.contains(docId)) {
        gate.erase(docId);
        std::ofstream ofs(gatePath);
        ofs << gate.dump(2);
    }

    if (fs::exists(docPath)) {
        fs::remove(docPath);
    }
    return true;
}

nlohmann::json DocumentRef::deleteField(const std::string& fieldName) {
    std::string collPath = binPath + "/" + author + "/" + databaseId + "/" + collectionName;
    std::string docPath = collPath + "/" + docId + ".json";

    if (!fs::exists(docPath)) return nullptr;

    nlohmann::json ed = nlohmann::json::object();
    try {
        std::ifstream ifs(docPath);
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        if (!content.empty()) ed = nlohmann::json::parse(content);
    } catch (...) {
        return nullptr;
    }

    if (ed.contains(fieldName)) {
        ed.erase(fieldName);
        std::ofstream ofs(docPath);
        ofs << ed.dump(2);
    }

    nlohmann::json res = nlohmann::json::object();
    res[docId] = ed;
    return res;
}

// ---------------------------------------------------------------------------
// CollectionRef Implementation
// ---------------------------------------------------------------------------
CollectionRef::CollectionRef(const std::string& databaseId,
                             const std::string& author,
                             const std::string& collectionName,
                             const std::string& binPath)
    : databaseId(databaseId), author(author), collectionName(collectionName), binPath(binPath) {}

DocumentRef CollectionRef::id(const std::string& docId) const {
    return DocumentRef(databaseId, author, collectionName, docId, binPath);
}

nlohmann::json CollectionRef::read(const nlohmann::json& query) const {
    std::string path = binPath + "/" + author + "/" + databaseId;
    std::string collPath = path + "/" + collectionName;
    std::string gatePath = collPath + "/gate.json";

    if (!fs::exists(gatePath)) {
        return nlohmann::json::array();
    }

    nlohmann::json gate = nlohmann::json::object();
    try {
        std::ifstream ifs(gatePath);
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        if (!content.empty()) gate = nlohmann::json::parse(content);
    } catch (...) {
        return nlohmann::json::array();
    }

    nlohmann::json docs = nlohmann::json::array();
    for (auto it = gate.begin(); it != gate.end(); ++it) {
        std::string docId = it.value().is_string() ? it.value().get<std::string>() : it.key();
        std::string filePath = collPath + "/" + docId + ".json";
        if (fs::exists(filePath)) {
            try {
                std::ifstream ifs(filePath);
                std::string content((std::istreambuf_iterator<char>(ifs)),
                                     std::istreambuf_iterator<char>());
                if (!content.empty()) {
                    nlohmann::json item = nlohmann::json::parse(content);

                    bool matches = true;
                    if (!query.is_null() && query.is_object()) {
                        for (auto qit = query.begin(); qit != query.end(); ++qit) {
                            if (!item.contains(qit.key()) || item[qit.key()] != qit.value()) {
                                matches = false;
                                break;
                            }
                        }
                    }
                    if (matches) {
                        docs.push_back(item);
                    }
                }
            } catch (...) {}
        }
    }
    return docs;
}

nlohmann::json CollectionRef::set(const nlohmann::json& data, const std::string& customId) {
    std::string path = binPath + "/" + author + "/" + databaseId;
    std::string collPath = path + "/" + collectionName;
    fs::create_directories(collPath);

    std::string gatePath = collPath + "/gate.json";
    nlohmann::json gate = nlohmann::json::object();
    if (fs::exists(gatePath)) {
        try {
            std::ifstream ifs(gatePath);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());
            if (!content.empty()) gate = nlohmann::json::parse(content);
        } catch (...) {}
    }

    std::string docId = customId;
    if (docId.empty()) {
        if (data.is_object() && data.contains("_id") && data["_id"].is_string()) {
            docId = data["_id"].get<std::string>();
        } else {
            docId = crypto::random_uuid();
        }
    }

    gate[docId] = docId;

    nlohmann::json docData = data.is_object() ? data : nlohmann::json::object();
    docData["_id"] = docId;

    std::string docPath = collPath + "/" + docId + ".json";
    {
        std::ofstream ofs(docPath);
        ofs << docData.dump(2);
    }
    {
        std::ofstream ofs(gatePath);
        ofs << gate.dump(2);
    }

    nlohmann::json res = nlohmann::json::object();
    res[docId] = docData;
    return res;
}

nlohmann::json CollectionRef::add(const nlohmann::json& data) {
    return set(data, "");
}

nlohmann::json CollectionRef::deleteQuery(const nlohmann::json& query) {
    nlohmann::json all = read(query);
    int count = 0;
    if (all.is_array()) {
        for (const auto& item : all) {
            if (item.is_object() && item.contains("_id") && item["_id"].is_string()) {
                std::string targetId = item["_id"].get<std::string>();
                if (id(targetId).deleteDoc()) {
                    count++;
                }
            }
        }
    }
    return {
        {"success", true},
        {"count", count}
    };
}

// ---------------------------------------------------------------------------
// CreateDB Implementation
// ---------------------------------------------------------------------------
CreateDB::CreateDB(const std::string& uid, const std::string& customBinPath)
    : uid(uid) {
    if (uid.empty()) {
        throw std::runtime_error("userId / uid is required");
    }
    std::string base = customBinPath.empty() ? resolve_bin_path() : customBinPath;
    binaryEntry = base + "/" + uid;
    pid = crypto::random_uuid();
    confPath = binaryEntry + "/config.db.json";

    fs::create_directories(binaryEntry);
    if (!fs::exists(confPath)) {
        std::ofstream ofs(confPath);
        ofs << "{}";
    }
}

CreateDB& CreateDB::name(const std::string& name) {
    dbName = name;
    return *this;
}

nlohmann::json CreateDB::readConfig() const {
    if (!fs::exists(confPath)) return nlohmann::json::object();
    try {
        std::ifstream ifs(confPath);
        if (!ifs.is_open()) return nlohmann::json::object();
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        if (content.empty()) return nlohmann::json::object();
        return nlohmann::json::parse(content);
    } catch (...) {
        return nlohmann::json::object();
    }
}

void CreateDB::writeConfig(const nlohmann::json& data) {
    fs::create_directories(binaryEntry);
    std::ofstream ofs(confPath);
    ofs << data.dump(2);
}

nlohmann::json CreateDB::create() {
    std::string key = generate_key(uid.length() > 0 ? uid.length() : 36);
    nlohmann::json data = readConfig();

    std::string assignedName = dbName.empty() ? "Untitled" : dbName;
    nlohmann::json record = {
        {"name", assignedName},
        {"databaseId", pid},
        {"createdAt", crypto::current_time_ms()},
        {"createdBy", uid},
        {"apiKey", key}
    };

    data[pid] = record;
    writeConfig(data);

    std::string fp = binaryEntry + "/" + pid;
    fs::create_directories(fp);
    std::ofstream dbOfs(fp + "/db.json");
    dbOfs << "{}";

    return {
        {"databaseId", pid},
        {"apiKey", key},
        {"author", uid},
        {"name", assignedName}
    };
}

nlohmann::json CreateDB::list() const {
    nlohmann::json data = readConfig();
    nlohmann::json arr = nlohmann::json::array();
    for (auto it = data.begin(); it != data.end(); ++it) {
        arr.push_back(it.value());
    }
    return arr;
}

nlohmann::json CreateDB::get(const std::string& databaseId) const {
    nlohmann::json data = readConfig();
    if (data.contains(databaseId)) {
        return data[databaseId];
    }
    return nullptr;
}

bool CreateDB::deleteDb(const std::string& databaseId) {
    nlohmann::json data = readConfig();
    if (!data.contains(databaseId)) {
        return false;
    }
    data.erase(databaseId);
    writeConfig(data);

    std::string dbDir = binaryEntry + "/" + databaseId;
    if (fs::exists(dbDir)) {
        std::error_code ec;
        fs::remove_all(dbDir, ec);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Database Implementation
// ---------------------------------------------------------------------------
Database::Database(const Options& options)
    : databaseId(options.databaseId), userEmail(options.userEmail),
      author(options.author), apiKey(options.apiKey) {
    if (databaseId.empty() || userEmail.empty() || author.empty() || apiKey.empty()) {
        throw std::runtime_error("Missing required parameters");
    }
    binPath = options.customBinPath.empty() ? resolve_bin_path() : options.customBinPath;
    userEntry = binPath + "/" + author + "/config.db.json";
    id = crypto::random_uuid();
    access = false;
}

Database::Database(const std::string& databaseId,
                   const std::string& userEmail,
                   const std::string& author,
                   const std::string& apiKey,
                   const std::string& customBinPath)
    : Database(Options{databaseId, userEmail, author, apiKey, customBinPath}) {}

void Database::checkConnected() const {
    if (!access) {
        throw std::runtime_error("Database isn't connected");
    }
}

bool Database::connect() {
    if (!fs::exists(userEntry)) {
        throw std::runtime_error("database not found or invalid apiKey");
    }

    nlohmann::json data;
    try {
        std::ifstream ifs(userEntry);
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        data = nlohmann::json::parse(content);
    } catch (...) {
        throw std::runtime_error("Failed to read database configuration");
    }

    bool authorized = false;
    for (auto it = data.begin(); it != data.end(); ++it) {
        const auto& item = it.value();
        if (item.is_object() &&
            item.value("apiKey", "") == apiKey &&
            item.value("databaseId", "") == databaseId) {
            authorized = true;
            break;
        }
    }

    if (!authorized) {
        access = false;
        throw std::runtime_error("Not Authorized");
    }

    access = true;
    return true;
}

bool Database::disconnect() {
    access = false;
    return true;
}

bool Database::isConnected() const {
    return access;
}

nlohmann::json Database::listCollections() const {
    checkConnected();
    std::string dbPath = binPath + "/" + author + "/" + databaseId;
    if (!fs::exists(dbPath)) return nlohmann::json::array();

    nlohmann::json collections = nlohmann::json::array();
    try {
        for (const auto& entry : fs::directory_iterator(dbPath)) {
            if (entry.is_directory()) {
                std::string collName = entry.path().filename().string();
                std::string gatePath = entry.path().string() + "/gate.json";
                int docCount = 0;
                if (fs::exists(gatePath)) {
                    try {
                        std::ifstream ifs(gatePath);
                        std::string content((std::istreambuf_iterator<char>(ifs)),
                                             std::istreambuf_iterator<char>());
                        if (!content.empty()) {
                            auto gate = nlohmann::json::parse(content);
                            if (gate.is_object()) docCount = static_cast<int>(gate.size());
                        }
                    } catch (...) {}
                }
                collections.push_back({
                    {"name", collName},
                    {"count", docCount}
                });
            }
        }
    } catch (...) {}

    return collections;
}

nlohmann::json Database::createCollection(const std::string& name) {
    checkConnected();
    std::string collPath = binPath + "/" + author + "/" + databaseId + "/" + name;
    fs::create_directories(collPath);
    std::string gatePath = collPath + "/gate.json";
    if (!fs::exists(gatePath)) {
        std::ofstream ofs(gatePath);
        ofs << "{}";
    }
    return {
        {"success", true},
        {"name", name}
    };
}

nlohmann::json Database::deleteCollection(const std::string& name) {
    checkConnected();
    std::string collPath = binPath + "/" + author + "/" + databaseId + "/" + name;
    if (fs::exists(collPath)) {
        std::error_code ec;
        fs::remove_all(collPath, ec);
    }
    return {
        {"success", true},
        {"name", name}
    };
}

CollectionRef Database::collection(const std::string& name) const {
    checkConnected();
    return CollectionRef(databaseId, author, name, binPath);
}

} // namespace fastdb
