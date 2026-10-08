#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "fastdb/fastdb.hpp"

using namespace fastdb::client;

int main() {
    std::cout << "=== Running FastDB C++ End-to-End Integration Tests ===\n";

    int testPort = 5543;
    fastdb::Server server(testPort);
    if (!server.start()) {
        std::cerr << "Failed to start test server on port " << testPort << "\n";
        return 1;
    }
    std::cout << "✓ Server started on port " << testPort << "\n";

    // Give server a moment to bind and listen
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    try {
        // Connect Client
        ClientConnection::instance().setHostAndPort("127.0.0.1", testPort);
        bool clientConnected = ClientConnection::instance().connect("127.0.0.1", testPort);
        assert(clientConnected);
        std::cout << "✓ Connected to server via socket\n";

        // 1. Register user
        int64_t now = fastdb::crypto::current_time_ms();
        std::string email = "integ_test_" + std::to_string(now) + "@example.com";
        nlohmann::json cbData;

        auto regRes = Register({
            {"name", "Client User"},
            {"email", email},
            {"password", "SecretPassword123"}
        }, [&](const nlohmann::json& res) {
            cbData = res;
        });

        assert(regRes.contains("uid"));
        std::string uid = regRes["uid"].get<std::string>();
        assert(!uid.empty());
        assert(regRes[uid]["email"] == email);
        assert(cbData[uid]["email"] == email);
        std::cout << "✓ Register via socket passed (callback & synchronous response)\n";

        // 2. Login user
        auto loginRes = Login({
            {"email", email},
            {"password", "SecretPassword123"}
        });
        assert(loginRes["user"]["email"] == email);
        assert(loginRes.contains("token") && !loginRes["token"].get<std::string>().empty());
        std::cout << "✓ Login via socket passed\n";

        // 3. Find user
        auto foundUser = FindUser({
            {"email", email}
        });
        assert(foundUser["user"]["email"] == email);
        std::cout << "✓ Find user via socket passed\n";

        // 4. Create Database
        nlohmann::json cbDbRes;
        auto createDbRes = CreateDB({
            {"userId", uid},
            {"name", "ShopDB"}
        }, [&](const nlohmann::json& res) {
            cbDbRes = res;
        });
        assert(createDbRes.contains("databaseId"));
        assert(createDbRes.contains("apiKey"));
        assert(createDbRes["author"] == uid);
        assert(cbDbRes["databaseId"] == createDbRes["databaseId"]);
        std::string databaseId = createDbRes["databaseId"].get<std::string>();
        std::string apiKey = createDbRes["apiKey"].get<std::string>();
        std::cout << "✓ CreateDB via socket passed (callback & response)\n";

        // 5. List Databases
        auto listRes = ListDB({
            {"userId", uid}
        });
        assert(listRes.contains("databases") && listRes["databases"].is_array());
        bool foundDb = false;
        for (const auto& d : listRes["databases"]) {
            if (d.value("databaseId", "") == databaseId) foundDb = true;
        }
        assert(foundDb);
        std::cout << "✓ ListDB via socket passed\n";

        // 6. Get Database Info
        auto dbInfoRes = GetDB({
            {"userId", uid},
            {"databaseId", databaseId}
        });
        assert(dbInfoRes["database"]["name"] == "ShopDB");
        std::cout << "✓ GetDB via socket passed\n";

        // 7. Connect to Database using Client Database class
        Database db(databaseId, email, uid, apiKey, "127.0.0.1", testPort);
        bool connectOk = db.connect();
        assert(connectOk);
        assert(db.isConnected());
        std::cout << "✓ Client db.connect() passed\n";

        // 8. Document: set
        auto newProduct = db.document("products").set({
            {"name", "Mechanical Keyboard"},
            {"price", 89.99},
            {"category", "Electronics"}
        });
        assert(newProduct.is_object() && !newProduct.empty());
        std::string productId = newProduct.begin().key();
        assert(!productId.empty());
        assert(newProduct[productId]["name"] == "Mechanical Keyboard");
        std::cout << "✓ Client db.document().set() passed\n";

        // 9. Document: read all
        auto allProducts = db.document("products").read();
        assert(allProducts.is_array());
        assert(allProducts.size() == 1);
        assert(allProducts[0]["name"] == "Mechanical Keyboard");
        std::cout << "✓ Client db.document().read() passed\n";

        // 10. Document: read with filter
        auto filtered = db.document("products").read({
            {"category", "Electronics"}
        });
        assert(filtered.size() == 1);
        auto notFoundFilter = db.document("products").read({
            {"category", "Books"}
        });
        assert(notFoundFilter.empty());
        std::cout << "✓ Client db.document().read(filter) passed\n";

        // 11. Document: update by ID
        auto updated = db.document("products").id(productId).update({
            {"price", 79.99},
            {"color", "Midnight Black"}
        });
        assert(updated[productId]["price"] == 79.99);
        assert(updated[productId]["color"] == "Midnight Black");
        assert(updated[productId]["name"] == "Mechanical Keyboard");
        std::cout << "✓ Client db.document().id().update() passed\n";

        // 12. Document: get by ID
        auto singleDoc = db.document("products").id(productId).read();
        assert(singleDoc["price"] == 79.99);
        assert(singleDoc["color"] == "Midnight Black");
        std::cout << "✓ Client db.document().id().read() passed\n";

        // 13. Document: delete by ID
        auto deleteDocRes = db.document("products").id(productId).deleteDoc();
        assert(deleteDocRes.is_boolean() ? deleteDocRes.get<bool>() : deleteDocRes.value("success", false));
        auto afterDelete = db.document("products").id(productId).read();
        assert(afterDelete.is_null());
        std::cout << "✓ Client db.document().id().delete() passed\n";

        // 14. Disconnect database
        db.disconnect();
        assert(!db.isConnected());
        std::cout << "✓ Client db.disconnect() passed\n";

        // 15. Delete Database
        auto delDbRes = DeleteDB({
            {"userId", uid},
            {"databaseId", databaseId}
        });
        assert(delDbRes.value("success", false));
        std::cout << "✓ DeleteDB via socket passed\n";

        // 16. Delete User
        auto delUserRes = DeleteUser({
            {"uid", uid}
        });
        assert(delUserRes.value("success", false));
        std::cout << "✓ DeleteUser via socket passed\n";

        std::cout << "\nALL INTEGRATION TESTS PASSED SUCCESSFULLY!\n\n";

    } catch (const std::exception& e) {
        std::cerr << "Integration test error: " << e.what() << "\n";
        server.stop();
        return 1;
    }

    ClientConnection::instance().disconnect();
    server.stop();
    return 0;
}
