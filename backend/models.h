#ifndef MODELS_H
#define MODELS_H

#include <string>
#include <vector>
#include <queue>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct User {
    std::string userId;       // UPI ID (e.g., user@okhdfc)
    std::string name;         // Full Name
    std::string phone;        // Phone number
    std::string createdDate;  // Account creation date
    std::string lastDevice;   // Device identifier of last transaction
    std::string lastLocation; // City location of last transaction
};

struct Transaction {
    std::string transactionId;// Unique transaction ID (e.g., TXN100023)
    std::string senderUpi;    // Sender's UPI ID (HashMap key)
    std::string receiverUpi;  // Receiver's UPI ID
    double amount;            // Transaction amount
    std::string timestamp;    // Format: YYYY-MM-DD HH:MM:SS
    std::string device;       // Device used
    std::string location;     // City (e.g., Bangalore)
    int riskScore;            // 0 - 100
    std::string riskLevel;    // SAFE (0-49), SUSPICIOUS (50-74), FRAUD ALERT (75+)
    std::vector<std::string> riskReasons; // Weighted reasons (+20 Device Change, -15 Trusted Contact)
};

// Represents a summary of a user for the Max-Heap leaderboard
struct SuspiciousUser {
    std::string userId;
    std::string name;
    double maxRiskScore;
    int fraudCount;

    // Operator overload for max-heap comparison.
    bool operator<(const SuspiciousUser& other) const {
        if (maxRiskScore == other.maxRiskScore) {
            return fraudCount < other.fraudCount;
        }
        return maxRiskScore < other.maxRiskScore;
    }
};

// ============================================================================
// BEHAVIORAL PROFILE STRUCT [NEW]
// Tracks historical customer habits to enable adaptive anomaly checks.
// ============================================================================
struct UserProfile {
    double avgAmount = 0.0;
    double maxAmount = 0.0;
    double minAmount = 0.0;
    int totalTxCount = 0;
    std::unordered_map<std::string, int> frequentContacts; // receiverUpi -> txn count
    std::unordered_map<std::string, int> dailyTxCounts;     // YYYY-MM-DD -> txn count
    std::vector<int> hourlyTxCounts = std::vector<int>(24, 0); // Hour 0-23 frequency
};

// ============================================================================
// DSA CONCEPT: CUSTOM MAX-HEAP
// Maintains the leaderboard of the top suspicious users.
// ============================================================================
class MaxHeap {
private:
    std::vector<SuspiciousUser> heap;

    int parent(int i) { return (i - 1) / 2; }
    int leftChild(int i) { return 2 * i + 1; }
    int rightChild(int i) { return 2 * i + 2; }

    void heapifyUp(int i) {
        while (i > 0 && heap[parent(i)] < heap[i]) {
            std::swap(heap[parent(i)], heap[i]);
            i = parent(i);
        }
    }

    void heapifyDown(int i) {
        int size = heap.size();
        int largest = i;
        int left = leftChild(i);
        int right = rightChild(i);

        if (left < size && heap[largest] < heap[left]) {
            largest = left;
        }
        if (right < size && heap[largest] < heap[right]) {
            largest = right;
        }

        if (largest != i) {
            std::swap(heap[i], heap[largest]);
            heapifyDown(largest);
        }
    }

public:
    void insert(const SuspiciousUser& user) {
        heap.push_back(user);
        heapifyUp(heap.size() - 1);
    }

    SuspiciousUser extractMax() {
        if (heap.empty()) return {};
        SuspiciousUser maxVal = heap[0];
        heap[0] = heap.back();
        heap.pop_back();
        if (!heap.empty()) {
            heapifyDown(0);
        }
        return maxVal;
    }

    bool empty() const { return heap.empty(); }
    size_t size() const { return heap.size(); }
    void clear() { heap.clear(); }

    std::vector<SuspiciousUser> getSortedList() {
        MaxHeap tempHeap = *this;
        std::vector<SuspiciousUser> sorted;
        while (!tempHeap.empty()) {
            sorted.push_back(tempHeap.extractMax());
        }
        return sorted;
    }
};

