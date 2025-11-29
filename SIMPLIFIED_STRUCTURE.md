# Simplified OrderBook Structure

## Overview
Consolidated the entire order management engine into **just 2 files** while maintaining full functionality including data fabric simulation.

---

## File Structure

```
OME_OrderBook/
├── include/
│   └── orderbook.h          # Single header with all classes
├── src/
│   ├── orderbook.cpp        # Implementation
│   └── main.cpp             # Demo with data fabric simulation
└── CMakeLists.txt
```

**Total: 2 main files (header + implementation) vs 9 files before!**

---

## What's in `orderbook.h`

All core functionality in one organized header:

### 1. **Order Struct**
```cpp
struct Order {
    uint64_t order_id;
    uint32_t price;
    uint32_t quantity;
    char side;         // 'B' or 'S'
    uint64_t timestamp;
    bool active;
}
```

### 2. **DataFabric Class** (FIFO Simulation)
```cpp
class DataFabric {
    std::queue<Chunk> fifo_;   // Simulates FPGA soft-core FIFO
    
    void write_chunk(const Chunk&);  // Producer (soft-core)
    bool read_chunk(Chunk&);         // Consumer (orderbook)
}
```
- **Purpose**: Simulates hardware fabric interface
- **Key Feature**: Allows chunked/fragmented data delivery

### 3. **ITCHParser Class** (Message Decoder)
```cpp
class ITCHParser {
    ParseResult parse_one(const vector<uint8_t>&);
}
```
- **Supports 3 message types**:
  - `'A'` - Add Order (26 bytes)
  - `'C'` - Cancel Order (9 bytes)
  - `'E'` - Execute Order (13 bytes)
- **Returns**: (bytes_consumed, parsed_data) or invalid if incomplete

### 4. **OrderBook Class** (Main Engine)
```cpp
class OrderBook {
    DataFabric& fabric_;              // Reference to FIFO
    vector<uint8_t> message_buffer_;  // Reassembly buffer
    ITCHParser parser_;               // Message decoder
    unordered_map<uint64_t, Order> orders_;  // Order storage
    
    void process();                   // Main tick - drains fabric
    bool add_order(const Order&);
    bool cancel_order(uint64_t);
    bool execute_order(uint64_t, uint32_t);
    const Order* find_order(uint64_t);
}
```

---

## Data Flow

```
┌──────────────────┐
│   Soft Core      │  (Simulated in main.cpp)
│   (FPGA)         │
└────────┬─────────┘
         │ write_chunk()
         ▼
┌──────────────────┐
│   DataFabric     │  std::queue<Chunk>
│   (FIFO Queue)   │  - Stores byte chunks
└────────┬─────────┘
         │ read_chunk()
         ▼
┌──────────────────┐
│   OrderBook      │
│   process()      │
└────────┬─────────┘
         │ append to buffer
         ▼
┌──────────────────┐
│ message_buffer_  │  vector<uint8_t>
│ (Reassembly)     │  - Accumulates bytes
└────────┬─────────┘
         │ parse_one()
         ▼
┌──────────────────┐
│   ITCHParser     │  Stateless decoder
│                  │  - Extracts fields
└────────┬─────────┘
         │ Order data
         ▼
┌──────────────────┐
│ orders_ map      │  unordered_map<id, Order>
│ (Storage)        │  - O(1) lookup
└──────────────────┘
```

---

## Key Features Retained

### ✅ **Data Fabric Simulation**
- FIFO queue simulates FPGA soft-core
- Supports chunked delivery (messages split across packets)
- Realistic transport layer model

### ✅ **Message Reassembly**
- Buffer accumulates bytes across chunks
- Parser checks for complete messages
- Handles fragmentation gracefully

### ✅ **ITCH Protocol Support**
- Add Order: Full order details
- Cancel Order: Mark inactive
- Execute Order: Partial fills supported

### ✅ **Event Callbacks**
- Downstream processors can register callbacks
- Notified on Add/Cancel/Execute events
- Ready for bid/ask aggregation layer

### ✅ **O(1) Order Lookup**
- Hash map for fast queries
- Active flag for soft deletes
- Scales well with order count

---

## Example Usage (from main.cpp)

