#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include "models.h"

// ============================================================================
// DSA CONCEPT: CUSTOM JSON PARSER & SERIALIZER
// ============================================================================

enum class JSONType { Null, Object, Array, String, Number, Boolean };

struct JSONValue {
    JSONType type = JSONType::Null;
    std::string stringVal;
    double numberVal = 0.0;
    bool boolVal = false;
    std::vector<JSONValue> arrayVal;
    std::map<std::string, JSONValue> objectVal; // Sorted map for clean serialized outputs

    std::string stringify() const {
        std::stringstream ss;
        if (type == JSONType::Null) {
            ss << "null";
        } else if (type == JSONType::String) {
            ss << "\"" << escapeString(stringVal) << "\"";
        } else if (type == JSONType::Number) {
            ss << numberVal;
        } else if (type == JSONType::Boolean) {
            ss << (boolVal ? "true" : "false");
        } else if (type == JSONType::Array) {
            ss << "[";
            for (size_t i = 0; i < arrayVal.size(); ++i) {
                if (i > 0) ss << ",";
                ss << arrayVal[i].stringify();
            }
            ss << "]";
        } else if (type == JSONType::Object) {
            ss << "{";
            bool first = true;
            for (const auto& pair : objectVal) {
                if (!first) ss << ",";
                first = false;
                ss << "\"" << escapeString(pair.first) << "\":" << pair.second.stringify();
            }
            ss << "}";
        }
        return ss.str();
    }

private:
    static std::string escapeString(const std::string& s) {
        std::stringstream ss;
        for (char c : s) {
            if (c == '"') ss << "\\\"";
            else if (c == '\\') ss << "\\\\";
            else if (c == '\n') ss << "\\n";
            else if (c == '\r') ss << "\\r";
            else if (c == '\t') ss << "\\t";
            else ss << c;
        }
        return ss.str();
    }
};

class JSONParser {
private:
    std::string src;
    size_t pos = 0;

    void skipWhitespace() {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r')) {
            pos++;
        }
    }

    char peek() {
        skipWhitespace();
        if (pos >= src.size()) return '\0';
        return src[pos];
    }

    char get() {
        skipWhitespace();
        if (pos >= src.size()) return '\0';
        return src[pos++];
    }

    std::string parseString() {
        get(); // Consume open quote '"'
        std::string res;
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') {
                return res;
            } else if (c == '\\' && pos < src.size()) {
                char next = src[pos++];
                if (next == '"') res += '"';
                else if (next == '\\') res += '\\';
                else if (next == '/') res += '/';
                else if (next == 'b') res += '\b';
                else if (next == 'f') res += '\f';
                else if (next == 'n') res += '\n';
                else if (next == 'r') res += '\r';
                else if (next == 't') res += '\t';
            } else {
                res += c;
            }
        }
        return res;
    }

    double parseNumber() {
        size_t start = pos;
        if (src[pos] == '-') pos++;
        while (pos < src.size() && (std::isdigit(src[pos]) || src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E' || src[pos] == '+' || src[pos] == '-')) {
            pos++;
        }
        std::string numStr = src.substr(start, pos - start);
        try {
            return std::stod(numStr);
        } catch (...) {
            return 0.0;
        }
    }

    JSONValue parseValue() {
        char c = peek();
        if (c == '"') {
            JSONValue val;
            val.type = JSONType::String;
            val.stringVal = parseString();
            return val;
        } else if (c == '{') {
            return parseObject();
        } else if (c == '[') {
            return parseArray();
        } else if (std::isdigit(c) || c == '-') {
            JSONValue val;
            val.type = JSONType::Number;
            val.numberVal = parseNumber();
            return val;
        } else if (c == 't' || c == 'f') {
            JSONValue val;
            val.type = JSONType::Boolean;
            std::string s;
            while (pos < src.size() && std::isalpha(src[pos])) {
                s += src[pos++];
            }
            val.boolVal = (s == "true");
            return val;
        } else if (c == 'n') {
            JSONValue val;
            val.type = JSONType::Null;
            pos += 4; // consume "null"
            return val;
        }
        return {};
    }

    JSONValue parseObject() {
        JSONValue val;
        val.type = JSONType::Object;
        get(); // Consume '{'
        if (peek() == '}') {
            get(); // Consume '}'
            return val;
        }
        while (true) {
            std::string key = parseString();
            if (get() != ':') return val; // error
            val.objectVal[key] = parseValue();
            char next = peek();
            if (next == ',') {
                get(); // Consume ','
            } else if (next == '}') {
                get(); // Consume '}'
                break;
            } else {
                break;
            }
        }
        return val;
    }

    JSONValue parseArray() {
        JSONValue val;
        val.type = JSONType::Array;
        get(); // Consume '['
        if (peek() == ']') {
            get(); // Consume ']'
            return val;
        }
        while (true) {
            val.arrayVal.push_back(parseValue());
            char next = peek();
            if (next == ',') {
                get(); // Consume ','
            } else if (next == ']') {
                get(); // Consume ']'
                break;
            } else {
                break;
            }
        }
        return val;
    }

public:
    static JSONValue parse(const std::string& s) {
        JSONParser parser;
        parser.src = s;
        parser.pos = 0;
        return parser.parseValue();
    }
};

