#pragma once

/**
 * FastDB C++ - High-Performance Document Database & Real-Time Engine
 *
 * Umbrella header providing all FastDB core modules:
 * - Key generator
 * - User management & authentication (JWT HS256)
 * - Database and collection controller
 * - Client SDK
 * - Server engine
 */

#include "fastdb/crypto.hpp"
#include "fastdb/keygen.hpp"
#include "fastdb/user.hpp"
#include "fastdb/database.hpp"
#include "fastdb/client.hpp"
#include "fastdb/server.hpp"
