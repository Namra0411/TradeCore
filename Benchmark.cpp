// Benchmark harness for the Book matching engine.
//
// Methodology: warm up the book with a batch of two-sided resting
// orders, then fire a large stream of randomised requests (mostly
// new limit orders, plus cancels, modifies, and market orders) at
// it, timing each request individually with steady_clock. Cancel and
// modify targets are drawn from a maintained set of *actually still
// resting* order ids (O(1) add/remove via swap-and-pop), so the
// requested type mix is honoured rather than degrading into mostly
// AddLimit as ids go stale.
//
// Reports average latency, percentiles, and implied throughput
// overall and broken down by request type, and dumps every sample to
// a CSV for anyone who wants to plot a histogram.
//
// This measures single-threaded, in-process request handling only —
// no network/serialization overhead is included.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

#include "Book.hpp"

enum class ReqType { AddLimit, Cancel, Modify, Market };

static const char* reqName(ReqType t) {
    switch (t) {
        case ReqType::AddLimit: return "AddLimit";
        case ReqType::Cancel:   return "Cancel";
        case ReqType::Modify:   return "Modify";
        case ReqType::Market:   return "Market";
    }
    return "?";
}

struct Sample {
    ReqType type;
    long long nanos;
};

// O(1) add/remove/random-pick set of currently-resting order ids,
// so cancel/modify requests always target a real resting order.
class ActiveIdSet {
public:
    void add(int id) {
        posOf[id] = ids.size();
        ids.push_back(id);
    }
    void remove(int id) {
        auto it = posOf.find(id);
        if (it == posOf.end()) return;
        size_t pos = it->second;
        int lastId = ids.back();
        ids[pos] = lastId;
        posOf[lastId] = pos;
        ids.pop_back();
        posOf.erase(it);
    }
    bool empty() const { return ids.empty(); }
    size_t size() const { return ids.size(); }
    int pickRandom(std::mt19937& rng) const {
        std::uniform_int_distribution<size_t> d(0, ids.size() - 1);
        return ids[d(rng)];
    }

private:
    std::vector<int> ids;
    std::unordered_map<int, size_t> posOf;
};

static void printStats(const char* label, std::vector<long long>& nanos) {
    if (nanos.empty()) {
        std::printf("%-10s  (no samples)\n", label);
        return;
    }
    std::sort(nanos.begin(), nanos.end());
    long long sum = std::accumulate(nanos.begin(), nanos.end(), 0LL);
    double avg = static_cast<double>(sum) / nanos.size();
    auto pct = [&](double p) {
        size_t idx = static_cast<size_t>(p * (nanos.size() - 1));
        return nanos[idx];
    };
    std::printf("%-10s  n=%-8zu avg=%7.0fns  p50=%6lldns  p90=%6lldns  p99=%6lldns  max=%8lldns\n",
                label, nanos.size(), avg, pct(0.50), pct(0.90), pct(0.99), nanos.back());
}