// ============================================================================
// HELPERS: SAFE MAPPING FIELD GETTERS
// ============================================================================
inline std::string getJSONString(const std::map<std::string, JSONValue>& m, const std::string& key, const std::string& def = "") {
    auto it = m.find(key);
    if (it != m.end() && it->second.type == JSONType::String) {
        return it->second.stringVal;
    }
    return def;
}

inline double getJSONNumber(const std::map<std::string, JSONValue>& m, const std::string& key, double def = 0.0) {
    auto it = m.find(key);
    if (it != m.end() && it->second.type == JSONType::Number) {
        return it->second.numberVal;
    }
    return def;
}

inline bool getJSONBool(const std::map<std::string, JSONValue>& m, const std::string& key, bool def = false) {
    auto it = m.find(key);
    if (it != m.end() && it->second.type == JSONType::Boolean) {
        return it->second.boolVal;
    }
    return def;
}

// ============================================================================
// SERIALIZATION
// ============================================================================
inline JSONValue userToJSON(const User& u) {
    JSONValue val;
    val.type = JSONType::Object;
    val.objectVal["userId"] = { JSONType::String, u.userId };
    val.objectVal["name"] = { JSONType::String, u.name };
    val.objectVal["phone"] = { JSONType::String, u.phone };
    val.objectVal["createdDate"] = { JSONType::String, u.createdDate };
    val.objectVal["lastDevice"] = { JSONType::String, u.lastDevice };
    val.objectVal["lastLocation"] = { JSONType::String, u.lastLocation };
    return val;
}

inline User jsonToUser(const JSONValue& val) {
    User u;
    if (val.type == JSONType::Object) {
        u.userId = getJSONString(val.objectVal, "userId");
        u.name = getJSONString(val.objectVal, "name");
        u.phone = getJSONString(val.objectVal, "phone");
        u.createdDate = getJSONString(val.objectVal, "createdDate");
        u.lastDevice = getJSONString(val.objectVal, "lastDevice");
        u.lastLocation = getJSONString(val.objectVal, "lastLocation");
    }
    return u;
}

inline JSONValue transactionToJSON(const Transaction& tx) {
    JSONValue val;
    val.type = JSONType::Object;
    val.objectVal["transactionId"] = { JSONType::String, tx.transactionId };
    val.objectVal["senderUpi"] = { JSONType::String, tx.senderUpi };
    val.objectVal["receiverUpi"] = { JSONType::String, tx.receiverUpi };
    val.objectVal["amount"] = { JSONType::Number, "", tx.amount };
    val.objectVal["timestamp"] = { JSONType::String, tx.timestamp };
    val.objectVal["device"] = { JSONType::String, tx.device };
    val.objectVal["location"] = { JSONType::String, tx.location };
    val.objectVal["riskScore"] = { JSONType::Number, "", static_cast<double>(tx.riskScore) };
    val.objectVal["riskLevel"] = { JSONType::String, tx.riskLevel };

    JSONValue reasonsArr;
    reasonsArr.type = JSONType::Array;
    for (const auto& r : tx.riskReasons) {
        reasonsArr.arrayVal.push_back({ JSONType::String, r });
    }
    val.objectVal["riskReasons"] = reasonsArr;
    return val;
}

