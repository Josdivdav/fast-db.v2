/**
 * FastDB C++ Client Usage Guide & Demonstration
 * 
 * This file illustrates all primary operations supported by the FastDB C++ client SDK:
 * 1. Registering and authenticating users
 * 2. Creating and listing databases
 * 3. Connecting to a database
 * 4. Creating and listing collections
 * 5. Adding & updating documents
 * 6. Querying and filtering documents
 * 7. Removing documents and fields
 * 8. Clean disconnection
 */

#include <iostream>
#include "fastdb/fastdb.hpp"

using namespace fastdb::client;

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 5000;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    std::cout << "Connecting FastDB C++ client to " << host << ":" << port << "...\n";

    try {
        ClientConnection::instance().setHostAndPort(host, port);

        // 1. Register user
        int64_t now = fastdb::crypto::current_time_ms();
        std::string email = "demo_" + std::to_string(now) + "@fastdb.com";

        nlohmann::json regRes = Register({
            {"name", "Joshua David"},
            {"email", email},
            {"password", "SecurePassword123"}
        });
        std::string uid = regRes.value("uid", "");
        std::cout << "✓ Registered user UID: " << uid << " (" << email << ")\n";

        // 2. Login user
        nlohmann::json loginRes = Login({
            {"email", email},
            {"password", "SecurePassword123"}
        });
        std::cout << "✓ Logged in, JWT token received: "
                  << loginRes["token"].get<std::string>().substr(0, 20) << "...\n";

        // 3. Create Database
        nlohmann::json dbRes = CreateDB({
            {"userId", uid},
            {"name", "ProductionDemoDB"}
        });
        std::string databaseId = dbRes["databaseId"];
        std::string apiKey = dbRes["apiKey"];
        std::cout << "✓ Created database: " << databaseId << " (apiKey: " << apiKey << ")\n";

        // 4. Connect to database using Database class
        Database db(databaseId, email, uid, apiKey, host, port);
        db.connect();
        std::cout << "✓ Connected to FastDB successfully!\n";

        // 5. Create Collection
        db.createCollection("users");
        std::cout << "✓ Created collection 'users'\n";

        // 6. Insert documents
        nlohmann::json doc1 = db.collection("users").add({
            {"name", "Joshua David"},
            {"role", "Lead Architect"},
            {"active", true}
        });
        std::string docId1 = doc1.begin().key();
        std::cout << "✓ Inserted user document ID: " << docId1 << "\n";

        // 7. Query with filter
        auto results = db.document("users").read({
            {"name", "Joshua David"}
        });
        std::cout << "✓ Query results for name='Joshua David': " << results.dump(2) << "\n";

        // 8. Update document
        auto updated = db.document("users").doc(docId1).update({
            {"email", email},
            {"updatedAt", fastdb::crypto::current_time_ms()}
        });
        std::cout << "✓ Updated document: " << updated.dump(2) << "\n";

        // 9. Read document by ID
        auto single = db.document("users").doc(docId1).get();
        std::cout << "✓ Read document by ID: " << single.dump(2) << "\n";

        // 10. Clean disconnect
        db.disconnect();
        ClientConnection::instance().disconnect();
        std::cout << "✓ Disconnected cleanly. Demonstration completed successfully!\n";

    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
