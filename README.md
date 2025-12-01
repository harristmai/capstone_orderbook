# Capstone OrderBook

High-performance order management engine with FPGA-ready NASDAQ ITCH 5.0 protocol support and price-level aggregation.

## Features

- **NASDAQ ITCH 5.0 Protocol**: Full support for Add (A), Execute (E), Cancel (X), and Replace (U) messages
- **AXI-Stream FIFO Simulation**: Hardware-accurate backpressure with configurable depth (256B-4KB)
- **Price-Level Aggregation**: OrderBookEngine with best bid/ask and market depth queries
- **Message Reassembly**: Handles fragmented message delivery across chunks
- **O(1) Order Operations**: Hash map-based order storage with soft deletes
- **Error Handling**: Buffer overflow protection, unknown message detection, invalid operation tracking
- **Dual Logging**: Console and file output for verification testing
- **Event-Driven Architecture**: Callback system for downstream processing

## Structure

```
capstone_orderbook/
├── include/
│   ├── orderbook.h          # OrderBook, DataFabric, ITCHParser
│   └── bid_ask.h            # OrderBookEngine, price-level matching
├── src/
│   ├── orderbook.cpp        # Order management implementation
│   ├── bid_ask.cpp          # Bid/ask aggregation implementation
│   └── main.cpp             # Verification test suite
├── debug/
│   └── orderbook_verification_test_results.log  # Test output
├── CMakeLists.txt           # Build configuration
└── README.md                # This file
```

## Building

```bash
# Configure and build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release

# Run verification tests
cd Release
./orderbook_main.exe

# View results
type ..\..\debug\orderbook_verification_test_results.log
```

**Build Output:**
- `build/Release/orderbook_main.exe` - Verification test executable
- `debug/orderbook_verification_test_results.log` - Test results and logs

## Requirements

- **Compiler**: MSVC 19.44+ / GCC 7+ / Clang 5+
- **C++ Standard**: C++17
- **CMake**: 3.15+
- **Platform**: Windows (primary), Linux (supported)

## Quick Example

```cpp
// Create FIFO with backpressure (256-byte depth)
DataFabric fabric(256);
OrderBook orderbook(fabric);

// Register callback for order events
orderbook.set_event_callback([](char type, const Order& order) {
    std::cout << "[EVENT] " << (type == 'A' ? "ADD" : 
                                 type == 'E' ? "EXECUTE" : 
                                 type == 'X' ? "CANCEL" : "REPLACE")
              << " - Order " << order.order_id 
              << " | Price: " << order.price 
              << " | Qty: " << order.quantity << "\n";
});

// Build ITCH 5.0 Add Order message
auto msg = MessageBuilder::build_add_order(
    12345,   // order_id
    10000,   // price (basis points)
    50,      // quantity
    'B',     // side (Buy)
    1000000  // timestamp
);

// Send through FIFO (with backpressure check)
if (fabric.write_chunk(msg)) {
    orderbook.process();  // Parse and store order
}

// Query best bid/ask
uint64_t bid_price, bid_qty, ask_price, ask_qty;
orderbook.get_best_bid(bid_price, bid_qty);
orderbook.get_best_ask(ask_price, ask_qty);

// Get market depth (top 5 levels)
auto depth = orderbook.get_depth(5);
```

## ITCH 5.0 Message Format

### Add Order ('A') - 36 bytes (NASDAQ ITCH 5.0 Spec)
```
[A:1][Stock Locate:2][Tracking:2][Timestamp:6][Order ID:8][Side:1]
[Shares:4][Stock:8][Price:4]
```

### Order Cancel ('X') - 23 bytes
```
[X:1][Stock Locate:2][Tracking:2][Timestamp:6][Order ID:8][Cancelled Shares:4]
```

### Order Executed ('E') - 31 bytes
```
[E:1][Stock Locate:2][Tracking:2][Timestamp:6][Order ID:8]
[Executed Shares:4][Match Number:8]
```

### Order Replace ('U') - 35 bytes
```
[U:1][Stock Locate:2][Tracking:2][Timestamp:6][Original Order ID:8]
[New Order ID:8][Shares:4][Price:4]
```

**Encoding:** Little-endian (x86-optimized)  
**Prices:** Basis points (10000 = $100.00)  
**Timestamps:** Nanoseconds since midnight

## Components

### DataFabric (AXI-Stream FIFO)
- **Configurable depth**: 256B-4KB (default 4KB)
- **Backpressure simulation**: TREADY/TVALID protocol
- **Flow control statistics**: Utilization, backpressure events, high-water mark
- **Purpose**: Models FPGA soft-core to processor DMA transfers

### ITCHParser (NASDAQ ITCH 5.0)
- **Stateless design**: Thread-safe, zero-copy validation
- **Message types**: Add (A), Execute (E), Cancel (X), Replace (U)
- **Error handling**: Unknown message detection, buffer overflow protection
- **Format**: Little-endian binary (x86-optimized)

### OrderBook (Main Engine)
- **Order operations**: Add, cancel, execute, replace with O(1) lookup
- **Message buffer**: Handles fragmented delivery with reassembly
- **Soft deletes**: Maintains order history via active flag
- **Error tracking**: Unknown messages, buffer overflows, invalid operations
- **Event callbacks**: Notifies downstream processors on state changes

### OrderBookEngine (Price-Level Aggregation)
- **Dual-sided book**: Separate bid and ask price-level maps
- **FIFO price-time priority**: Maintains order queue at each price level
- **Market data queries**: Best bid/ask, spread calculation, market depth (top-K)
- **Incremental updates**: O(log P) operations where P = number of price levels
- **Memory efficient**: Aggregates quantities, removes empty levels

