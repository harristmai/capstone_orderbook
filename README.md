# Capstone OrderBook

High-performance order management engine with FPGA soft-core data fabric simulation.

## Features

- **Data Fabric Simulation**: FIFO queue simulating FPGA soft-core transport
- **ITCH Protocol Parser**: Supports Add, Cancel, and Execute messages
- **Message Reassembly**: Handles chunked/fragmented message delivery
- **O(1) Order Lookup**: Hash map-based order storage
- **Event-Driven**: Callback system for downstream processing
- **Clean Architecture**: Just 2 main files (header + implementation)

## Structure

```
├── include/
│   └── orderbook.h          # All class declarations
├── src/
│   ├── orderbook.cpp        # Implementation
│   └── main.cpp             # Demo with data fabric simulation
├── benchmarks/
│   └── benchmarks_ome.cpp   # Performance benchmarks
├── CMakeLists.txt
└── docs/                    # Design documentation
```

## Building

```bash
mkdir build && cd build
cmake ..
make
./orderbook_main
```

## Requirements

- C++17 or later
- CMake 3.15+
- GCC 15.2.1 or compatible compiler

## Quick Example

```cpp
// Create data fabric (simulates FPGA soft-core)
DataFabric fabric;
OrderBook orderbook(fabric);

// Register callback for events
orderbook.set_event_callback([](char type, const Order& order) {
    std::cout << "Event: " << type << " - Order " << order.order_id << "\n";
});

// Build and send ITCH message
auto msg = MessageBuilder::build_add_order(12345, 10000, 50, 'B', 1000000);
fabric.write_chunk(msg);

// Process messages
orderbook.process();

// Query orders
const Order* order = orderbook.find_order(12345);
```

## ITCH Message Format

### Add Order ('A') - 26 bytes
```
[A][order_id:8][price:4][qty:4][side:1][timestamp:8]
```

### Cancel Order ('C') - 9 bytes
```
[C][order_id:8]
```

### Execute Order ('E') - 13 bytes
```
[E][order_id:8][qty:4]
```

## Components

### DataFabric
- FIFO queue for byte chunks
- Simulates FPGA soft-core transport layer
- Supports chunked message delivery

### ITCHParser
- Stateless message decoder
- Handles little-endian binary format
- Returns parsed order data

### OrderBook
- Main order management engine
- Message reassembly buffer
- Hash map order storage
- Event notification system

## Performance

- **O(1)** order lookup by ID
- **O(log P)** bid/ask aggregation (P = price levels)
- Efficient incremental updates
- Minimal memory allocation

## Documentation

- `BID_ASK_PROCESSOR_DESIGN.md` - Bid/Ask aggregation design
- `SIMPLIFIED_STRUCTURE.md` - Architecture overview
- `QUICK_REFERENCE.md` - API reference
- `CLEANUP_SUMMARY.md` - Design decisions

## License

[Add your license here]

## Author

[Your name/info]
