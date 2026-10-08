#include "fastdb/user.hpp"
#include "fastdb/crypto.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>
#include <atomic>

namespace fs = std::filesystem;

namespace fastdb {

static std::atomic<int64_t> threadI{0};
static const int64_t MAX_COUNT = 100000;
static std::string s_defaultUserEntry = "";

static std::string get_jwt_secret() {
    const char* env = std::getenv("JWT_SECRET");
    if (env && *env) {
        return std::string(env);
    }
    return "fast-db-secret-key-12345";
}

static std::string resolve_default_path() {
    if (!s_defaultUserEntry.empty()) {
        return s_defaultUserEntry;
    }
    const char* env_path = std::getenv("FASTDB_CONFIG_PATH");
    if (env_path && *env_path) {
        return std::string(env_path);
    }
    // Check if running from ./fastdb
    if (fs::exists("../src/config")) {
        return "../src/config/entry.users.json";
    }
    return "./src/config/entry.users.json";
}

void CreateUser::setDefaultUserEntry(const std::string& path) {
    s_defaultUserEntry = path;
}

std::string CreateUser::getDefaultUserEntry() {
    return resolve_default_path();
}

CreateUser::CreateUser(const std::string& customUserEntry) {
    if (!customUserEntry.empty()) {
        userEntry = customUserEntry;
    } else {
        userEntry = resolve_default_path();
    }

    id = crypto::random_uuid();

    fs::path p(userEntry);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }
    if (!fs::exists(p)) {
        std::ofstream ofs(p);
        ofs << "{}";
    }
}

CreateUser& CreateUser::name(const std::string& name) {
    t_store["name"] = name;
    return *this;
}

CreateUser& CreateUser::email(const std::string& email) {
    t_store["email"] = email;
    return *this;
}

CreateUser& CreateUser::password(const std::string& password) {
    t_store["password"] = password;
    return *this;
}

std::string CreateUser::getId() const {
    return id;
}

nlohmann::json CreateUser::readData() {
    if (!fs::exists(userEntry)) {
        return nlohmann::json::object();
    }
    try {
        std::ifstream ifs(userEntry);
        if (!ifs.is_open()) return nlohmann::json::object();
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        if (content.empty()) return nlohmann::json::object();
        return nlohmann::json::parse(content);
    } catch (...) {
        return nlohmann::json::object();
    }
}

void CreateUser::writeData(const nlohmann::json& data) {
    fs::path p(userEntry);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }
    std::ofstream ofs(userEntry);
    ofs << data.dump(2);
}

nlohmann::json CreateUser::save() {
    if (threadI >= MAX_COUNT) {
        throw std::runtime_error("Error please try again later");
    }

    nlohmann::json data = readData();

    std::string em = t_store["email"];
    std::string uid = id;

    for (auto it = data.begin(); it != data.end(); ++it) {
        const auto& u = it.value();
        if (u.is_object() && u.contains("email") && u["email"] == em) {
            throw std::runtime_error("User already exists");
        }
    }

    std::string secret = get_jwt_secret();
    nlohmann::json tokenPayload = {
        {"uid", uid},
        {"email", em}
    };
    std::string token = crypto::jwt_sign(tokenPayload, secret, 7 * 24 * 3600);

    nlohmann::json userRecord = {
        {"password", t_store["password"]},
        {"name", t_store["name"]},
        {"email", em},
        {"uid", uid},
        {"token", token},
        {"createdAt", crypto::current_time_ms()}
    };

    data[uid] = userRecord;
    writeData(data);
    threadI++;

    nlohmann::json result = nlohmann::json::object();
    result[uid] = userRecord;
    return result;
}

nlohmann::json CreateUser::login(const std::string& em, const std::string& pwd) {
    nlohmann::json data = readData();

    nlohmann::json foundUser;
    bool found = false;

    for (auto it = data.begin(); it != data.end(); ++it) {
        const auto& u = it.value();
        if (u.is_object() && u.contains("email") && u["email"] == em
            && u.contains("password") && u["password"] == pwd) {
            foundUser = u;
            found = true;
            break;
        }
    }

    if (!found) {
        throw std::runtime_error("Invalid email or password");
    }

    std::string uid = foundUser.value("uid", "");
    std::string emailVal = foundUser.value("email", "");
    std::string secret = get_jwt_secret();

    nlohmann::json tokenPayload = {
        {"uid", uid},
        {"email", emailVal}
    };
    std::string token = crypto::jwt_sign(tokenPayload, secret, 7 * 24 * 3600);

    nlohmann::json userData = foundUser;
    userData.erase("password");
    userData["token"] = token;

    nlohmann::json result = {
        {"user", userData},
        {"token", token}
    };
    return result;
}

nlohmann::json CreateUser::find(const std::string& em) {
    nlohmann::json data = readData();

    for (auto it = data.begin(); it != data.end(); ++it) {
        const auto& u = it.value();
        if (u.is_object() && u.contains("email") && u["email"] == em) {
            nlohmann::json copy = u;
            copy.erase("password");
            return copy;
        }
    }
    return nullptr;
}

nlohmann::json CreateUser::findById(const std::string& uid) {
    nlohmann::json data = readData();
    if (data.contains(uid)) {
        nlohmann::json copy = data[uid];
        copy.erase("password");
        return copy;
    }
    return nullptr;
}

nlohmann::json CreateUser::list() {
    nlohmann::json data = readData();
    nlohmann::json arr = nlohmann::json::array();
    for (auto it = data.begin(); it != data.end(); ++it) {
        nlohmann::json copy = it.value();
        copy.erase("password");
        arr.push_back(copy);
    }
    return arr;
}

bool CreateUser::deleteUser(const std::string& uid) {
    if (uid.empty()) {
        throw std::runtime_error("uid is required");
    }
    nlohmann::json data = readData();
    if (!data.contains(uid)) {
        throw std::runtime_error("Not Authorized");
    }
    data.erase(uid);
    writeData(data);
    return true;
}

} // namespace fastdb
