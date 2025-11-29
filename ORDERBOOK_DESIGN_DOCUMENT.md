# OrderBook Component - Design Document

## 1. Executive Summary

This document describes the design of the OrderBook component, which serves as the first critical block in a high-frequency trading Order Management Engine (OME). The OrderBook component receives market data messages from a simulated FPGA data fabric, parses ITCH protocol messages, and maintains an in-memory order table with O(1) lookup performance.

**Version:** 1.0  
**Date:** November 28, 2025  
**Component:** OrderBook Block  
**Related Blocks:** Price Level Aggregation (future), Bid/Ask Processor (future)

---

## 2. Purpose

The purpose of this Design Document is to articulate the architectural and implementation choices made for the OrderBook component, linking these decisions to requirements for high-performance order management systems in financial trading environments.

### 2.1 Design Goals

1. **Low Latency Processing**: Minimize time from message receipt to order table update
2. **Reliable Message Handling**: Handle fragmented/chunked data delivery without message loss
3. **Scalability**: Support thousands of concurrent orders with O(1) lookup performance
4. **Extensibility**: Enable future integration with price level aggregation and matching engines
5. **Testability**: Provide clear interfaces for verification and validation

---

## 3. High-Level Architecture

### 3.1 System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    OrderBook Component                          │
│                                                                 │
│  ┌───────────────┐      ┌──────────────┐      ┌─────────────┐ │
│  │  DataFabric   │ ───> │ MessageBuffer│ ───> │ ITCHParser  │ │
│  │  (FIFO Queue) │      │  (Reassembly)│      │  (Decoder)  │ │
│  └───────────────┘      └──────────────┘      └─────────────┘ │
│         ▲                                            │          │
│         │                                            ▼          │
│  ┌──────┴────────┐                          ┌─────────────┐   │
│  │ Soft-Core     │                          │  OrderBook  │   │
│  │ (FPGA/Sim)    │                          │   Storage   │   │
│  └───────────────┘                          └─────────────┘   │
│                                                     │          │
│                                                     ▼          │
│                                             ┌─────────────┐   │
│                                             │  Callbacks  │   │
│                                             │ (Events Out)│   │
│                                             └─────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 Major Components

| Component | Responsibility | Technology Choice |
|-----------|---------------|-------------------|
| **DataFabric** | Simulate FPGA soft-core FIFO interface | `std::queue<Chunk>` |
| **MessageBuffer** | Reassemble fragmented messages | `std::vector<uint8_t>` |
| **ITCHParser** | Decode ITCH protocol messages | Stateless parser with struct return |
| **OrderBook** | Store and manage orders | `std::unordered_map<uint64_t, Order>` |
| **Order** | Represent individual order data | POD struct |

---

## 4. Detailed Component Design

### 4.1 Data Structures

#### 4.1.1 Order Structure

**Design Choice:** Plain Old Data (POD) struct

```cpp
struct Order {
    uint64_t order_id;      // Unique identifier
    uint32_t price;         // Price in fixed-point (e.g., cents)
    uint32_t quantity;      // Number of shares/contracts
    char side;              // 'B' (Buy) or 'S' (Sell)
    uint64_t timestamp;     // Event timestamp
    bool active;            // Soft-delete flag
};
```

**Design Rationale:**
- **POD Structure** enables efficient memory layout and cache performance
- **Fixed-width integers** (uint32_t, uint64_t) align with FPGA data types
- **Soft-delete flag** (`active`) avoids hash map reallocation overhead
- **Timestamp inclusion** supports future audit trail and time-priority matching

**Requirements Link:**
- NFR1: Low latency → cache-friendly data layout
- FR1: Order tracking → contains all essential order attributes
- FR3: Cancel operations → soft-delete via `active` flag

#### 4.1.2 DataFabric (FIFO Interface)

**Design Choice:** Queue-based FIFO with byte chunks

```cpp
class DataFabric {
    using Chunk = std::vector<uint8_t>;
    std::queue<Chunk> fifo_;
public:
    void write_chunk(const Chunk& chunk);
    bool read_chunk(Chunk& out);
    bool empty() const;
};
```

