# Quick Reference: Simplified OrderBook

## File Structure (Final)
```
include/
  └── orderbook.h         ← ALL classes here

src/
  ├── orderbook.cpp       ← Implementation
  └── main.cpp            ← Demo with data fabric
```

## Classes in orderbook.h

### 1. Order (Struct)
```cpp
struct Order {
    uint64_t order_id, timestamp;
    uint32_t price, quantity;
    char side;  // 'B' or 'S'
    bool active;
};
```

### 2. DataFabric (FIFO Simulation)
```cpp
class DataFabric {
    std::queue<Chunk> fifo_;
public:
    void write_chunk(const Chunk&);  // Soft-core writes
    bool read_chunk(Chunk&);         // OrderBook reads
};
```

### 3. ITCHParser (Message Decoder)
```cpp
class ITCHParser {
public:
    ParseResult parse_one(const vector<uint8_t>&);
};
```
**Supported Messages:**
- `'A'` - Add (26 bytes)
- `'C'` - Cancel (9 bytes)  
- `'E'` - Execute (13 bytes)

### 4. OrderBook (Main Engine)
```cpp
class OrderBook {
    DataFabric& fabric_;
    vector<uint8_t> message_buffer_;
    ITCHParser parser_;
    unordered_map<uint64_t, Order> orders_;
public:
    void process();                          // Main loop
    bool add_order(const Order&);
    bool cancel_order(uint64_t);
    bool execute_order(uint64_t, uint32_t);
    const Order* find_order(uint64_t);
};
```

## Data Structures

| Class | Internal Data Structure | Purpose |
|-------|------------------------|---------|
| DataFabric | `queue<Chunk>` | FIFO for byte chunks |
| ITCHParser | (none - stateless) | Byte decoder |
| OrderBook | `vector<uint8_t>` | Message reassembly |
| OrderBook | `unordered_map<uint64_t, Order>` | Order storage |

## Message Format (Little-Endian)

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

## How Data Gets Populated

### 1. Soft-Core Sends Data
```cpp
fabric.write_chunk(chunk);  // → FIFO queue
```

### 2. OrderBook Processes
```cpp
orderbook.process();
// 1. Drains chunks from fabric.fifo_ → message_buffer_
// 2. Parses complete messages from buffer
// 3. Updates orders_ hash map
// 4. Removes consumed bytes from buffer
```

### 3. Data Flow Path
```
Chunk → FIFO → Buffer → Parser → Order → HashMap
```

## Usage Example

```cpp
// Setup
DataFabric fabric;
OrderBook book(fabric);

// Build message (simulates soft-core)
auto msg = MessageBuilder::build_add_order(
    12345,  // order_id
    10000,  // price
    50,     // quantity
    'B',    // side
    1000000 // timestamp
);

// Send (can be chunked!)
fabric.write_chunk(msg);

// Process
book.process();  // Parses and stores order

// Query
const Order* o = book.find_order(12345);
```

## Key Features

✅ **Data Fabric Simulation** - FIFO queue like FPGA  
✅ **Chunked Delivery** - Messages can span multiple packets  
✅ **Message Reassembly** - Buffer accumulates bytes  
✅ **ITCH Protocol** - Add/Cancel/Execute messages  
✅ **O(1) Lookup** - Hash map for orders  
✅ **Event Callbacks** - Notify downstream processors  
✅ **Partial Execution** - Reduce order quantity  

## Build & Run

```bash
cd build
cmake ..
make
./orderbook_main
```

## Extending

### Add New Message Type
1. Add constant in ITCHParser
2. Add case in parse_one()
3. Add handler in OrderBook::handle_message()

### Add Bid/Ask Aggregation
1. Create new class in orderbook.h
2. Register as event callback
3. Aggregate by price level

**Simple, organized, and ready to extend!**
