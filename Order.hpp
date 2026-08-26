#pragma once

class Limit;

// A single order sitting in (or passing through) the book.
// Intrusive doubly-linked list node so a Limit can keep FIFO order
// without any extra container allocations.
struct Order {
    int idNumber;
    int traderId;     // owner of the order; used for self-trade prevention
    bool buyOrSell;   // true = buy, false = sell
    int shares;       // remaining (unfilled) shares
    int limitPrice;   // price the order was placed at
    Order* nextOrder;
    Order* prevOrder;
    Limit* parentLimit;

    Order(int id, int traderId_, bool buyOrSell_, int shares_, int limitPrice_)
        : idNumber(id),
          traderId(traderId_),
          buyOrSell(buyOrSell_),
          shares(shares_),
          limitPrice(limitPrice_),
          nextOrder(nullptr),
          prevOrder(nullptr),
          parentLimit(nullptr) {}
};
