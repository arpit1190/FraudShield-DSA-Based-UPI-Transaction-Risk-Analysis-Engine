#ifndef DATABASE_H
#define DATABASE_H

#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include "models.h"
#include "utils.h"

// ============================================================================
// PERSISTENCE LAYER: FILE-BASED JSON DATABASE
// Allows the application to work out-of-the-box on any system without requiring
// external database software (like MySQL, MongoDB, or PostgreSQL).
// We serialize state to/from raw text JSON files.
// ============================================================================
class Database {
private:
    std::string usersPath;
    std::string txPath;

    // Helper to read entire file content as a string
    std::string readFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    // Helper to write string content to a file
    bool writeFile(const std::string& filepath, const std::string& content) {
        std::ofstream file(filepath, std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << content;
        return true;
    }

public:
    Database(const std::string& usersFile = "users.json", const std::string& txFile = "transactions.json")
        : usersPath(usersFile), txPath(txFile) {}

    // Load users from users.json and load into RiskEngine memory maps
    bool loadUsers(RiskEngine& engine) {
        std::string jsonStr = readFile(usersPath);
        if (jsonStr.empty()) {
            std::cout << "[DB] No users file found or file is empty. Starting fresh." << std::endl;
            return false;
        }

        JSONValue parsed = JSONParser::parse(jsonStr);
        if (parsed.type != JSONType::Array) {
            std::cout << "[DB] Invalid users JSON format (expected array). Starting fresh." << std::endl;
            return false;
        }

        int loadedCount = 0;
        for (const auto& val : parsed.arrayVal) {
            User u = jsonToUser(val);
            if (!u.userId.empty()) {
                engine.addUser(u);
                loadedCount++;
            }
        }
        std::cout << "[DB] Loaded " << loadedCount << " users from " << usersPath << std::endl;
        return true;
    }

    // Load transactions from transactions.json and reconstruct engine history
    bool loadTransactions(RiskEngine& engine) {
        std::string jsonStr = readFile(txPath);
        if (jsonStr.empty()) {
            std::cout << "[DB] No transactions file found or file is empty. Starting fresh." << std::endl;
            return false;
        }

        JSONValue parsed = JSONParser::parse(jsonStr);
        if (parsed.type != JSONType::Array) {
            std::cout << "[DB] Invalid transactions JSON format (expected array). Starting fresh." << std::endl;
            return false;
        }

        int loadedCount = 0;
        for (const auto& val : parsed.arrayVal) {
            Transaction tx = jsonToTransaction(val);
            if (!tx.transactionId.empty()) {
                engine.commitTransaction(tx);
                loadedCount++;
            }
        }
        std::cout << "[DB] Loaded " << loadedCount << " transactions from " << txPath << std::endl;
        return true;
    }

    // Save all users from RiskEngine to users.json
    bool saveUsers(const RiskEngine& engine) {
        JSONValue arrayVal;
        arrayVal.type = JSONType::Array;

        for (const auto& pair : engine.userMap) {
            arrayVal.arrayVal.push_back(userToJSON(pair.second));
        }

        std::string serialized = arrayVal.stringify();
        return writeFile(usersPath, serialized);
    }

    // Save all transactions from RiskEngine to transactions.json
    bool saveTransactions(const RiskEngine& engine) {
        JSONValue arrayVal;
        arrayVal.type = JSONType::Array;

        for (const auto& tx : engine.allTransactions) {
            arrayVal.arrayVal.push_back(transactionToJSON(tx));
        }

        std::string serialized = arrayVal.stringify();
        return writeFile(txPath, serialized);
    }

    // Utility to save both data sets in one call
    bool saveAll(const RiskEngine& engine) {
        bool usersSaved = saveUsers(engine);
        bool txSaved = saveTransactions(engine);
        return usersSaved && txSaved;
    }
};

#endif
