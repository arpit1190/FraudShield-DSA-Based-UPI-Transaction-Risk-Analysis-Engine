#ifndef UNICODE
#define UNICODE
#endif

// Set Windows version to Vista or newer for getaddrinfo / freeaddrinfo support in MinGW
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>
#include "models.h"
#include "utils.h"
#include "database.h"

// Automatically link Winsock library when using Microsoft Visual C++ Compiler
#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "8080"
#define BUFFER_SIZE 4096

// Shared State
RiskEngine engine;
Database db("users.json", "transactions.json");

// Helper to URL-decode strings
inline std::string urlDecode(const std::string& str) {
    std::string res;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            std::string hex = str.substr(i + 1, 2);
            char c = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            res += c;
            i += 2;
        } else if (str[i] == '+') {
            res += ' ';
        } else {
            res += str[i];
        }
    }
    return res;
}

// Robustly read local files from multiple relative paths
std::string getFileContent(const std::string& path) {
    std::vector<std::string> paths = {
        path,
        "frontend/" + path,
        "../frontend/" + path,
        "../" + path,
        "../../frontend/" + path
    };

    for (const auto& p : paths) {
        std::ifstream file(p, std::ios::binary);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }
    }
    return "";
}

// Send structured HTTP Responses with CORS headers
void sendHTTPResponse(SOCKET clientSocket, int statusCode, const std::string& statusText, const std::string& contentType, const std::string& body) {
    std::stringstream ss;
    ss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
       << "Content-Type: " << contentType << "; charset=utf-8\r\n"
       << "Content-Length: " << body.length() << "\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type\r\n"
       << "Connection: close\r\n\r\n"
       << body;
    std::string resp = ss.str();
    send(clientSocket, resp.c_str(), static_cast<int>(resp.length()), 0);
}