**Design Rationale:**
- **Simulates hardware FIFO** behavior for realistic testing
- **Chunk-based delivery** models DMA transfers from FPGA
- **Queue implementation** provides natural FIFO semantics
- **Move semantics** in `read_chunk()` eliminates unnecessary copies

**Requirements Link:**
- C1: FPGA integration → models soft-core to processor communication
- NFR2: Reliability → supports chunked/fragmented delivery
- NFR3: Testability → allows injection of test messages

**Alternative Considered:**
- **Lock-free queue** (boost::lockfree::queue)
  - **Rejected** for initial design: adds dependency, complexity
  - **Future consideration** for multi-threaded performance

#### 4.1.3 Message Buffer (Reassembly)

**Design Choice:** Dynamic vector with erase-from-front

```cpp
class OrderBook {
    std::vector<uint8_t> message_buffer_;
    
    void process() {
        // Append chunks to buffer
        message_buffer_.insert(message_buffer_.end(), chunk.begin(), chunk.end());
        
        // Parse and remove consumed bytes
        message_buffer_.erase(message_buffer_.begin(), 
                            message_buffer_.begin() + bytes_consumed);
    }
};
```

**Design Rationale:**
- **Vector over deque**: Simpler implementation, contiguous memory for parser
- **Erase-from-front**: Simple message consumption pattern
- **Dynamic sizing**: Handles variable-length message accumulation

**Requirements Link:**
- FR4: Message parsing → handles incomplete messages
- NFR2: Reliability → reassembles fragmented data
- NFR1: Performance → contiguous memory for parser efficiency

**Performance Trade-off:**
- `erase()` from front is O(n) but n is small (typical buffer < 1KB)
- Alternative: circular buffer (rejected for code simplicity in v1.0)

#### 4.1.4 ITCH Parser

**Design Choice:** Stateless parser with struct return

```cpp
class ITCHParser {
public:
    struct ParseResult {
        size_t bytes_consumed;
        bool valid;
        char type;           // 'A', 'C', 'E'
        uint64_t order_id;
        uint32_t price;
        uint32_t quantity;
        char side;
        uint64_t timestamp;
    };
    
    ParseResult parse_one(const std::vector<uint8_t>& buffer) const;
};
```

**Design Rationale:**
- **Stateless design** enables concurrent parsing (future multi-threading)
- **Struct return** with validity flag avoids exceptions in hot path
- **Fixed message sizes** enable fast length validation
- **Little-endian encoding** matches x86 architecture (optimization)

**Message Format:**

| Type | Size | Format |
|------|------|--------|
| Add Order ('A') | 26 bytes | type(1) + order_id(8) + price(4) + qty(4) + side(1) + ts(8) |
| Cancel Order ('C') | 9 bytes | type(1) + order_id(8) |
| Execute Order ('E') | 13 bytes | type(1) + order_id(8) + qty(4) |

**Requirements Link:**
- FR2: Add/Cancel/Execute → supports core ITCH message types
- NFR1: Performance → stateless, zero-copy validation
- FR4: Protocol compliance → adheres to ITCH-like format

**Alternative Considered:**
- **Exception-based parsing** (throw on invalid message)
  - **Rejected**: exceptions in hot path harm performance
  - **Chosen**: validity flag with struct return

#### 4.1.5 OrderBook (Main Storage)

**Design Choice:** Hash map with soft deletes

```cpp
class OrderBook {
    std::unordered_map<uint64_t, Order> orders_;
    
public:
    bool add_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    bool execute_order(uint64_t order_id, uint32_t quantity);
    const Order* find_order(uint64_t order_id) const;
};
```

**Design Rationale:**
- **`unordered_map`** provides O(1) average-case lookup/insert
- **Soft deletes** (active flag) avoid map rehashing on cancel
- **Pointer return** from `find_order()` avoids copy, null indicates not found
- **Const correctness** enables safe concurrent reads (future)

**Requirements Link:**
- FR1: Order storage → hash map for fast access
- NFR1: Performance → O(1) operations, no unnecessary copies
- FR3: Cancel → soft-delete avoids map modification
- FR5: Execute → in-place quantity update

**Performance Characteristics:**