inline Transaction jsonToTransaction(const JSONValue& val) {
    Transaction tx;
    if (val.type == JSONType::Object) {
        tx.transactionId = getJSONString(val.objectVal, "transactionId");
        tx.senderUpi = getJSONString(val.objectVal, "senderUpi");
        tx.receiverUpi = getJSONString(val.objectVal, "receiverUpi");
        tx.amount = getJSONNumber(val.objectVal, "amount");
        tx.timestamp = getJSONString(val.objectVal, "timestamp");
        tx.device = getJSONString(val.objectVal, "device");
        tx.location = getJSONString(val.objectVal, "location");
        tx.riskScore = static_cast<int>(getJSONNumber(val.objectVal, "riskScore"));
        tx.riskLevel = getJSONString(val.objectVal, "riskLevel");

        auto it = val.objectVal.find("riskReasons");
        if (it != val.objectVal.end() && it->second.type == JSONType::Array) {
            for (const auto& reasonVal : it->second.arrayVal) {
                if (reasonVal.type == JSONType::String) {
                    tx.riskReasons.push_back(reasonVal.stringVal);
                }
            }
        }
    }
    return tx;
}

inline std::string timeToString(std::time_t timeVal) {
    std::tm* tmPtr = std::localtime(&timeVal);
    char buf[30];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmPtr);
    return std::string(buf);
}