// Client handler routine (Multi-threadable)
void handleClient(SOCKET clientSocket) {
    char buf[BUFFER_SIZE];
    std::string requestStr;
    int bytesReceived = 0;

    // Read first chunk from socket
    bytesReceived = recv(clientSocket, buf, sizeof(buf) - 1, 0);
    if (bytesReceived <= 0) {
        closesocket(clientSocket);
        return;
    }
    buf[bytesReceived] = '\0';
    requestStr.append(buf, bytesReceived);

    // Find HTTP Headers and Body boundary
    size_t sep = requestStr.find("\r\n\r\n");
    if (sep == std::string::npos) {
        closesocket(clientSocket);
        return;
    }

    std::string headerBlock = requestStr.substr(0, sep);
    std::string bodyBlock = requestStr.substr(sep + 4);

    // Parse Request Method and Path
    std::stringstream headerStream(headerBlock);
    std::string method, fullPath, protocol;
    headerStream >> method >> fullPath >> protocol;

    // Separate Path and Query String
    std::string path = fullPath;
    std::string queryStr;
    size_t qPos = fullPath.find('?');
    if (qPos != std::string::npos) {
        path = fullPath.substr(0, qPos);
        queryStr = fullPath.substr(qPos + 1);
    }

    // Extract Content-Length for POST body parsing
    size_t clPos = headerBlock.find("Content-Length:");
    size_t contentLength = 0;
    if (clPos != std::string::npos) {
        size_t endLine = headerBlock.find("\r\n", clPos);
        std::string clVal = headerBlock.substr(clPos + 15, endLine - (clPos + 15));
        clVal.erase(0, clVal.find_first_not_of(" \t"));
        clVal.erase(clVal.find_last_not_of(" \t") + 1);
        try {
            contentLength = std::stoul(clVal);
        } catch (...) {
            contentLength = 0;
        }
    }

    // Read remaining body from socket if Content-Length is larger than currently received body
    while (bodyBlock.length() < contentLength) {
        bytesReceived = recv(clientSocket, buf, sizeof(buf) - 1, 0);
        if (bytesReceived <= 0) break;
        buf[bytesReceived] = '\0';
        bodyBlock.append(buf, bytesReceived);
    }
    std::string requestBody = bodyBlock.substr(0, contentLength);

    // Handle CORS preflight options request
    if (method == "OPTIONS") {
        sendHTTPResponse(clientSocket, 204, "No Content", "text/plain", "");
        closesocket(clientSocket);
        return;
    }

    // ROUTING SYSTEM
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        std::string html = getFileContent("index.html");
        if (html.empty()) {
            sendHTTPResponse(clientSocket, 404, "Not Found", "text/plain", "Frontend index.html file not found on server.");
        } else {
            sendHTTPResponse(clientSocket, 200, "OK", "text/html", html);
        }
    } 
    else if (method == "GET" && path == "/index.css") {
        std::string css = getFileContent("index.css");
        if (css.empty()) {
            sendHTTPResponse(clientSocket, 404, "Not Found", "text/plain", "index.css not found.");
        } else {
            sendHTTPResponse(clientSocket, 200, "OK", "text/css", css);
        }
    } 
    else if (method == "GET" && path == "/index.js") {
        std::string js = getFileContent("index.js");
        if (js.empty()) {
            sendHTTPResponse(clientSocket, 404, "Not Found", "text/plain", "index.js not found.");
        } else {
            sendHTTPResponse(clientSocket, 200, "OK", "application/javascript", js);
        }
    }
    // API: GET DASHBOARD
    else if (method == "GET" && path == "/api/dashboard") {
        int totalUsers = engine.userMap.size();
        int totalTransactions = engine.allTransactions.size();
        int fraudAlerts = 0;
        int suspiciousTransactions = 0;
        double totalAmount = 0.0;
        double highestTransaction = 0.0;

        std::map<std::string, int> fraudTrendMap;
        std::map<std::string, double> volumeTrendMap;
        std::map<std::string, int> userAlertsMap;

        // Rule Trigger Counters for explanations dashboard chart
        int amountAnomalyTriggers = 0;
        int velocitySpamTriggers = 0;
        int muleDispersionTriggers = 0;
        int newReceiverTriggers = 0;
        int deviceChangeTriggers = 0;
        int locationChangeTriggers = 0;
        int timeAnomalyTriggers = 0;

        for (const auto& tx : engine.allTransactions) {
            if (tx.riskLevel == "FRAUD ALERT") {
                fraudAlerts++;
            } else if (tx.riskLevel == "SUSPICIOUS") {
                suspiciousTransactions++;
            }
            
            totalAmount += tx.amount;
            if (tx.amount > highestTransaction) {
                highestTransaction = tx.amount;
            }

            // Extract YYYY-MM-DD for trending charts
            std::string date = tx.timestamp.substr(0, 10);
            volumeTrendMap[date] += tx.amount;
            if (tx.riskLevel == "FRAUD ALERT") {
                fraudTrendMap[date]++;
            }

            // Count user alerts
            if (tx.riskLevel == "FRAUD ALERT" || tx.riskLevel == "SUSPICIOUS") {
                userAlertsMap[tx.senderUpi]++;
            }

            // Count rule triggers for flagged accounts (reasons list)
            if (tx.riskScore > 0) {
                for (const auto& reason : tx.riskReasons) {
                    if (reason.find("Amount Anomaly") != std::string::npos) amountAnomalyTriggers++;
                    else if (reason.find("Velocity") != std::string::npos || reason.find("High Transaction Frequency") != std::string::npos) velocitySpamTriggers++;
                    else if (reason.find("Mule") != std::string::npos || reason.find("Multiple Unique Receivers") != std::string::npos) muleDispersionTriggers++;
                    else if (reason.find("New Receiver") != std::string::npos) newReceiverTriggers++;
                    else if (reason.find("Device Anomaly") != std::string::npos || reason.find("Device Change") != std::string::npos) deviceChangeTriggers++;
                    else if (reason.find("Location Anomaly") != std::string::npos || reason.find("Location Change") != std::string::npos) locationChangeTriggers++;
                    else if (reason.find("Late-Night") != std::string::npos || reason.find("Hour Anomaly") != std::string::npos || reason.find("Unusual") != std::string::npos) timeAnomalyTriggers++;
                }
            }
        }

        double avgAmount = totalTransactions > 0 ? (totalAmount / totalTransactions) : 0.0;

        // Build charts JSON data
        std::string fraudDates = "[", fraudCounts = "[";
        bool first = true;
        for (const auto& pair : fraudTrendMap) {
            if (!first) { fraudDates += ","; fraudCounts += ","; }
            first = false;
            fraudDates += "\"" + pair.first + "\"";
            fraudCounts += std::to_string(pair.second);
        }
        fraudDates += "]"; fraudCounts += "]";

        std::string volDates = "[", volAmounts = "[";
        first = true;
        for (const auto& pair : volumeTrendMap) {
            if (!first) { volDates += ","; volAmounts += ","; }
            first = false;
            volDates += "\"" + pair.first + "\"";
            volAmounts += std::to_string(pair.second);
        }
        volDates += "]"; volAmounts += "]";

        std::string activeUsersStr = "[", activeAlertCountsStr = "[";
        first = true;
        std::vector<std::pair<std::string, int>> userAlertsList(userAlertsMap.begin(), userAlertsMap.end());
        std::sort(userAlertsList.begin(), userAlertsList.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        int limit = 0;
        for (const auto& pair : userAlertsList) {
            if (limit++ >= 5) break;
            if (!first) { activeUsersStr += ","; activeAlertCountsStr += ","; }
            first = false;
            std::string name = engine.userMap[pair.first].name;
            activeUsersStr += "\"" + name + "\"";
            activeAlertCountsStr += std::to_string(pair.second);
        }
        activeUsersStr += "]"; activeAlertCountsStr += "]";

        // Compile Dashboard JSON Response
        std::stringstream ss;
        ss << "{"
           << "\"totalUsers\":" << totalUsers << ","
           << "\"totalTransactions\":" << totalTransactions << ","
           << "\"fraudAlerts\":" << fraudAlerts << ","
           << "\"suspiciousTransactions\":" << suspiciousTransactions << ","
           << "\"avgTransactionAmount\":" << avgAmount << ","
           << "\"highestTransaction\":" << highestTransaction << ","
           << "\"charts\":{"
               << "\"fraudTrend\":{\"dates\":" << fraudDates << ",\"counts\":" << fraudCounts << "},"
               << "\"txnVolume\":{\"dates\":" << volDates << ",\"amounts\":" << volAmounts << "},"
               << "\"riskDistribution\":{"
                   << "\"safe\":" << (totalTransactions - suspiciousTransactions - fraudAlerts) << ","
                   << "\"suspicious\":" << suspiciousTransactions << ","
                   << "\"fraud\":" << fraudAlerts
               << "},"
               << "\"topReasons\":{"
                   << "\"amount\":" << amountAnomalyTriggers << ","
                   << "\"velocity\":" << velocitySpamTriggers << ","
                   << "\"mule\":" << muleDispersionTriggers << ","
                   << "\"newReceiver\":" << newReceiverTriggers << ","
                   << "\"deviceChange\":" << deviceChangeTriggers << ","
                   << "\"locationChange\":" << locationChangeTriggers << ","
                   << "\"timeAnomaly\":" << timeAnomalyTriggers
               << "},"
               << "\"userActivity\":{\"usernames\":" << activeUsersStr << ",\"alertCounts\":" << activeAlertCountsStr << "}"
           << "}"
           << "}";

        sendHTTPResponse(clientSocket, 200, "OK", "application/json", ss.str());
    }
    // API: GET USER PROFILE (BEHAVIORAL STATISTICS) [NEW]
    else if (method == "GET" && path == "/api/user-profile") {
        std::map<std::string, std::string> queryParams;
        if (!queryStr.empty()) {
            std::stringstream ss(queryStr);
            std::string pair;
            while (std::getline(ss, pair, '&')) {
                size_t eqPos = pair.find('=');
                if (eqPos != std::string::npos) {
                    queryParams[pair.substr(0, eqPos)] = pair.substr(eqPos + 1);
                }
            }
        }

        std::string userId = urlDecode(queryParams["userId"]);
        if (userId.empty()) {
            userId = urlDecode(queryParams["user"]);
        }

        auto it = engine.userMap.find(userId);
        if (it == engine.userMap.end()) {
            sendHTTPResponse(clientSocket, 404, "Not Found", "application/json", "{\"error\":\"User VPA profile not found\"}");
            closesocket(clientSocket);
            return;
        }

        User& u = it->second;
        UserProfile& prof = engine.userProfiles[userId];

        std::stringstream ss;
        ss << "{"
           << "\"userId\":\"" << u.userId << "\","
           << "\"name\":\"" << u.name << "\","
           << "\"phone\":\"" << u.phone << "\","
           << "\"lastLocation\":\"" << u.lastLocation << "\","
           << "\"lastDevice\":\"" << u.lastDevice << "\","
           << "\"avgAmount\":" << prof.avgAmount << ","
           << "\"maxAmount\":" << prof.maxAmount << ","
           << "\"minAmount\":" << prof.minAmount << ","
           << "\"totalTxCount\":" << prof.totalTxCount << ","
           << "\"trustedContacts\":[";
        
        bool first = true;
        for (const auto& pair : prof.frequentContacts) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"receiver\":\"" << pair.first << "\",\"count\":" << pair.second << "}";
        }
        
        ss << "],\"hourlyHeatmap\":[";
        for (int i = 0; i < 24; ++i) {
            if (i > 0) ss << ",";
            ss << prof.hourlyTxCounts[i];
        }
        ss << "]}";

        sendHTTPResponse(clientSocket, 200, "OK", "application/json", ss.str());
    }
    // API: GET USERS
    else if (method == "GET" && path == "/api/users") {
        JSONValue arr;
        arr.type = JSONType::Array;
        for (const auto& pair : engine.userMap) {
            arr.arrayVal.push_back(userToJSON(pair.second));
        }
        sendHTTPResponse(clientSocket, 200, "OK", "application/json", arr.stringify());
    }
    // API: GET LEADERBOARD
    else if (method == "GET" && path == "/api/leaderboard") {
        std::vector<SuspiciousUser> rank = engine.calculateLeaderboard();
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < rank.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{"
               << "\"userId\":\"" << rank[i].userId << "\","
               << "\"name\":\"" << rank[i].name << "\","
               << "\"maxRiskScore\":" << rank[i].maxRiskScore << ","
               << "\"fraudCount\":" << rank[i].fraudCount
               << "}";
        }
        ss << "]";
        sendHTTPResponse(clientSocket, 200, "OK", "application/json", ss.str());
    }
    // API: GET TRANSACTIONS (WITH FILTERS)
    else if (method == "GET" && path == "/api/transactions") {
        std::map<std::string, std::string> queryParams;
        if (!queryStr.empty()) {
            std::stringstream ss(queryStr);
            std::string pair;
            while (std::getline(ss, pair, '&')) {
                size_t eqPos = pair.find('=');
                if (eqPos != std::string::npos) {
                    queryParams[pair.substr(0, eqPos)] = pair.substr(eqPos + 1);
                }
            }
        }

        std::string filterUser = urlDecode(queryParams["user"]);
        std::string filterDate = urlDecode(queryParams["date"]);
        std::string filterRisk = urlDecode(queryParams["risk"]);
        bool filterFraudOnly = queryParams["fraudOnly"] == "true";

        JSONValue arr;
        arr.type = JSONType::Array;

        for (const auto& tx : engine.allTransactions) {
            // Apply User Filter (case-insensitive UPI ID or Name)
            if (!filterUser.empty()) {
                std::string uLow = filterUser;
                std::transform(uLow.begin(), uLow.end(), uLow.begin(), ::tolower);

                std::string senderLow = tx.senderUpi;
                std::transform(senderLow.begin(), senderLow.end(), senderLow.begin(), ::tolower);

                std::string nameLow = engine.userMap[tx.senderUpi].name;
                std::transform(nameLow.begin(), nameLow.end(), nameLow.begin(), ::tolower);

                if (senderLow.find(uLow) == std::string::npos && nameLow.find(uLow) == std::string::npos) {
                    continue;
                }
            }

            // Apply Date Filter (matches YYYY-MM-DD prefix)
            if (!filterDate.empty() && tx.timestamp.find(filterDate) == std::string::npos) {
                continue;
            }

            // Apply Risk Level Filter
            if (!filterRisk.empty()) {
                if (filterRisk == "FRAUD" && tx.riskLevel != "FRAUD ALERT") continue;
                if (filterRisk == "SUSPICIOUS" && tx.riskLevel != "SUSPICIOUS") continue;
                if (filterRisk == "SAFE" && tx.riskLevel != "SAFE") continue;
            }

            // Apply Fraud-Only filter
            if (filterFraudOnly && tx.riskLevel != "FRAUD ALERT") {
                continue;
            }

            arr.arrayVal.push_back(transactionToJSON(tx));
        }

        // Return latest transactions first
        std::reverse(arr.arrayVal.begin(), arr.arrayVal.end());

        sendHTTPResponse(clientSocket, 200, "OK", "application/json", arr.stringify());
    }
    // API: POST ADD USER
    else if (method == "POST" && path == "/api/users") {
        JSONValue parsed = JSONParser::parse(requestBody);
        if (parsed.type != JSONType::Object) {
            sendHTTPResponse(clientSocket, 400, "Bad Request", "application/json", "{\"error\":\"Invalid User JSON format\"}");
            closesocket(clientSocket);
            return;
        }

        User u = jsonToUser(parsed);
        if (u.userId.empty() || u.name.empty()) {
            sendHTTPResponse(clientSocket, 400, "Bad Request", "application/json", "{\"error\":\"Missing required fields: userId, name\"}");
            closesocket(clientSocket);
            return;
        }

        if (u.createdDate.empty()) {
            u.createdDate = timeToString(std::time(nullptr)).substr(0, 10);
        }

        bool success = engine.addUser(u);
        if (success) {
            db.saveAll(engine);
            sendHTTPResponse(clientSocket, 201, "Created", "application/json", "{\"message\":\"User created successfully\"}");
        } else {
            sendHTTPResponse(clientSocket, 409, "Conflict", "application/json", "{\"error\":\"User UPI ID already exists\"}");
        }
    }
    // API: POST CREATE TRANSACTION
    else if (method == "POST" && path == "/api/transactions") {
        JSONValue parsed = JSONParser::parse(requestBody);
        if (parsed.type != JSONType::Object) {
            sendHTTPResponse(clientSocket, 400, "Bad Request", "application/json", "{\"error\":\"Invalid Transaction JSON format\"}");
            closesocket(clientSocket);
            return;
        }

        Transaction tx = jsonToTransaction(parsed);
        if (tx.senderUpi.empty() || tx.receiverUpi.empty() || tx.amount <= 0) {
            sendHTTPResponse(clientSocket, 400, "Bad Request", "application/json", "{\"error\":\"Missing fields: senderUpi, receiverUpi, amount\"}");
            closesocket(clientSocket);
            return;
        }

        if (tx.device.empty()) tx.device = "UPI Web Portal";
        if (tx.location.empty()) tx.location = "Online";
        if (tx.timestamp.empty()) {
            tx.timestamp = timeToString(std::time(nullptr));
        }
        if (tx.transactionId.empty()) {
            tx.transactionId = "TXN" + std::to_string(100000 + engine.allTransactions.size() + 1);
        }

        // Check if sender exists
        if (engine.userMap.find(tx.senderUpi) == engine.userMap.end()) {
            sendHTTPResponse(clientSocket, 404, "Not Found", "application/json", "{\"error\":\"Sender UPI ID is not registered\"}");
            closesocket(clientSocket);
            return;
        }

        // Run Risk Evaluation
        Transaction evaluated = engine.evaluateTransaction(tx);
        
        // Commit and persist
        engine.commitTransaction(evaluated);
        db.saveAll(engine);

        sendHTTPResponse(clientSocket, 200, "OK", "application/json", transactionToJSON(evaluated).stringify());
    }
    // API: POST RUN SIMULATOR
    else if (method == "POST" && path == "/api/simulator") {
        int count = 50; // default transactions to simulate
        if (!requestBody.empty()) {
            JSONValue parsed = JSONParser::parse(requestBody);
            if (parsed.type == JSONType::Object) {
                double parsedCount = getJSONNumber(parsed.objectVal, "count", 50.0);
                count = static_cast<int>(parsedCount);
            }
        }

        generateMockData(engine, count);
        db.saveAll(engine);

        std::stringstream ss;
        ss << "{\"message\":\"Simulator executed successfully. Generated " << count << " mock transactions.\"}";
        sendHTTPResponse(clientSocket, 200, "OK", "application/json", ss.str());
    }
    // 404 Routing
    else {
        sendHTTPResponse(clientSocket, 404, "Not Found", "text/plain", "HTTP 404: Endpoint not found.");
    }

    closesocket(clientSocket);
}