```cpp
// 1. Create fabric and orderbook
DataFabric fabric;
OrderBook orderbook(fabric);

// 2. Register callback
orderbook.set_event_callback([](char type, const Order& order) {
    std::cout << "Event: " << type << " - Order " << order.order_id << "\n";
});

// 3. Build ITCH message (simulates soft-core)
auto msg = MessageBuilder::build_add_order(12345, 10000, 50, 'B', 1000000);

// 4. Simulate chunked delivery
Chunk c1(msg.begin(), msg.begin() + 10);  // First 10 bytes
Chunk c2(msg.begin() + 10, msg.end());    // Remaining bytes

// 5. Send chunks through fabric
fabric.write_chunk(c1);
orderbook.process();  // Not enough data yet

fabric.write_chunk(c2);
orderbook.process();  // Complete message parsed!

// 6. Query orders
const Order* order = orderbook.find_order(12345);
```

---

## Demo Output

```
=== OrderBook with Data Fabric Simulation ===

--- Test 1: Add Orders (with chunking) ---
After chunk 1: 0 orders
[EVENT] ADD - Order 12345 | Price: 10000 | Qty: 50 | Side: B | Active: Yes
After chunk 2: 1 orders

--- Test 2: Execute Partial Order ---
[EVENT] EXECUTE - Order 12345 | Price: 10000 | Qty: 30 | Side: B | Active: Yes
Order 12345 after execution: qty=30

--- Test 3: Cancel Order ---
[EVENT] CANCEL - Order 12346 | Price: 10050 | Qty: 100 | Side: S | Active: No
After cancel: 1 active orders

--- Final OrderBook State ---
OrderBook: 6 active orders
     OrderID     Price  Quantity  Side    Active
----------------------------------------------------------
       12345     10000        30     B       Yes
       20000      9900        10     B       Yes
       ...
```

---

## Comparison: Before vs After

| Aspect | Before | After |
|--------|--------|-------|
| **Header Files** | 9 files | 1 file |
| **Source Files** | 3 files | 2 files |
| **Total LOC** | ~400 lines | ~350 lines |
| **Organization** | Scattered | Consolidated |
| **Functionality** | Same | Same + better |
| **Maintainability** | Complex | Simple |

---

## Benefits of Simplified Structure

### 1. **Easier to Understand**
- All related code in one place
- Clear data flow
- No jumping between files

### 2. **Easier to Maintain**
- Fewer files to manage
- Changes localized
- Less coupling

### 3. **Easier to Extend**
- Add new message types in ITCHParser
- Add order operations in OrderBook
- Everything visible

### 4. **Still Modular**
- Clear class boundaries
- Each class has single responsibility
- Easy to test

---

## What Was Removed

❌ Removed (redundant):
- `order_event.h` - Merged into Order struct
- `order_record.h` - Same as Order
- `order_table.h` - Merged into OrderBook
- `data_fabric_interface.h` - Moved to orderbook.h
- `message_buffer.h` - Simple vector in OrderBook
- `mock_itch_parser.h` - Now ITCHParser in orderbook.h
- `order_management_engine.h` - Merged into OrderBook

✅ Kept (essential):
- Data fabric simulation (DataFabric class)
- ITCH parsing (ITCHParser class)
- Order storage (OrderBook class)
- Message reassembly (message_buffer_)
- Event callbacks
- All functionality!

---

## Next Steps

### Easy Extensions:

1. **Add More ITCH Message Types**
   - Modify Order ('M')
   - Delete Order ('D')
   - Trade ('T')

2. **Add Bid/Ask Aggregation**
   - Price-level book
   - Best bid/offer tracking
   - Market depth

3. **Add Performance Monitoring**
   - Message processing time
   - Throughput metrics
   - Latency tracking

4. **Add Persistence**
   - Save/load orderbook state
   - Message replay
   - Historical analysis

---

## Build & Run

```bash
cd /home/chezboy/school_work/CAPSTONE/OME_OrderBook
mkdir -p build && cd build
cmake ..
make
./orderbook_main
```

---

## Summary

**From 9 header files → 1 header file**
**Same functionality, simpler structure, easier to work with!**

The data fabric simulation is still fully functional, demonstrating realistic FPGA soft-core → FIFO → orderbook data flow with chunked message delivery.
