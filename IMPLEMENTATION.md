# Order Management Engine - Implementation Summary

## Overview
This implementation creates a complete "frontend" data pipeline for an order management engine:
**Data Fabric → Message Buffer → ITCH Decoder → Order Events → Order Table**

## Components Implemented

### 1. **OrderEvent** (`include/order_event.h`, `src/order_event.cpp`)
- Defines the event structure for order lifecycle events
- Event types: `Add`, `Execute`, `Cancel`
- Contains: order_id, price, quantity, side (B/S), timestamp
- Includes stream output operator for debugging

### 2. **OrderRecord** (`include/order_record.h`)
- Represents a stored order in the order table
- Includes an `active` flag to track cancelled orders
- Simple struct for efficient storage

### 3. **OrderTable** (`include/order_table.h`)
- Hash-map based order storage using `std::unordered_map`
- Operations:
  - `add_order()`: Insert new order
  - `cancel_order()`: Mark order as inactive
  - `find()`: Lookup order by ID (returns nullptr if not found/inactive)

### 4. **DataFabricInterface** (`include/data_fabric_interface.h`)
- Simulates the FPGA soft-core data fabric
- Uses a FIFO queue to buffer chunks of bytes
- Operations:
  - `write_chunk()`: Producer writes data chunks
  - `read_chunk()`: Consumer reads available chunks
  - `empty()`: Check if FIFO has data

### 5. **MessageBuffer** (`include/message_buffer.h`)
- Handles messages that span multiple chunks
- Accumulates bytes until a complete message is available
- Operations:
  - `append()`: Add incoming chunk data
  - `consume()`: Remove processed bytes from front
  - `data()`: Access buffer contents
  - `size()`: Get current buffer size

### 6. **MockITCHParser** (`include/mock_itch_parser.h`)
- Parses mock ITCH "Add Order" messages (26 bytes)
- Message format:
  ```
  byte 0      : 'A' (message type)
  bytes 1-8   : order_id (uint64_t, little-endian)
  bytes 9-12  : price (uint32_t)
  bytes 13-16 : quantity (uint32_t)
  byte 17     : side ('B' or 'S')
  bytes 18-25 : timestamp (uint64_t)
  ```
- Returns `(bytes_consumed, OrderEvent)` or `(0, nullopt)` if incomplete
- Easily extensible for real ITCH format later

### 7. **OrderManagementEngine** (`include/order_management_engine.h`)
- **Main orchestrator** that ties everything together
- Workflow:
  1. Drains chunks from DataFabricInterface
  2. Appends to MessageBuffer
  3. Parses complete messages using MockITCHParser
  4. Updates OrderTable
  5. Notifies callbacks with events
- API:
  - `process()`: Main processing loop (call repeatedly)
  - `set_event_callback()`: Register event listener
  - `find_order()`: Query order table

### 8. **Demo Application** (`src/main.cpp`)
- Demonstrates the complete pipeline
- Creates a mock Add Order message
- Simulates chunked delivery (splits message in two parts)
- Shows message reassembly and processing
- Verifies order is stored correctly in table

## Build & Run

```bash
cd /home/chezboy/school_work/CAPSTONE/OME_OrderBook
mkdir -p build && cd build
cmake ..
make
./ome_main
```

**Expected Output:**
```
Got event: order_id=12345 qty=50 price=10000 side=B
Order in table: price=10000 qty=50
```

## Key Design Decisions

1. **Header-only where possible**: Most classes are template-style for efficiency
2. **FIFO chunking**: Simulates realistic network/fabric behavior
3. **Message buffering**: Handles fragmentation gracefully
4. **Callback pattern**: Allows downstream processing without tight coupling
5. **Little-endian**: Simple byte packing (can be changed to match real ITCH)

## Next Steps for Extension

1. **Add more ITCH message types**: Execute, Cancel, Modify
2. **Real ITCH format**: Replace MockITCHParser with actual ITCH 5.0 spec
3. **Bid/Ask aggregation**: Add price-level book building
4. **Performance optimization**: Lock-free structures, zero-copy buffers
5. **Testing**: Add unit tests for each component
6. **Benchmarking**: Measure throughput and latency

## Architecture Flow

```
┌─────────────────────┐
│  Soft Core (FPGA)   │
│  [Data Producer]    │
└──────────┬──────────┘
           │ write_chunk()
           ▼
┌─────────────────────┐
│ DataFabricInterface │  (FIFO Queue)
│   [Chunk Buffer]    │
└──────────┬──────────┘
           │ read_chunk()
           ▼
┌─────────────────────┐
│   MessageBuffer     │  (Reassembly)
│  [Byte Accumulator] │
└──────────┬──────────┘
           │ parse_one()
           ▼
┌─────────────────────┐
│  MockITCHParser     │  (Decoder)
│  [Message Parser]   │
└──────────┬──────────┘
           │ OrderEvent
           ▼
┌─────────────────────┐
│   OrderTable        │  (Storage)
│  [Order Lookup]     │
└─────────────────────┘
           │
           ▼
     [Callback/Output]
```

This is a **production-ready foundation** for building a high-performance order book system!
