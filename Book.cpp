#include "Book.hpp"
#include <iostream>
#include <algorithm>

Book::~Book() {
    for (auto& kv : orderMap) delete kv.second;
    for (auto& kv : buyLimitByPrice) delete kv.second;
    for (auto& kv : sellLimitByPrice) delete kv.second;
}

Limit* Book::getOrCreateLimit(bool buyOrSell, int price) {
    auto& byPrice = buyOrSell ? buyLimitByPrice : sellLimitByPrice;
    auto it = byPrice.find(price);
    if (it != byPrice.end()) return it->second;

    Limit* limit = new Limit(price);
    byPrice[price] = limit;
    if (buyOrSell) buyLimits[price] = limit;
    else sellLimits[price] = limit;
    return limit;
}

void Book::eraseLimitIfEmpty(Limit* limit, bool buyOrSell) {
    if (!limit->empty()) return;
    int price = limit->limitPrice;
    if (buyOrSell) {
        buyLimits.erase(price);
        buyLimitByPrice.erase(price);
    } else {
        sellLimits.erase(price);
        sellLimitByPrice.erase(price);
    }
    delete limit;
}

void Book::removeOrderRecord(Order* order) {
    Limit* limit = order->parentLimit;
    bool side = order->buyOrSell;
    if (limit) {
        limit->removeOrder(order);
        eraseLimitIfEmpty(limit, side);
    }
    orderMap.erase(order->idNumber);
    delete order;
}

void Book::freeOrderKeepLimit(Order* order) {
    if (order->parentLimit) order->parentLimit->removeOrder(order);
    orderMap.erase(order->idNumber);
    delete order;
}

void Book::match(Order* incoming, bool unbounded, int priceLimit,
                  std::vector<Trade>& trades, std::vector<int>& stpCancelled) {
    bool buy = incoming->buyOrSell;

    if (buy) {
        while (incoming->shares > 0 && !sellLimits.empty()) {
            auto bestIt = sellLimits.begin();
            int bestPrice = bestIt->first;
            if (!unbounded && bestPrice > priceLimit) break;

            // Own the Limit* for this whole level; only freeOrderKeepLimit
            // is used inside the loop so `level` itself is never dangling
            // until we explicitly erase it below.
            Limit* level = bestIt->second;
            while (incoming->shares > 0 && level->headOrder) {
                Order* resting = level->headOrder;

                // Self-trade prevention: cancel the resting order
                // instead of trading against it, then keep matching
                // against whatever's next in this level's queue.
                if (resting->traderId == incoming->traderId) {
                    stpCancelled.push_back(resting->idNumber);
                    freeOrderKeepLimit(resting);
                    continue;
                }

                int traded = std::min(incoming->shares, resting->shares);
                trades.push_back({incoming->idNumber, resting->idNumber, bestPrice, traded});

                incoming->shares -= traded;
                resting->shares -= traded;
                level->totalVolume -= traded;

                if (resting->shares == 0) freeOrderKeepLimit(resting);
            }
            eraseLimitIfEmpty(level, false); // safe: level was never freed above
        }
    } else {
        while (incoming->shares > 0 && !buyLimits.empty()) {
            auto bestIt = buyLimits.begin();
            int bestPrice = bestIt->first;
            if (!unbounded && bestPrice < priceLimit) break;

            Limit* level = bestIt->second;
            while (incoming->shares > 0 && level->headOrder) {
                Order* resting = level->headOrder;

                if (resting->traderId == incoming->traderId) {
                    stpCancelled.push_back(resting->idNumber);
                    freeOrderKeepLimit(resting);
                    continue;
                }

                int traded = std::min(incoming->shares, resting->shares);
                trades.push_back({resting->idNumber, incoming->idNumber, bestPrice, traded});

                incoming->shares -= traded;
                resting->shares -= traded;
                level->totalVolume -= traded;

                if (resting->shares == 0) freeOrderKeepLimit(resting);
            }
            eraseLimitIfEmpty(level, true);
        }
    }
}

OrderResult Book::addLimitOrder(int id, int traderId, bool buyOrSell, int shares, int price) {
    if (shares <= 0) return {OrderStatus::RejectedInvalidShares, {}, {}, false};
    if (price <= 0) return {OrderStatus::RejectedInvalidPrice, {}, {}, false};
    if (orderMap.count(id)) return {OrderStatus::RejectedDuplicateId, {}, {}, false};

    Order* incoming = new Order(id, traderId, buyOrSell, shares, price);

    std::vector<Trade> trades;
    std::vector<int> stpCancelled;
    match(incoming, /*unbounded=*/false, price, trades, stpCancelled);

    bool resting = incoming->shares > 0;
    if (resting) {
        Limit* level = getOrCreateLimit(buyOrSell, price);
        level->pushOrder(incoming);
        orderMap[id] = incoming;
    } else {
        delete incoming; // fully filled, never rested
    }
    return {OrderStatus::Accepted, std::move(trades), std::move(stpCancelled), resting};
}

OrderResult Book::addMarketOrder(int id, int traderId, bool buyOrSell, int shares) {
    if (shares <= 0) return {OrderStatus::RejectedInvalidShares, {}, {}, false};
    if (orderMap.count(id)) return {OrderStatus::RejectedDuplicateId, {}, {}, false};

    Order incoming(id, traderId, buyOrSell, shares, 0);
    std::vector<Trade> trades;
    std::vector<int> stpCancelled;
    match(&incoming, /*unbounded=*/true, 0, trades, stpCancelled);
    // Market orders never rest; any unfilled remainder is simply dropped.
    return {OrderStatus::Accepted, std::move(trades), std::move(stpCancelled), false};
}

bool Book::cancelOrder(int id) {
    auto it = orderMap.find(id);
    if (it == orderMap.end()) return false;
    removeOrderRecord(it->second);
    return true;
}

OrderResult Book::modifyOrder(int id, int newShares, int newPrice) {
    auto it = orderMap.find(id);
    if (it == orderMap.end()) return {OrderStatus::RejectedUnknownOrder, {}, {}, false};

    bool side = it->second->buyOrSell;
    int traderId = it->second->traderId;
    cancelOrder(id);
    return addLimitOrder(id, traderId, side, newShares, newPrice);
}

void Book::printBook() const {
    std::cout << "--- ASKS (low->high) ---\n";
    for (auto it = sellLimits.rbegin(); it != sellLimits.rend(); ++it) {
        std::cout << "  " << it->first << " x " << it->second->totalVolume
                  << " (" << it->second->size << " orders)\n";
    }
    std::cout << "--- BIDS (high->low) ---\n";
    for (auto& kv : buyLimits) {
        std::cout << "  " << kv.first << " x " << kv.second->totalVolume
                  << " (" << kv.second->size << " orders)\n";
    }
}
