#pragma once
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include "Limit.hpp"
#include "Order.hpp"

// a trade that happend, returned to caller so they can log/broadcast it
struct Trade {
    int buyOrderId;
    int sellOrderId;
    int price;
    int shares;
};

// why order got accepted/rejected, dont just check trades.empty()
enum class OrderStatus {
    Accepted,
    RejectedInvalidShares,
    RejectedInvalidPrice,
    RejectedDuplicateId,
    RejectedUnknownOrder
};

struct OrderResult {
    OrderStatus status;
    std::vector<Trade> trades;
    std::vector<int> selfTradeCancelledIds;
    bool resting = false;
};

// order book + matching engine.
// buyLimits/sellLimits ordered so best price is always begin()
class Book {
public:
    Book() = default;
    ~Book();

    Book(const Book&) = delete;
    Book& operator=(const Book&) = delete;

    // adds limit order, matches immediatly if it crosses, rest sits in book
    OrderResult addLimitOrder(int id, int traderId, bool buyOrSell, int shares, int price);

    // eats liqudity till filled or book empty, leftover just dropped
    OrderResult addMarketOrder(int id, int traderId, bool buyOrSell, int shares);

    bool cancelOrder(int id);

    // cancel + readd under the hood so it loses time priority
    OrderResult modifyOrder(int id, int newShares, int newPrice);

    bool hasBestBid() const { return !buyLimits.empty(); }
    bool hasBestAsk() const { return !sellLimits.empty(); }
    int bestBid() const { return buyLimits.begin()->first; }
    int bestAsk() const { return sellLimits.begin()->first; }

    bool isOrderActive(int id) const { return orderMap.count(id) > 0; }

    void printBook() const;

private:
    std::map<int, Limit*, std::greater<int>> buyLimits;
    std::map<int, Limit*> sellLimits;
    std::unordered_map<int, Limit*> buyLimitByPrice;
    std::unordered_map<int, Limit*> sellLimitByPrice;
    std::unordered_map<int, Order*> orderMap;

    Limit* getOrCreateLimit(bool buyOrSell, int price);
    void eraseLimitIfEmpty(Limit* limit, bool buyOrSell);

    void removeOrderRecord(Order* order);

    // like above but leaves the limit alone, used inside match()
    // since were still lopping over that limits queue
    void freeOrderKeepLimit(Order* order);

    void match(Order* incoming, bool unbounded, int priceLimit,
               std::vector<Trade>& trades, std::vector<int>& stpCancelled);
};