int main() {
    const int WARMUP_ORDERS = 10000;   // initial two-sided resting liquidity
    const int NUM_REQUESTS  = 500000;  // timed requests
    const unsigned SEED     = 42;

    std::mt19937 rng(SEED);
    std::normal_distribution<double> priceDist(300.0, 50.0);
    std::uniform_int_distribution<int> sharesDist(1, 100);
    std::uniform_int_distribution<int> traderDist(1, 200); // 200 distinct traders
    std::uniform_real_distribution<double> reqTypeDist(0.0, 1.0);

    Book book;
    int nextId = 1;
    ActiveIdSet active;

    auto randomPrice = [&]() {
        int p = static_cast<int>(std::round(priceDist(rng)));
        return std::max(1, p);
    };

    // --- Warm-up: build a realistic two-sided book before timing starts ---
    for (int i = 0; i < WARMUP_ORDERS; ++i) {
        int price = randomPrice();
        bool buy = price < 300; // buys below center, sells above -> mostly non-crossing
        int shares = sharesDist(rng);
        int trader = traderDist(rng);
        int id = nextId++;
        OrderResult r = book.addLimitOrder(id, trader, buy, shares, price);
        if (r.resting) active.add(id);
    }

    std::printf("Warm-up complete: %d orders placed, %zu resting.\n\n", WARMUP_ORDERS, active.size());

    // --- Timed phase ---
    std::vector<Sample> samples;
    samples.reserve(NUM_REQUESTS);

    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_REQUESTS; ++i) {
        double r = reqTypeDist(rng);
        ReqType type;
        if (r < 0.70) type = ReqType::AddLimit;
        else if (r < 0.85 && !active.empty()) type = ReqType::Cancel;
        else if (r < 0.95 && !active.empty()) type = ReqType::Modify;
        else if (r < 0.95) type = ReqType::AddLimit; // active was empty, fall back
        else type = ReqType::Market;

        auto start = std::chrono::steady_clock::now();

        switch (type) {
            case ReqType::AddLimit: {
                int price = randomPrice();
                int shares = sharesDist(rng);
                int trader = traderDist(rng);
                bool buy = (rng() & 1) != 0;
                int id = nextId++;
                OrderResult res = book.addLimitOrder(id, trader, buy, shares, price);
                if (res.resting) active.add(id);
                for (int cancelledId : res.selfTradeCancelledIds) active.remove(cancelledId);
                break;
            }
            case ReqType::Cancel: {
                int id = active.pickRandom(rng);
                book.cancelOrder(id);
                active.remove(id);
                break;
            }
            case ReqType::Modify: {
                int id = active.pickRandom(rng);
                OrderResult res = book.modifyOrder(id, sharesDist(rng), randomPrice());
                // modifyOrder reuses the same id; update liveness based on outcome.
                if (!res.resting) active.remove(id);
                for (int cancelledId : res.selfTradeCancelledIds) active.remove(cancelledId);
                break;
            }
            case ReqType::Market: {
                int shares = sharesDist(rng);
                int trader = traderDist(rng);
                bool buy = (rng() & 1) != 0;
                int id = nextId++;
                OrderResult res = book.addMarketOrder(id, trader, buy, shares);
                for (int cancelledId : res.selfTradeCancelledIds) active.remove(cancelledId);
                break;
            }
        }

        auto end = std::chrono::steady_clock::now();
        long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        samples.push_back({type, ns});
    }

    auto t1 = std::chrono::steady_clock::now();
    double totalSeconds = std::chrono::duration<double>(t1 - t0).count();

    // --- Report ---
    std::vector<long long> all, addLimit, cancel, modify, market;
    for (auto& s : samples) {
        all.push_back(s.nanos);
        switch (s.type) {
            case ReqType::AddLimit: addLimit.push_back(s.nanos); break;
            case ReqType::Cancel:   cancel.push_back(s.nanos); break;
            case ReqType::Modify:   modify.push_back(s.nanos); break;
            case ReqType::Market:   market.push_back(s.nanos); break;
        }
    }

    std::printf("=== Overall (%d requests, %.3fs wall) ===\n", NUM_REQUESTS, totalSeconds);
    std::printf("Throughput: %.0f requests/sec\n\n", NUM_REQUESTS / totalSeconds);
    printStats("All", all);
    printStats(reqName(ReqType::AddLimit), addLimit);
    printStats(reqName(ReqType::Cancel), cancel);
    printStats(reqName(ReqType::Modify), modify);
    printStats(reqName(ReqType::Market), market);

    // --- CSV dump for anyone who wants a histogram ---
    const char* csvPath = "benchmark_latencies.csv";
    std::ofstream csv(csvPath);
    csv << "request_type,latency_ns\n";
    for (auto& s : samples) csv << reqName(s.type) << "," << s.nanos << "\n";
    std::printf("\nRaw per-request samples written to %s\n", csvPath);

    return 0;
}
