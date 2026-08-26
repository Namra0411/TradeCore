#include <iostream>
#include "Book.hpp"

static const char* statusName(OrderStatus s) {
    switch (s) {
        case OrderStatus::Accepted: return "Accepted";
        case OrderStatus::RejectedInvalidShares: return "RejectedInvalidShares";
        case OrderStatus::RejectedInvalidPrice: return "RejectedInvalidPrice";
        case OrderStatus::RejectedDuplicateId: return "RejectedDuplicateId";
        case OrderStatus::RejectedUnknownOrder: return "RejectedUnknownOrder";
    }
    return "?";
}

static void printResult(const OrderResult& r) {
    std::cout << "  status: " << statusName(r.status) << "\n";
    for (const auto& t : r.trades) {
        std::cout << "  TRADE buy#" << t.buyOrderId << " x sell#" << t.sellOrderId
                  << " @ " << t.price << " for " << t.shares << " shares\n";
    }
    for (int id : r.selfTradeCancelledIds) {
        std::cout << "  STP cancelled resting order #" << id << "\n";
    }
}

int main() {
    Book book;

    std::cout << "Resting sell 100 (trader 1), 10 @ 101\n";
    printResult(book.addLimitOrder(1, /*traderId=*/1, false, 10, 101));

    std::cout << "Resting sell 100 (trader 1), 5 @ 102\n";
    printResult(book.addLimitOrder(2, /*traderId=*/1, false, 5, 102));

    std::cout << "Resting buy 100 (trader 2), 8 @ 99\n";
    printResult(book.addLimitOrder(3, /*traderId=*/2, true, 8, 99));

    book.printBook();

    std::cout << "\nAggressive buy limit (trader 3), 6 @ 101 (should cross and trade)\n";
    printResult(book.addLimitOrder(4, /*traderId=*/3, true, 6, 101));
    book.printBook();

    std::cout << "\nInput validation: duplicate id 4\n";
    printResult(book.addLimitOrder(4, /*traderId=*/3, true, 1, 100));

    std::cout << "\nInput validation: zero shares\n";
    printResult(book.addLimitOrder(10, /*traderId=*/3, true, 0, 100));

    std::cout << "\nInput validation: negative price\n";
    printResult(book.addLimitOrder(11, /*traderId=*/3, true, 5, -5));

    std::cout << "\nSelf-trade prevention: trader 1's own aggressive buy hits trader 1's resting ask #2\n";
    printResult(book.addLimitOrder(12, /*traderId=*/1, true, 5, 102));
    book.printBook();

    std::cout << "\nMarket buy (trader 5) for 6 shares (sweeps remaining ask liquidity)\n";
    printResult(book.addMarketOrder(5, /*traderId=*/5, true, 6));
    book.printBook();

    std::cout << "\nCancel order 3\n";
    book.cancelOrder(3);
    book.printBook();

    std::cout << "\nModify: add resting sell (trader 4) 15 @ 110, then move it to 108\n";
    book.addLimitOrder(6, /*traderId=*/4, false, 15, 110);
    printResult(book.modifyOrder(6, 15, 108));
    book.printBook();

    std::cout << "\nModify unknown order id 999\n";
    printResult(book.modifyOrder(999, 5, 100));

    return 0;
}