| Operation | Time Complexity | Space Complexity |
|-----------|----------------|------------------|
| Add Order | O(1) average | O(1) per order |
| Cancel Order | O(1) average | O(1) - no deallocation |
| Execute Order | O(1) average | O(1) - in-place update |
| Find Order | O(1) average | O(1) |

**Alternative Considered:**
- **Array-based with free list** (common in HFT)
  - **Pros**: Better cache locality, predictable performance
  - **Cons**: Fixed capacity, complex memory management
  - **Future consideration** for FPGA implementation

### 4.2 Event-Driven Architecture

**Design Choice:** Callback-based event notification

```cpp
class OrderBook {
    using EventCallback = std::function<void(char type, const Order& order)>;
    EventCallback callback_;
    
public:
    void set_event_callback(EventCallback cb) {
        callback_ = std::move(cb);
    }
    
private:
    void handle_message(const ITCHParser::ParseResult& result) {
        // Update order state
        // ...
        
        // Notify callback
        if (callback_) {
            callback_(event_type, order);
        }
    }
};
```

**Design Rationale:**
- **Loose coupling** between OrderBook and downstream processors
- **Single callback** keeps interface simple for v1.0
- **Move semantics** avoids copy overhead
- **Null check** allows optional callback registration

**Requirements Link:**
- NFR4: Extensibility → enables future price level aggregation
- NFR3: Testability → callbacks can be mocked/instrumented
- C2: Architecture → supports pipeline design pattern

**Future Enhancement:**
- Multiple callback registration (observer pattern)
- Priority-based callback ordering
- Async callback execution (thread pool)

---

## 5. Processing Flow

### 5.1 Main Processing Loop

```
┌─────────────────────────────────────────────────────────────┐
│ OrderBook::process()                                        │
│                                                             │
│ 1. Drain FIFO                                              │
│    while (fabric.read_chunk(chunk))                        │
│       message_buffer_.insert(chunk)                        │
│                                                             │
│ 2. Parse Complete Messages                                 │
│    while (true)                                            │
│       result = parser.parse_one(message_buffer_)          │
│       if (!result.valid) break                            │
│                                                             │
│       3. Handle Message                                    │
│          switch (result.type)                              │
│             case 'A': add_order()                          │
│             case 'C': cancel_order()                       │
│             case 'E': execute_order()                      │
│                                                             │
│       4. Fire Callback                                     │
│          if (callback_)                                    │
│             callback_(type, order)                         │
│                                                             │
│       5. Consume Bytes                                     │
│          message_buffer_.erase(0, bytes_consumed)         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**Design Rationale:**
- **Drain-then-parse** pattern maximizes batch processing
- **Loop-until-incomplete** ensures all parseable messages processed
- **State update before callback** guarantees consistency
- **Byte consumption** prevents re-parsing

**Requirements Link:**
- NFR1: Performance → batch processing reduces overhead
- NFR2: Reliability → processes all available messages atomically

### 5.2 Message Fragmentation Handling

**Scenario:** 26-byte Add Order message split across 3 chunks

```
Chunk 1: [bytes 0-9]    → Buffer: [0-9]      → parse_one() = invalid
Chunk 2: [bytes 10-19]  → Buffer: [0-19]     → parse_one() = invalid
Chunk 3: [bytes 20-25]  → Buffer: [0-25]     → parse_one() = VALID!
                        → Buffer: []          → message consumed