// Win32 Thread Entry point for handling client requests concurrently
DWORD WINAPI ClientHandlerThread(LPVOID lpParam) {
    SOCKET clientSocket = (SOCKET)(lpParam);
    handleClient(clientSocket);
    return 0;
}

int main() {
    // 1. Initialize Winsock
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct addrinfo* result = NULL;
    struct addrinfo hints;

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed with error: " << iResult << std::endl;
        return 1;
    }

    // 2. Load persistent database
    std::cout << "[INIT] Initializing FraudShield Database..." << std::endl;
    db.loadUsers(engine);
    db.loadTransactions(engine);

    // If database is empty, auto-generate standard baseline simulator data
    if (engine.userMap.empty()) {
        std::cout << "[INIT] Database is empty. Running initial mock generator (135 txs)..." << std::endl;
        generateMockData(engine, 135);
        db.saveAll(engine);
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        std::cerr << "getaddrinfo failed with error: " << iResult << std::endl;
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for the server to listen for incoming connection requests
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        std::cerr << "socket failed with error: " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Bind socket
    iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        std::cerr << "bind failed with error: " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    // Listen
    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        std::cerr << "listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "   FRAUDSHIELD REAL-TIME ENGINE STATUS: ONLINE    " << std::endl;
    std::cout << "   Server listening at http://localhost:8080      " << std::endl;
    std::cout << "==================================================" << std::endl;

    // Accept and handle client connections in a loop
    while (true) {
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            std::cerr << "accept failed with error: " << WSAGetLastError() << std::endl;
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        // Spawn a Win32 thread to handle client requests concurrently.
        HANDLE hThread = CreateThread(NULL, 0, ClientHandlerThread, (LPVOID)ClientSocket, 0, NULL);
        if (hThread) {
            CloseHandle(hThread); // Close thread handle to detach
        } else {
            // Fallback to synchronous handling
            handleClient(ClientSocket);
        }
    }

    closesocket(ListenSocket);
    WSACleanup();
    return 0;
}