// ============================================================================
// HELPER: Convert Date String to Epoch Time
// ============================================================================
inline std::time_t stringToTime(const std::string& timeStr) {
    std::tm t = {};
    std::istringstream ss(timeStr);
    ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        std::tm fallback = {};
        std::istringstream ss2(timeStr);
        ss2 >> std::get_time(&fallback, "%Y-%m-%d");
        if (ss2.fail()) return 0;
        return std::mktime(&fallback);
    }
    return std::mktime(&t);
}

// ============================================================================
// RISK ENGINE WITH ADAPTIVE BEHAVIORAL SCORING
// ============================================================================
class RiskEngine {
public:
    // Core DSA Maps
    std::unordered_map<std::string, User> userMap;
    std::unordered_map<std::string, std::vector<Transaction>> userHistory;
    std::unordered_map<std::string, std::unordered_set<std::string>> uniqueReceivers;
    std::unordered_map<std::string, std::deque<Transaction>> recentQueue;
    std::unordered_map<std::string, UserProfile> userProfiles; // Behavioral profiles

    std::vector<Transaction> allTransactions;

    bool addUser(const User& user) {
        if (userMap.find(user.userId) != userMap.end()) {
            return false;
        }
        userMap[user.userId] = user;
        // Initialize blank profile
        userProfiles[user.userId] = UserProfile();
        return true;
    }

    // Dynamic incremental profile updates
    void updateProfile(const Transaction& tx) {
        UserProfile& prof = userProfiles[tx.senderUpi];
        prof.totalTxCount++;

        // Update transaction limits
        if (prof.totalTxCount == 1) {
            prof.avgAmount = tx.amount;
            prof.maxAmount = tx.amount;
            prof.minAmount = tx.amount;
        } else {
            // Incremental rolling average formula: avg = avg + (newVal - avg) / count
            prof.avgAmount = prof.avgAmount + (tx.amount - prof.avgAmount) / prof.totalTxCount;
            if (tx.amount > prof.maxAmount) prof.maxAmount = tx.amount;
            if (tx.amount < prof.minAmount) prof.minAmount = tx.amount;
        }

        // Increment frequent contact counts
        prof.frequentContacts[tx.receiverUpi]++;

        // Increment daily transaction frequency
        std::string date = tx.timestamp.substr(0, 10);
        prof.dailyTxCounts[date]++;

        // Track hourly active patterns (Hour 0-23)
        if (tx.timestamp.length() >= 19) {
            try {
                int hour = std::stoi(tx.timestamp.substr(11, 2));
                if (hour >= 0 && hour < 24) {
                    prof.hourlyTxCounts[hour]++;
                }
            } catch (...) {}
        }
    }