// ============================================================================
// TRANSACTION SIMULATOR ENGINE (UPDATED FOR BEHAVIOR PROFILE AND SCENARIOS)
// ============================================================================
inline void generateMockData(RiskEngine& engine, int numTxs) {
    if (!engine.userMap.empty()) return;

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // Register standard mock users
    std::vector<User> mockUsers = {
        { "amit.sharma@paytm", "Amit Sharma", "+91 98765 43210", "2026-01-10", "OnePlus 11", "Delhi" },
        { "priya.patel@okaxis", "Priya Patel", "+91 87654 32109", "2026-02-15", "iPhone 14 Pro", "Mumbai" },
        { "rahul.verma@oksbi", "Rahul Verma", "+91 76543 21098", "2026-03-01", "Samsung S23", "Bangalore" },
        { "ananya.iyer@okicici", "Ananya Iyer", "+91 65432 10987", "2026-03-22", "iPhone 13", "Chennai" },
        { "vikram.singh@okhdfc", "Vikram Singh", "+91 98123 45678", "2026-04-05", "Google Pixel 7", "Jaipur" },
        { "sneha.reddy@okaxis", "Sneha Reddy", "+91 88990 11223", "2026-04-18", "OnePlus Nord 3", "Hyderabad" },
        { "rohit.das@paytm", "Rohit Das", "+91 77889 90011", "2026-05-02", "Redmi Note 12", "Kolkata" },
        { "pooja.nair@oksbi", "Pooja Nair", "+91 66778 89900", "2026-05-19", "Vivo V27", "Kochi" }
    };

    for (const auto& u : mockUsers) {
        engine.addUser(u);
    }

    std::vector<std::string> shoppingRecs = { "amazon@okicici", "flipkart@paytm", "myntra@okaxis", "reliance.fresh@oksbi" };
    std::vector<std::string> utilityRecs = { "electricity@okaxis", "water.board@oksbi", "broadband@okhdfc" };
    std::vector<std::string> foodRecs = { "swiggy@okaxis", "zomato@paytm", "starbucks@okhdfc" };

    std::time_t now = std::time(nullptr);
    std::time_t baseTime = now - 3 * 24 * 3600; // 3 days ago
    int txCount = 0;

    // STEP 1: Generate realistic baseline profiles for each user (normal habits)
    for (const auto& sender : mockUsers) {
        // We will seed 15 normal transactions per user to build a stable profile
        
        // 1. Salary Credit on Day -2
        {
            Transaction salary;
            salary.transactionId = "TXN" + std::to_string(100000 + ++txCount);
            salary.senderUpi = sender.userId;
            salary.receiverUpi = "employer@paytm"; // Trusted source
            salary.amount = 50000.00 + (std::rand() % 30000); // 50k - 80k salary
            salary.timestamp = timeToString(baseTime + 3600 * 2); // 10:00 AM standard
            salary.device = sender.lastDevice;
            salary.location = sender.lastLocation;
            engine.commitTransaction(engine.evaluateTransaction(salary));
        }

        // 2. Regular Utility payments
        for (int i = 0; i < 2; ++i) {
            Transaction util;
            util.transactionId = "TXN" + std::to_string(100000 + ++txCount);
            util.senderUpi = sender.userId;
            util.receiverUpi = utilityRecs[i % utilityRecs.size()];
            util.amount = 800 + (std::rand() % 1500); // 800 - 2300 bills
            util.timestamp = timeToString(baseTime + 3600 * 12 + i * 86400); // midday
            util.device = sender.lastDevice;
            util.location = sender.lastLocation;
            engine.commitTransaction(engine.evaluateTransaction(util));
        }

        // 3. Normal everyday Food/Chai purchases (establishing frequent contacts & amounts)
        for (int i = 0; i < 10; ++i) {
            Transaction buy;
            buy.transactionId = "TXN" + std::to_string(100000 + ++txCount);
            buy.senderUpi = sender.userId;
            
            // Randomly choose shopping or food receiver
            buy.receiverUpi = (i % 2 == 0) ? foodRecs[i % foodRecs.size()] : shoppingRecs[i % shoppingRecs.size()];
            buy.amount = 150 + (std::rand() % 850); // ₹150 - ₹1000 standard shopping
            buy.timestamp = timeToString(baseTime + 3600 * (15 + (i % 3)) + i * 28800); // Daytime hours
            buy.device = sender.lastDevice;
            buy.location = sender.lastLocation;
            engine.commitTransaction(engine.evaluateTransaction(buy));
        }
    }

    // STEP 2: Inject specific fraud scenarios & false-positive cases (realistic weights)
    std::time_t fraudBaseTime = now - 3600; // 1 hour ago

    // Scenario 1: Account Takeover (ATO) -> high risk trigger
    // Rahul Verma (standard: Bangalore, Samsung S23, avg amount ~1200)
    // Suddenly attempts a transaction of ₹75,000 from a new device in Mumbai to a brand new receiver at night.
    // Expect: Trigger FRAUD ALERT (Device change, location change, new receiver, massive amount anomaly)
    {
        Transaction tx;
        tx.transactionId = "TXN" + std::to_string(100000 + ++txCount);
        tx.senderUpi = "rahul.verma@oksbi";
        tx.receiverUpi = "mule.beneficiary@okaxis";
        tx.amount = 75000.00; // Average is ~1500
        tx.timestamp = timeToString(fraudBaseTime + 300); // 1 hour ago
        tx.device = "OnePlus 12"; // Device mismatch (+20)
        tx.location = "Mumbai"; // Location mismatch (+25)
        
        // Let's modify the timestamp hour to 03:00 AM late night
        std::tm t = {};
        std::istringstream ss(tx.timestamp);
        ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
        t.tm_hour = 3;
        std::time_t atoTime = std::mktime(&t);
        tx.timestamp = timeToString(atoTime);

        engine.commitTransaction(engine.evaluateTransaction(tx));
    }

    // Scenario 2: False Positive Mitigation -> trusted night contact
    // Amit Sharma initiates a late night payment (01:45 AM) of ₹850 to swiggy@okaxis
    // Even though it is late night, he frequently transacts with swiggy@okaxis, on his OnePlus 11, in Delhi.
    // Expect: Trigger SAFE (Night Hour Anomaly triggers, but gets negated by Trusted Contact -20, Device match -10, Location match -10, Normal Size -15).
    {
        Transaction tx;
        tx.transactionId = "TXN" + std::to_string(100000 + ++txCount);
        tx.senderUpi = "amit.sharma@paytm";
        tx.receiverUpi = "swiggy@okaxis"; // Frequent receiver
        tx.amount = 850.00; // Normal range
        tx.timestamp = timeToString(fraudBaseTime + 600);
        tx.device = "OnePlus 11"; // Consistenet
        tx.location = "Delhi"; // Consistent

        // Set hour to 01:45 AM
        std::tm t = {};
        std::istringstream ss(tx.timestamp);
        ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
        t.tm_hour = 1;
        t.tm_min = 45;
        std::time_t fpTime = std::mktime(&t);
        tx.timestamp = timeToString(fpTime);

        engine.commitTransaction(engine.evaluateTransaction(tx));
    }

    // Scenario 3: Velocity Attack (Burst)
    // Priya Patel (iPhone 14, Mumbai) makes 6 quick transactions within 3 minutes to different new receivers.
    // Expect: Trigger FRAUD ALERT (Velocity burst +30, New receivers +15, Multiple unique receivers +15/30)
    for (int i = 0; i < 6; ++i) {
        Transaction tx;
        tx.transactionId = "TXN" + std::to_string(100000 + ++txCount);
        tx.senderUpi = "priya.patel@okaxis";
        
        std::stringstream ss;
        ss << "test.receiver" << i << "@okicici";
        tx.receiverUpi = ss.str();
        
        tx.amount = 1200.00;
        tx.timestamp = timeToString(fraudBaseTime + 1200 + (i * 25)); // 25 seconds apart
        tx.device = "iPhone 14 Pro";
        tx.location = "Mumbai";

        engine.commitTransaction(engine.evaluateTransaction(tx));
    }

    // Scenario 4: Money Mule Distribution (Mule dispersion pattern)
    // Sneha Reddy disperses ₹4,000 to 5 different new recipients within 8 minutes.
    // Expect: Trigger SUSPICIOUS / FRAUD ALERT (Mule risk +30, New receivers +15, Velocity +15)
    for (int i = 0; i < 5; ++i) {
        Transaction tx;
        tx.transactionId = "TXN" + std::to_string(100000 + ++txCount);
        tx.senderUpi = "sneha.reddy@okaxis";
        
        std::stringstream ss;
        ss << "agent.mule" << i << "@paytm";
        tx.receiverUpi = ss.str();

        tx.amount = 4000.00;
        tx.timestamp = timeToString(fraudBaseTime + 2000 + (i * 70)); // 70 seconds apart
        tx.device = "OnePlus Nord 3";
        tx.location = "Hyderabad";

        engine.commitTransaction(engine.evaluateTransaction(tx));
    }

    // Scenario 5: High Value Anomaly without other flags
    // Vikram Singh transfers ₹22,000 to flipkart@paytm (which is not new but average is 800)
    // Same device, same location.
    // Expect: Trigger SUSPICIOUS (Amount Anomaly +35, Device Match -10, Location Match -10, New Receiver +0, Trusted contact +0 = Score: 15. SAFE).
    // Note: This exhibits how profiling protects safe large payments from false positive triggers!
    {
        Transaction tx;
        tx.transactionId = "TXN" + std::to_string(100000 + ++txCount);
        tx.senderUpi = "vikram.singh@okhdfc";
        tx.receiverUpi = "flipkart@paytm"; // frequent contact
        tx.amount = 22000.00; // high amount
        tx.timestamp = timeToString(fraudBaseTime + 2800);
        tx.device = "Google Pixel 7";
        tx.location = "Jaipur";

        engine.commitTransaction(engine.evaluateTransaction(tx));
    }
}

#endif
