# TradeCore

A C++17 limit order book and matching engine implementing price-time priority — the core matching logic behind any exchange or trading venue.

## Features

- **Limit orders** — rest in the book if they don't cross, or match immediately against the best opposite price(s) if they do, with strict price-time priority (best price first, FIFO within a price level).
- **Market orders** — sweep the best available opposite-side liquidity until filled or the book runs out; any unfilled remainder is dropped rather than resting.
- **Cancel** — remove a resting order by ID in O(1).
- **Modify** — change a resting order's price/size (cancel + re-add under the hood, so it loses time priority at its new price — standard exchange behavior for a price change).
- **Self-trade prevention** — an incoming order that would match against a resting order from the same trader cancels that resting order instead of trading against it, then continues matching against the rest of the queue.
- **Input validation** — non-positive shares/price and duplicate order IDs are rejected with an explicit status rather than silently ignored.

## Architecture

- `Order.hpp` — a single order; intrusive doubly-linked list node so a price level can maintain FIFO order with no extra container overhead.
- `Limit.hpp` — one price level: a FIFO queue of orders plus aggregate size/volume.
- `Book.hpp` / `Book.cpp` — the matching engine:
  - Two ordered maps (one per side), so the best bid/ask is always an O(log M) lookup, M = number of distinct price levels.
  - A hash map per side for O(1) "does a limit exist at this price" checks.
  - A hash map from order ID to order for O(1) cancellation.
- `main.cpp` — a walkthrough demo: resting orders, a crossing limit order, cancellation, self-trade prevention, a market sweep, a modify, and rejected/invalid requests.
- `Benchmark.cpp` — a throughput/latency harness for the engine (see below).

## Building

```bash
g++ -std=c++17 -O2 main.cpp Book.cpp -o order_book
g++ -std=c++17 -O2 Benchmark.cpp Book.cpp -o benchmark
```

```bash
./order_book
./benchmark
```

## Benchmarking

`Benchmark.cpp` warms the book up with two-sided resting liquidity, then fires a large mixed stream of adds, cancels, modifies, and market orders at it, timing each request individually. It reports latency percentiles per request type and writes every sample to `benchmark_latencies.csv` for further analysis. On a single-threaded, mixed workload the engine sustains on the order of **millions of requests per second** with sub-microsecond average latency — see the script itself for exact methodology if you want to reproduce it.

## Sample output
<img width="966" height="530" alt="image" src="https://github.com/user-attachments/assets/96149c2f-2cb9-41b4-b541-7530c692f80f" />


## Known limitations

- Single-threaded, no concurrency support.
- No stop or stop-limit order types.
- Uses `std::map` rather than a hand-rolled balanced tree — same complexity guarantees, less custom code.
- No custom memory pool/allocator.

## License

MIT — see `LICENSE`.