    // Adaptive Risk Scoring Model
    Transaction evaluateTransaction(Transaction tx) {
        int score = 0;
        std::vector<std::string> reasons;

        // Check if sender exists
        auto senderIt = userMap.find(tx.senderUpi);
        if (senderIt == userMap.end()) {
            tx.riskScore = 100;
            tx.riskLevel = "FRAUD ALERT";
            tx.riskReasons.push_back("+100 Unregistered Sender UPI ID");
            return tx;
        }

        User& sender = senderIt->second;
        std::vector<Transaction>& history = userHistory[tx.senderUpi];
        UserProfile& prof = userProfiles[tx.senderUpi];
        std::time_t currentTxTime = stringToTime(tx.timestamp);

        // ====================================================================
        // POSITIVE WEIGHT CONTRIBUTIONS (RISK SCORING RULES)
        // ====================================================================

        // RULE 1: Adaptive Amount Anomaly Check (Behavior-based)
        if (prof.totalTxCount >= 3) {
            if (tx.amount > 10 * prof.avgAmount) {
                score += 50;
                std::stringstream ss;
                ss << "+50 Amount Anomaly (₹" << std::fixed << std::setprecision(0) << tx.amount 
                   << " is > 10x average of ₹" << std::setprecision(2) << prof.avgAmount << ")";
                reasons.push_back(ss.str());
            } else if (tx.amount > 5 * prof.avgAmount) {
                score += 35;
                std::stringstream ss;
                ss << "+35 Amount Anomaly (₹" << std::fixed << std::setprecision(0) << tx.amount 
                   << " is > 5x average of ₹" << std::setprecision(2) << prof.avgAmount << ")";
                reasons.push_back(ss.str());
            } else if (tx.amount > 3 * prof.avgAmount) {
                score += 20;
                std::stringstream ss;
                ss << "+20 Amount Anomaly (₹" << std::fixed << std::setprecision(0) << tx.amount 
                   << " is > 3x average of ₹" << std::setprecision(2) << prof.avgAmount << ")";
                reasons.push_back(ss.str());
            }
        }

        // Evict old elements outside sliding window from deque
        std::deque<Transaction>& recent = recentQueue[tx.senderUpi];
        while (!recent.empty() && (currentTxTime - stringToTime(recent.front().timestamp) > 600)) {
            recent.pop_front();
        }

        // RULE 2: Velocity Checks (Recent transactions in last 5 minutes)
        int txIn5Mins = 0;
        for (const auto& pastTx : recent) {
            if (currentTxTime - stringToTime(pastTx.timestamp) <= 300) {
                txIn5Mins++;
            }
        }
        
        if (txIn5Mins + 1 > 5) {
            score += 30;
            reasons.push_back("+30 Velocity Burst (" + std::to_string(txIn5Mins + 1) + " transactions in 5 minutes)");
        } else if (txIn5Mins + 1 > 3) {
            score += 15;
            reasons.push_back("+15 High Transaction Frequency (" + std::to_string(txIn5Mins + 1) + " transactions in 5 minutes)");
        }

        // RULE 3: Multiple unique receivers check (10 minutes sliding window)
        std::unordered_set<std::string> receiversIn10Mins;
        receiversIn10Mins.insert(tx.receiverUpi);
        for (const auto& pastTx : recent) {
            receiversIn10Mins.insert(pastTx.receiverUpi);
        }

        if (receiversIn10Mins.size() > 4) {
            score += 30;
            reasons.push_back("+30 Mule Distribution Risk (" + std::to_string(receiversIn10Mins.size()) + " unique receivers in 10 minutes)");
        } else if (receiversIn10Mins.size() > 2) {
            score += 15;
            reasons.push_back("+15 Multiple Unique Receivers (" + std::to_string(receiversIn10Mins.size()) + " unique receivers in 10 minutes)");
        }

        // RULE 4: New Receiver Check
        std::unordered_set<std::string>& userUniqueRecs = uniqueReceivers[tx.senderUpi];
        if (userUniqueRecs.find(tx.receiverUpi) == userUniqueRecs.end()) {
            score += 15;
            reasons.push_back("+15 New Receiver UPI: " + tx.receiverUpi);
        }

        // RULE 5: Device Change Check
        std::string lastDevice = history.empty() ? sender.lastDevice : history.back().device;
        if (!lastDevice.empty() && tx.device != lastDevice) {
            score += 20;
            reasons.push_back("+20 Device Anomaly (new device: '" + tx.device + "', previous was '" + lastDevice + "')");
        }

        // RULE 6: Location Change Check
        std::string lastLocation = history.empty() ? sender.lastLocation : history.back().location;
        if (!lastLocation.empty() && tx.location != lastLocation) {
            score += 25;
            reasons.push_back("+25 Location Anomaly (new location: '" + tx.location + "', previous was '" + lastLocation + "')");
        }

        // RULE 7: Hourly Active Pattern Check (Adaptive Night check)
        if (tx.timestamp.length() >= 19) {
            int hour = std::stoi(tx.timestamp.substr(11, 2));
            // Only perform check if user has established some behavior
            if (prof.totalTxCount >= 5) {
                // If user has NEVER transacted in this hour historically, trigger hour anomaly
                if (prof.hourlyTxCounts[hour] == 0) {
                    if (hour >= 23 || hour < 5) {
                        score += 10;
                        reasons.push_back("+10 Uncharacteristic Late-Night Session (" + tx.timestamp.substr(11, 5) + ")");
                    } else {
                        score += 5;
                        reasons.push_back("+5 Unusual Transaction Hour (" + tx.timestamp.substr(11, 5) + ")");
                    }
                }
            } else {
                // Fallback static rule for new users
                if (hour >= 23 || hour < 5) {
                    score += 8;
                    reasons.push_back("+8 Late-Night Session (" + tx.timestamp.substr(11, 5) + ")");
                }
            }
        }

        // ====================================================================
        // NEGATIVE WEIGHT REDUCTIONS (FALSE-POSITIVE PREVENTION)
        // ====================================================================

        // REDUCTION 1: Trusted Contact Reduction
        auto recIt = prof.frequentContacts.find(tx.receiverUpi);
        if (recIt != prof.frequentContacts.end() && recIt->second >= 3) {
            score -= 20;
            reasons.push_back("-20 Trusted Contact (frequency: " + std::to_string(recIt->second) + " txns)");
        }

        // REDUCTION 2: Normal Pattern Match (Amount is within 25% of average amount)
        if (prof.totalTxCount >= 3 && tx.amount >= 0.75 * prof.avgAmount && tx.amount <= 1.25 * prof.avgAmount) {
            score -= 15;
            reasons.push_back("-15 Normal Transaction Size (matches user average)");
        }

        // REDUCTION 3: Device Consistency
        if (!lastDevice.empty() && tx.device == lastDevice) {
            score -= 10;
            reasons.push_back("-10 Device Consistency matched");
        }

        // REDUCTION 4: Location Consistency
        if (!lastLocation.empty() && tx.location == lastLocation) {
            score -= 10;
            reasons.push_back("-10 Location Consistency matched");
        }

        // Final score mapping (capping within 0 - 100)
        tx.riskScore = std::max(0, std::min(100, score));

        if (tx.riskScore >= 75) {
            tx.riskLevel = "FRAUD ALERT";
        } else if (tx.riskScore >= 50) {
            tx.riskLevel = "SUSPICIOUS";
        } else {
            tx.riskLevel = "SAFE";
        }

        if (reasons.empty()) {
            reasons.push_back("No risk metrics flagged. Normal profile matched.");
        }
        tx.riskReasons = reasons;

        return tx;
    }

