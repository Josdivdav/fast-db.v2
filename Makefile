CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread
INCLUDES = -I./include

LIB_SRCS = src/crypto.cpp src/keygen.cpp src/user.cpp src/database.cpp src/server.cpp src/client.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)
STATIC_LIB = libfastdb.a

SERVER_BIN = fastdb_server
CLIENT_BIN = fastdb_client
TEST_UNIT_BIN = test_unit
TEST_INTEG_BIN = test_integration

all: $(STATIC_LIB) $(SERVER_BIN) $(CLIENT_BIN) $(TEST_UNIT_BIN) $(TEST_INTEG_BIN)

$(STATIC_LIB): $(LIB_OBJS)
	ar rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(SERVER_BIN): src/main.o $(STATIC_LIB)
	$(CXX) $(CXXFLAGS) $(INCLUDES) src/main.o $(STATIC_LIB) -o $@

$(CLIENT_BIN): examples/client.o $(STATIC_LIB)
	$(CXX) $(CXXFLAGS) $(INCLUDES) examples/client.o $(STATIC_LIB) -o $@

$(TEST_UNIT_BIN): tests/test_unit.o $(STATIC_LIB)
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_unit.o $(STATIC_LIB) -o $@

$(TEST_INTEG_BIN): tests/test_integration.o $(STATIC_LIB)
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_integration.o $(STATIC_LIB) -o $@

test: $(TEST_UNIT_BIN) $(TEST_INTEG_BIN)
	@echo "=========================================="
	@echo "Running FastDB Unit Tests..."
	@echo "=========================================="
	./$(TEST_UNIT_BIN)
	@echo "=========================================="
	@echo "Running FastDB Integration Tests..."
	@echo "=========================================="
	./$(TEST_INTEG_BIN)
	@echo "All tests passed successfully!"

run: $(SERVER_BIN)
	./$(SERVER_BIN)

client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

clean:
	rm -f src/*.o examples/*.o tests/*.o $(STATIC_LIB) $(SERVER_BIN) $(CLIENT_BIN) $(TEST_UNIT_BIN) $(TEST_INTEG_BIN)

.PHONY: all test run client clean
