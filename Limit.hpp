#pragma once
#include "Order.hpp"

// Represents a single price level. Holds every resting order at that
// price in FIFO (time priority) order via an intrusive doubly linked list.
class Limit {
public:
    int limitPrice;
    int size;         // number of orders resting at this price
    int totalVolume;  // sum of shares across those orders
    Order* headOrder; // oldest order (next to be matched)
    Order* tailOrder; // newest order

    explicit Limit(int price)
        : limitPrice(price), size(0), totalVolume(0),
          headOrder(nullptr), tailOrder(nullptr) {}

    bool empty() const { return size == 0; }

    // Append to the back of the FIFO queue.
    void pushOrder(Order* order) {
        order->parentLimit = this;
        order->nextOrder = nullptr;
        order->prevOrder = tailOrder;
        if (tailOrder) tailOrder->nextOrder = order;
        tailOrder = order;
        if (!headOrder) headOrder = order;
        ++size;
        totalVolume += order->shares;
    }

    // Detach an order from the FIFO queue (used on cancel/full fill).
    void removeOrder(Order* order) {
        if (order->prevOrder) order->prevOrder->nextOrder = order->nextOrder;
        else headOrder = order->nextOrder;

        if (order->nextOrder) order->nextOrder->prevOrder = order->prevOrder;
        else tailOrder = order->prevOrder;

        order->nextOrder = nullptr;
        order->prevOrder = nullptr;
        order->parentLimit = nullptr;
        --size;
        totalVolume -= order->shares;
    }
};