    void commitTransaction(const Transaction& tx) {
        userHistory[tx.senderUpi].push_back(tx);
        uniqueReceivers[tx.senderUpi].insert(tx.receiverUpi);
        recentQueue[tx.senderUpi].push_back(tx);
        allTransactions.push_back(tx);

        // Update profile stats dynamically
        updateProfile(tx);

        // Track last known nodes
        userMap[tx.senderUpi].lastDevice = tx.device;
        userMap[tx.senderUpi].lastLocation = tx.location;
    }

    std::vector<SuspiciousUser> calculateLeaderboard() {
        MaxHeap heap;

        for (const auto& pair : userMap) {
            const std::string& userId = pair.first;
            const User& user = pair.second;
            const std::vector<Transaction>& txs = userHistory[userId];

            double maxRisk = 0;
            int fraudCount = 0;

            for (const auto& tx : txs) {
                if (tx.riskScore > maxRisk) {
                    maxRisk = tx.riskScore;
                }
                if (tx.riskLevel == "FRAUD ALERT") {
                    fraudCount++;
                }
            }

            if (maxRisk > 0) {
                SuspiciousUser su;
                su.userId = userId;
                su.name = user.name;
                su.maxRiskScore = maxRisk;
                su.fraudCount = fraudCount;
                heap.insert(su);
            }
        }

        return heap.getSortedList();
    }
};

#endif