```

**Design Rationale:**
- **Accumulation strategy** handles worst-case fragmentation
- **No timeout mechanism** (messages expected to complete quickly)
- **Buffer persistence** across `process()` calls

**Requirements Link:**
- FR4: Fragmentation → accumulates partial messages
- NFR2: Reliability → no message loss due to chunking

---

## 6. Technology Choices & Justifications

### 6.1 C++17 Standard

**Choice:** C++17 language standard

**Rationale:**
- **Structured bindings** improve readability (`auto [it, inserted] = map.emplace()`)
- **std::optional** enables clean optional return (future use)
- **Inline variables** support header-only components
- **Widely supported** by modern compilers (GCC 7+, Clang 5+, MSVC 2017+)

**Requirements Link:**
- C3: Toolchain compatibility → modern but stable standard
- NFR1: Performance → zero-cost abstractions

**Alternative Considered:**
- **C++20**: Rejected due to limited compiler support in embedded/FPGA environments
- **C++11**: Rejected due to missing quality-of-life features

### 6.2 Standard Library Containers

**Choice:** std::unordered_map, std::vector, std::queue

**Rationale:**
- **No external dependencies** simplifies deployment
- **Well-tested implementations** reduce defect risk
- **Predictable performance** characteristics
- **Portable** across platforms

**Requirements Link:**
- C4: Deployment → minimal dependencies
- NFR3: Testability → standard library has known behavior

**Future Consideration:**
- Custom allocators for deterministic memory behavior
- Lock-free containers for multi-threading

### 6.3 Little-Endian Byte Order

**Choice:** Little-endian encoding for ITCH messages

**Rationale:**
- **Matches x86 architecture** → no byte swapping needed
- **Simpler parser implementation** → direct memory cast (with care)
- **Fast serialization** in MessageBuilder

**Requirements Link:**
- NFR1: Performance → eliminates byte-order conversion
- C5: Platform target → x86-64 primary deployment

**Trade-off:**
- Network protocols typically use big-endian (rejected for performance)
- Custom format allows architectural optimization

### 6.4 Soft Delete (Active Flag)

**Choice:** Mark orders inactive rather than erasing from map

**Rationale:**
- **Avoids hash map rehashing** on delete (performance)
- **Preserves order history** for auditing
- **Simpler execution logic** (no need to handle missing orders)

**Requirements Link:**
- NFR1: Performance → eliminates deallocation overhead
- FR6: Audit trail → retains cancelled order data
- FR3: Cancel operation → O(1) flag update

**Trade-off:**
- Memory usage grows with total orders (acceptable for session-based trading)
- Periodic cleanup can be added if needed

---

## 7. Design Patterns Applied

### 7.1 Pipeline Pattern

**Application:** DataFabric → MessageBuffer → Parser → OrderBook

**Benefits:**
- Clear separation of concerns
- Each stage independently testable
- Easy to add stages (e.g., encryption, compression)

### 7.2 Observer Pattern (Simplified)

**Application:** Event callbacks from OrderBook

**Benefits:**
- Decouples OrderBook from downstream processors
- Enables flexible event handling
- Supports testing via mock callbacks

### 7.3 Builder Pattern

**Application:** MessageBuilder in test/demo code

**Benefits:**
- Readable test message construction
- Encapsulates byte-level format details
- Type-safe message creation

---

## 8. File Organization

```
include/
  └── orderbook.h           # All class declarations
  
src/
  ├── orderbook.cpp         # OrderBook and ITCHParser implementation
  └── main.cpp              # Demo application and MessageBuilder

CMakeLists.txt              # Build configuration
```

**Design Rationale:**
- **Single header** reduces file navigation overhead
- **Consolidated implementation** keeps related code together
- **Header-only where possible** (Order, DataFabric) for inlining

**Requirements Link:**
- NFR5: Maintainability → reduced file count, clear organization
- NFR3: Testability → public API in single header

---

## 9. Extensibility Points

### 9.1 Adding New Message Types

**How:** Extend ITCHParser::parse_one() switch statement

```cpp
// 1. Add message size constant
static constexpr size_t MODIFY_MSG_SIZE = 21;

// 2. Add case in parse_one()
case 'M':  // Modify Order
    if (buffer.size() < MODIFY_MSG_SIZE) return result;
    // parse fields...
    result.valid = true;
    break;

// 3. Add handler in OrderBook::handle_message()
case 'M':
    modify_order(result.order_id, result.price, result.quantity);
    break;
```

### 9.2 Integrating Price Level Aggregation

**How:** Register callback to build bid/ask book

```cpp
BidAskProcessor processor;