## Verification Tests

The test suite (`src/main.cpp`) validates:

1. **OB-1: ITCH 5.0 Protocol Compliance** - Parse all 4 message types
2. **OB-4: Unknown Message Type Handling** - Error detection and recovery
3. **OB-6: Invalid Operation Handling** - Cancel/execute validation
4. **OB-7: FIFO Backpressure** - AXI-Stream flow control
5. **OB-8: Best Bid/Ask Calculation** - Top-of-book accuracy
6. **OB-9: Market Depth Aggregation** - Price-level depth queries

**Test Coverage:** 100% (6/6 tests passed)

**Log Output:** `debug/orderbook_verification_test_results.log` (4,799 bytes)

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|----------------|-------|
| Add Order | O(1) + O(log P) | Hash insert + price-level add |
| Cancel Order | O(1) + O(log P) | Hash lookup + level remove |
| Execute Order | O(1) + O(log P) | Hash update + quantity adjust |
| Find Order | O(1) | Hash map lookup |
| Best Bid/Ask | O(1) | First element in sorted map |
| Market Depth (K levels) | O(K) | Iterate top K price levels |

**P** = Number of active price levels (typically << total orders)

### Memory Usage
- **Per Order**: ~48 bytes (Order struct) + ~40 bytes (hash map overhead) + ~32 bytes (OrderInfo) = ~120 bytes
- **Per Price Level**: ~56 bytes (PriceLevel) + map overhead
- **FIFO Buffer**: Configurable (default 4KB)

### Throughput Targets
- **Message processing**: > 1M messages/second
- **Order lookup**: > 10M lookups/second
- **Latency (avg)**: < 500 nanoseconds per message

## Test Results Summary

```
=== Verification Test Results ===
Test Run Date: 2025-11-30

Test OB-1: ITCH 5.0 Protocol Compliance          ✓ PASS
Test OB-4: Unknown Message Type Handling          ✓ PASS
Test OB-6: Invalid Operation Handling             ✓ PASS
Test OB-7: FIFO Backpressure (AXI-Stream)        ✓ PASS
Test OB-8: Best Bid/Ask Calculation               ✓ PASS
Test OB-9: Market Depth Aggregation               ✓ PASS

Overall: 6/6 Tests Passed (100%)

FIFO Statistics (256-byte test):
  - Utilization: 98.44% (252/256 bytes)
  - Backpressure events: 13
  - Messages written: 7/20 (13 dropped due to backpressure)

Error Handling:
  - Unknown message types detected: 4
  - Buffer overflows prevented: 1
  - Invalid operations rejected: 3
```

Detailed results in: `debug/orderbook_verification_test_results.log`

## API Reference

### OrderBook Class

```cpp
// Main processing loop
void process();  // Drain FIFO, parse messages, update orders

// Order operations
bool add_order(const Order& order);
bool cancel_order(uint64_t order_id);
bool execute_order(uint64_t order_id, uint32_t quantity);
bool replace_order(uint64_t old_id, uint64_t new_id, uint32_t price, uint32_t qty);

// Queries
const Order* find_order(uint64_t order_id) const;
size_t get_active_order_count() const;

// Market data
bool get_best_bid(uint64_t& price_out, uint64_t& qty_out) const;
bool get_best_ask(uint64_t& price_out, uint64_t& qty_out) const;
bool get_spread(uint64_t& spread_out) const;
MarketDepth get_depth(size_t levels) const;

// Event callbacks
void set_event_callback(EventCallback cb);

// Error statistics
const ErrorStats& get_error_stats() const;
```

### DataFabric Class

```cpp
// Constructor with FIFO depth
DataFabric(size_t max_depth = 4096);

// Write with backpressure (returns false if FIFO full)
bool write_chunk(const Chunk& chunk);

// Read chunk from FIFO
bool read_chunk(Chunk& out);

// Status queries
bool empty() const;
bool full() const;
size_t depth_bytes() const;
float utilization() const;

// Statistics
const FIFOStats& get_stats() const;
```

## FPGA Integration Notes

This design is **FPGA-ready** with the following considerations:

1. **AXI-Stream Interface**: DataFabric models TVALID/TREADY backpressure
2. **Fixed Message Sizes**: All ITCH messages have known lengths (36/23/31/35 bytes)
3. **No Dynamic Allocation**: Order table size bounded by session requirements
4. **Little-Endian**: Matches x86, can be adapted for big-endian FPGA fabrics
5. **Stateless Parser**: ITCHParser can be implemented in hardware state machine

**Next Steps for FPGA:**
- Replace DataFabric with DMA driver (AXI-DMA or similar)
- Implement ITCHParser in Verilog/VHDL
- Consider hardware-accelerated order table (BRAM-based CAM)
- Add hardware timestamp capture for sub-microsecond latency

## Future Enhancements

- [ ] Lock-free data structures for multi-threading
- [ ] Memory-mapped persistence (WAL for crash recovery)
- [ ] Additional ITCH 5.0 message types (Trade, NOII, LULD)
- [ ] Performance benchmarking suite (Google Benchmark)
- [ ] Unit test framework (Google Test)
- [ ] Hardware matching engine integration

## Documentation

All code is inline-documented with detailed comments. Key files:
- `include/orderbook.h` - Main API and data structures
- `include/bid_ask.h` - Price-level aggregation engine
- `src/main.cpp` - Comprehensive test examples

## License

[To be determined]

## Author

Capstone Project - Order Management Engine  
FPGA-Ready High-Frequency Trading System
