#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <nlohmann/json.hpp>

namespace fastdb {

class CreateUser {
public:
    explicit CreateUser(const std::string& customUserEntry = "");

    CreateUser& name(const std::string& name);
    CreateUser& email(const std::string& email);
    CreateUser& password(const std::string& password);

    std::string getId() const;

    // Persist new user; returns json object { [uid]: userRecord }
    nlohmann::json save();

    // Authenticate user; returns json object { "user": userData, "token": token }
    nlohmann::json login(const std::string& email, const std::string& password);

    // Find user by email (without password); returns json object or null
    nlohmann::json find(const std::string& email);

    // Find user by uid (without password); returns json object or null
    nlohmann::json findById(const std::string& uid);

    // List all users (without passwords); returns json array
    nlohmann::json list();

    // Delete user by uid; returns true if successful
    bool deleteUser(const std::string& uid);
    bool remove(const std::string& uid) { return deleteUser(uid); }

    // Path configuration
    static void setDefaultUserEntry(const std::string& path);
    static std::string getDefaultUserEntry();

private:
    std::string userEntry;
    std::string id;
    std::unordered_map<std::string, std::string> t_store;

    nlohmann::json readData();
    void writeData(const nlohmann::json& data);
};

} // namespace fastdb