orderbook.set_event_callback([&processor](char type, const Order& order) {
    processor.handle_event(type, order);
});
```

### 9.3 Adding Performance Instrumentation

**How:** Wrap critical sections with timing

```cpp
void OrderBook::process() {
    auto start = high_resolution_clock::now();
    
    // ... processing logic ...
    
    auto duration = duration_cast<nanoseconds>(high_resolution_clock::now() - start);
    metrics_.record_processing_time(duration.count());
}
```

---

## 10. Constraints & Limitations

### 10.1 Current Constraints

| Constraint | Impact | Mitigation |
|------------|--------|------------|
| Single-threaded | Cannot scale to multiple cores | Future: lock-free design |
| In-memory only | Data lost on crash | Future: persistence layer |
| Soft-delete growth | Memory increases over session | Future: periodic cleanup |
| Single callback | Limited downstream flexibility | Future: observer pattern |
| Fixed message format | Cannot adapt to protocol changes | Version negotiation layer |

### 10.2 Assumptions

1. **Order IDs are unique** across the trading session
2. **Messages arrive in timestamp order** (no re-ordering needed)
3. **Sufficient memory** for all active orders (no eviction policy)
4. **Single producer** (DataFabric writer)
5. **Messages complete within reasonable time** (no timeout needed)

---

## 11. Design Decisions Summary

### 11.1 Key Decision Matrix

| Decision | Chosen Solution | Alternative | Rationale |
|----------|----------------|-------------|-----------|
| Order storage | `unordered_map` | Array + free list | Simplicity for v1.0 |
| Delete strategy | Soft delete (flag) | Hard delete (erase) | Performance: avoid rehash |
| Parser design | Stateless struct | Stateful class | Concurrency-ready |
| Event notification | Callback function | Observer list | Simplicity for v1.0 |
| Message buffer | `vector` erase | Circular buffer | Code simplicity |
| Byte order | Little-endian | Big-endian | Match x86 architecture |
| Error handling | Return codes | Exceptions | Hot-path performance |

### 11.2 Requirements Traceability

| Requirement ID | Design Component | Satisfied By |
|---------------|------------------|--------------|
| FR1: Order storage | OrderBook::orders_ | unordered_map with O(1) ops |
| FR2: Add orders | OrderBook::add_order() | Hash map insert + callback |
| FR3: Cancel orders | OrderBook::cancel_order() | Soft-delete with active flag |
| FR4: Message parsing | ITCHParser | Stateless parser with validation |
| FR5: Execute orders | OrderBook::execute_order() | In-place quantity update |
| NFR1: Performance | Data structures | O(1) operations, cache-friendly |
| NFR2: Reliability | Message buffer | Fragmentation handling |
| NFR3: Testability | Event callbacks | Mockable interfaces |
| NFR4: Extensibility | Callback pattern | Loose coupling |
| C1: FPGA integration | DataFabric | FIFO queue simulation |

---

## 12. Future Design Evolution

### 12.1 Phase 2: Multi-threading

**Planned Changes:**
- Lock-free order table (atomic operations)
- Per-price-level spinlocks
- Thread-local message buffers

**Impact:** Enable scaling to multiple cores

### 12.2 Phase 3: FPGA Integration

**Planned Changes:**
- Replace DataFabric with DMA interface
- Hardware-accelerated parsing
- Zero-copy buffer design

**Impact:** Sub-microsecond latency

### 12.3 Phase 4: Persistence

**Planned Changes:**
- Write-ahead log (WAL)
- Snapshot + replay mechanism
- Memory-mapped file support

**Impact:** Crash recovery, historical analysis

---

## 13. References

### 13.1 Internal Documents
- `IMPLEMENTATION.md` - Implementation details
- `SIMPLIFIED_STRUCTURE.md` - File organization
- `QUICK_REFERENCE.md` - API quick reference
- `BID_ASK_PROCESSOR_DESIGN.md` - Next component design

### 13.2 External Standards
- ITCH Protocol Specification (reference for message format)
- C++17 Standard (ISO/IEC 14882:2017)

### 13.3 Related Components
- Price Level Aggregation Block (future)
- Bid/Ask Processor (future)
- Order Matching Engine (future)

---

## 14. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-11-28 | Design Team | Initial design document for OrderBook component |

---

## Appendix A: Build Instructions

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cd build && make

# Run demo
./orderbook_main
```

## Appendix B: Source Code Location

All source code referenced in this document is located in:
- Header: `include/orderbook.h`
- Implementation: `src/orderbook.cpp`
- Demo: `src/main.cpp`
- Build: `CMakeLists.txt`

---

**End of Design Document**
