#pragma once
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include "Limit.hpp"
#include "Order.hpp"

// One executed trade, reported back to the caller so it can be
// logged / broadcast as market data.
struct Trade {
    int buyOrderId;
    int sellOrderId;
    int price;
    int shares;
};

// Why an order request was or wasn't accepted. Callers should check
// this rather than inferring rejection from an empty trades vector,
// since "Accepted" with zero trades (a resting order that didn't
// cross) is a normal, valid outcome.
enum class OrderStatus {
    Accepted,
    RejectedInvalidShares,   // shares <= 0
    RejectedInvalidPrice,    // price <= 0 (limit orders only)
    RejectedDuplicateId,     // an order with this id is already resting
    RejectedUnknownOrder     // cancel/modify referenced an id not in the book
};

struct OrderResult {
    OrderStatus status;
    std::vector<Trade> trades;
    std::vector<int> selfTradeCancelledIds; // resting order ids cancelled by STP
    bool resting = false; // true if this order (or its remainder) ended up resting in the book
};

// Core limit order book + matching engine.
//
// Two sides are kept as price -> Limit* maps, ordered so that the
// best price for each side is always at begin():
//   sellLimits: ascending  (lowest ask first)
//   buyLimits : descending (highest bid first)
//
// A parallel unordered_map per side gives O(1) "does a limit already
// exist at this price" lookups, and a single unordered_map<id, Order*>
// gives O(1) cancel/modify by order id.
class Book {
public:
    Book() = default;
    ~Book();

    // Non-copyable (raw owning pointers below).
    Book(const Book&) = delete;
    Book& operator=(const Book&) = delete;

    // Add a limit order. If it crosses the book it executes
    // immediately against resting orders (price-time priority);
    // any remainder rests in the book.
    //
    // `traderId` identifies the owner and is used for self-trade
    // prevention (STP): if this order would otherwise match against
    // a resting order with the same traderId, that resting order is
    // cancelled instead of traded against (reported in
    // selfTradeCancelledIds), and matching continues against the
    // next order in the queue.
    //
    // Rejected (bad shares/price, or duplicate id) if status != Accepted;
    // trades/selfTradeCancelledIds will be empty in that case.
    OrderResult addLimitOrder(int id, int traderId, bool buyOrSell, int shares, int price);

    // Add a market order: matches immediately against the best
    // available opposite-side liquidity until filled or the book
    // runs out; any unfilled remainder is simply dropped (does not rest).
    // Same STP behaviour as addLimitOrder.
    OrderResult addMarketOrder(int id, int traderId, bool buyOrSell, int shares);

    // Cancel a resting order by id. No-op (returns false) if unknown.
    bool cancelOrder(int id);

    // Modify a resting order's price and/or size. Implemented as
    // cancel + re-add (same traderId as the original order), so it
    // loses time priority at its price level (matches typical
    // exchange semantics for a price change). Rejected with
    // RejectedUnknownOrder if `id` isn't currently resting.
    OrderResult modifyOrder(int id, int newShares, int newPrice);

    bool hasBestBid() const { return !buyLimits.empty(); }
    bool hasBestAsk() const { return !sellLimits.empty(); }
    int bestBid() const { return buyLimits.begin()->first; }
    int bestAsk() const { return sellLimits.begin()->first; }

    // True if this id currently refers to a resting order. Used by
    // the benchmark to pick valid cancel/modify targets; generally
    // useful for callers too.
    bool isOrderActive(int id) const { return orderMap.count(id) > 0; }

    void printBook() const;

private:
    std::map<int, Limit*, std::greater<int>> buyLimits;   // highest first
    std::map<int, Limit*> sellLimits;                      // lowest first
    std::unordered_map<int, Limit*> buyLimitByPrice;       // O(1) existence check
    std::unordered_map<int, Limit*> sellLimitByPrice;
    std::unordered_map<int, Order*> orderMap;               // id -> Order

    Limit* getOrCreateLimit(bool buyOrSell, int price);
    void eraseLimitIfEmpty(Limit* limit, bool buyOrSell);

    // Full teardown: detach from its limit, erase that limit if it's
    // now empty, erase from orderMap, and delete. Safe to call any
    // time you're done with both the order AND (potentially) its limit.
    void removeOrderRecord(Order* order);

    // Partial teardown: detach from its limit and delete, but leave
    // the (possibly now-empty) Limit object alone. Used inside match()
    // where we're still iterating that Limit's queue or still holding
    // a raw pointer to it — the caller erases the limit itself once
    // it's fully done with that pointer.
    void freeOrderKeepLimit(Order* order);

    // Matches `incoming` against the opposite side, up to `priceLimit`
    // (ignored when unbounded==true, used for market orders). Mutates
    // incoming->shares down as fills occur, appends to `trades`, and
    // appends any STP-cancelled resting order ids to `stpCancelled`.
    void match(Order* incoming, bool unbounded, int priceLimit,
               std::vector<Trade>& trades, std::vector<int>& stpCancelled);
};
