# FastDB (C++)

FastDB is a high-performance document database and real-time socket engine implemented in modern C++ (C++20). It provides an embedded JSON document store, JWT authentication, real-time TCP socket API, WebSocket/Engine.IO protocol compatibility, and an HTTP server for dashboard hosting.

## Features

- **Embedded Document Engine**: JSON-backed document collections with fast querying, filtering, updates, and field-level deletions.
- **Real-Time Networking**: Multi-threaded socket server supporting HTTP static file hosting, raw TCP line-delimited JSON, and WebSocket / Socket.IO frames.
- **Authentication & Security**: Built-in JWT (HS256) signing and verification, API key generation, and user role management.
- **Self-Contained & Portable**: Zero external system dependencies. Includes single-header `nlohmann/json` and pure C++ cryptographic primitives (SHA-256, HMAC-SHA256, SHA-1, Base64URL, UUIDv4).
- **Turnkey Build**: Compiles cleanly with standard `g++` / `clang++` using `make` or `cmake`.

---

## Directory Structure

```text
fastdb/
├── include/
│   ├── nlohmann/
│   │   └── json.hpp          # JSON for Modern C++
│   └── fastdb/
│       ├── crypto.hpp        # Cryptography, JWT HS256, UUIDv4
│       ├── keygen.hpp        # Alphanumeric API key generator
│       ├── user.hpp          # User authentication and management
│       ├── database.hpp      # FastDB database and collection controller
│       ├── server.hpp        # HTTP, WebSocket and TCP server
│       ├── client.hpp        # FastDB client SDK
│       └── fastdb.hpp        # Master umbrella header
├── src/
│   ├── crypto.cpp            # Crypto & JWT implementation
│   ├── keygen.cpp            # Key generation implementation
│   ├── user.cpp              # User persistence and JWT login
│   ├── database.cpp          # Document CRUD & collection logic
│   ├── server.cpp            # Server implementation
│   ├── client.cpp            # Client SDK implementation
│   └── main.cpp              # Server executable entry point
├── examples/
│   └── client.cpp            # Demonstration client application
├── tests/
│   ├── test_unit.cpp         # Unit tests (User, DB, Collections, Documents)
│   └── test_integration.cpp  # End-to-end socket integration tests
├── Makefile                  # Build script
├── CMakeLists.txt            # CMake configuration
└── README.md
```

---

## Building and Testing

### Build Everything
```bash
cd fastdb
make
```

### Run Tests
```bash
make test
```
Runs both unit tests (`./test_unit`) and socket integration tests (`./test_integration`).

### Run the Server
```bash
make run
# or
./fastdb_server --port 5000 --public ../public
```

### Run the Demonstration Client
```bash
make client
# or
./fastdb_client 127.0.0.1 5000
```

---

## C++ API Usage

### 1. Master Include
```cpp
#include "fastdb/fastdb.hpp"
```

### 2. User Management
```cpp
fastdb::CreateUser user;
user.name("Alice")
    .email("alice@example.com")
    .password("SecretPassword123");

// Save user and generate JWT token
auto result = user.save();
std::string uid = user.getId();

// Authenticate user
auto session = user.login("alice@example.com", "SecretPassword123");
std::string jwt = session["token"];
```

### 3. Database & Collections (Direct Controller)
```cpp
// Create a database for user
fastdb::CreateDB creator(uid);
creator.name("ProductionDB");
auto dbInfo = creator.create();
std::string dbId = dbInfo["databaseId"];
std::string apiKey = dbInfo["apiKey"];

// Connect to database
fastdb::Database db(dbId, "alice@example.com", uid, apiKey);
db.connect();

// Create collection and insert document
db.createCollection("products");
auto newDoc = db.collection("products").add({
    {"name", "Mechanical Keyboard"},
    {"price", 99.99},
    {"inStock", true}
});
std::string docId = newDoc.begin().key();

// Query with filter
auto matching = db.document("products").read({
    {"inStock", true}
});

// Update document
db.document("products").doc(docId).update({
    {"price", 89.99}
});

// Delete document
db.document("products").doc(docId).deleteDoc();

db.disconnect();
```

### 4. Client SDK (Over Network Socket)
```cpp
using namespace fastdb::client;

// Connect to remote FastDB server
Database db("database-uuid", "user@example.com", "user-uuid", "api-key", "127.0.0.1", 5000);
db.connect();

// Perform CRUD operations seamlessly
auto doc = db.collection("orders").add({
    {"orderNumber", "ORD-1001"},
    {"total", 250.00}
});
```

---

## License

ISC License.
