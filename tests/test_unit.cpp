#include <iostream>
#include <cassert>
#include <string>
#include <chrono>
#include "fastdb/user.hpp"
#include "fastdb/database.hpp"
#include "fastdb/crypto.hpp"

using namespace fastdb;

int main() {
    std::cout << "=== Running FastDB C++ Unit Tests ===" << std::endl;

    // Test 1: User creation & login
    int64_t now = crypto::current_time_ms();
    std::string email = "test_cpp_" + std::to_string(now) + "@test.com";

    CreateUser user;
    user.name("Test User C++");
    user.email(email);
    user.password("testpass123");

    auto saved = user.save();
    std::string uid = user.getId();
    assert(saved.contains(uid));
    assert(saved[uid]["email"] == email);
    assert(saved[uid].contains("token") && !saved[uid]["token"].get<std::string>().empty());
    std::cout << "✓ User save and token generation passed" << std::endl;

    auto loginRes = user.login(email, "testpass123");
    assert(loginRes["user"]["email"] == email);
    assert(loginRes.contains("token") && !loginRes["token"].get<std::string>().empty());
    std::cout << "✓ User login passed" << std::endl;

    auto found = user.find(email);
    assert(!found.is_null());
    assert(found["email"] == email);
    assert(!found.contains("password"));
    std::cout << "✓ User find passed" << std::endl;

    // Test 2: Database creation
    CreateDB dbCreator(uid);
    dbCreator.name("MyTestDB_CPP");
    auto createdDb = dbCreator.create();
    assert(createdDb.contains("databaseId"));
    assert(createdDb.contains("apiKey"));
    assert(createdDb["author"] == uid);
    std::string dbId = createdDb["databaseId"].get<std::string>();
    std::string apiKey = createdDb["apiKey"].get<std::string>();
    std::cout << "✓ Database creation passed" << std::endl;

    auto dbList = dbCreator.list();
    bool foundInList = false;
    for (const auto& d : dbList) {
        if (d.value("databaseId", "") == dbId) {
            foundInList = true;
            break;
        }
    }
    assert(foundInList);
    std::cout << "✓ Database list passed" << std::endl;

    // Test 3: Local Database connection & documents
    Database db(dbId, email, uid, apiKey);
    bool connected = db.connect();
    assert(connected);
    assert(db.isConnected());
    std::cout << "✓ Database connect passed" << std::endl;

    // Set document
    nlohmann::json itemData = {
        {"title", "Widget A"},
        {"price", 19.99},
        {"inStock", true}
    };
    auto doc = db.document("items").set(itemData);
    assert(doc.is_object() && !doc.empty());
    std::string docId = doc.begin().key();
    assert(!docId.empty());
    assert(doc[docId]["title"] == "Widget A");
    std::cout << "✓ Document set passed" << std::endl;

    // Read all
    auto all = db.document("items").read();
    assert(all.is_array());
    assert(all.size() == 1);
    assert(all[0]["title"] == "Widget A");
    std::cout << "✓ Document read all passed" << std::endl;

    // Query filter
    nlohmann::json matchQuery = {
        {"title", "Widget A"},
        {"inStock", true}
    };
    auto match = db.document("items").read(matchQuery);
    assert(match.size() == 1);

    nlohmann::json noMatchQuery = {
        {"title", "Widget B"}
    };
    auto noMatch = db.document("items").read(noMatchQuery);
    assert(noMatch.empty());
    std::cout << "✓ Document query filter passed" << std::endl;

    // Update by ID
    nlohmann::json updateData = {
        {"price", 24.99},
        {"color", "blue"}
    };
    auto updated = db.document("items").id(docId).update(updateData);
    assert(updated[docId]["price"] == 24.99);
    assert(updated[docId]["color"] == "blue");
    assert(updated[docId]["title"] == "Widget A");
    std::cout << "✓ Document update by ID passed" << std::endl;

    // Read by ID
    auto byId = db.document("items").id(docId).read();
    assert(byId["price"] == 24.99);
    assert(byId["color"] == "blue");
    std::cout << "✓ Document read by ID passed" << std::endl;

    // Delete by ID
    bool deleted = db.document("items").id(docId).deleteDoc();
    assert(deleted);
    auto afterDelete = db.document("items").id(docId).read();
    assert(afterDelete.is_null());
    std::cout << "✓ Document delete by ID passed" << std::endl;

    // Test collection methods
    db.createCollection("orders");
    auto cols = db.listCollections();
    bool foundOrderCol = false;
    for (const auto& c : cols) {
        if (c.value("name", "") == "orders") {
            foundOrderCol = true;
            break;
        }
    }
    assert(foundOrderCol);
    std::cout << "✓ FastDB createCollection & listCollections passed" << std::endl;

    nlohmann::json orderData = {
        {"orderNumber", "ORD-999"},
        {"amount", 150.00}
    };
    auto orderDoc = db.collection("orders").add(orderData);
    std::string orderId = orderDoc.begin().key();
    auto orderRead = db.collection("orders").doc(orderId).get();
    assert(orderRead["orderNumber"] == "ORD-999");
    std::cout << "✓ FastDB collection().add() & doc().get() passed" << std::endl;

    db.collection("orders").doc(orderId).deleteField("amount");
    auto afterFieldDel = db.collection("orders").doc(orderId).get();
    assert(!afterFieldDel.contains("amount"));
    std::cout << "✓ FastDB deleteField passed" << std::endl;

    db.deleteCollection("orders");
    auto colsAfterDel = db.listCollections();
    bool ordersStillExists = false;
    for (const auto& c : colsAfterDel) {
        if (c.value("name", "") == "orders") {
            ordersStillExists = true;
            break;
        }
    }
    assert(!ordersStillExists);
    std::cout << "✓ FastDB deleteCollection passed" << std::endl;

    // Disconnect
    db.disconnect();
    assert(!db.isConnected());
    std::cout << "✓ Database disconnect passed" << std::endl;

    // Clean up created database
    bool dbDeleted = dbCreator.deleteDb(dbId);
    assert(dbDeleted);
    std::cout << "✓ Database deletion passed" << std::endl;

    // Clean up user
    bool userDeleted = user.deleteUser(uid);
    assert(userDeleted);
    std::cout << "✓ User delete passed" << std::endl;

    std::cout << "\nALL FASTDB C++ UNIT TESTS PASSED!\n" << std::endl;
    return 0;
}
