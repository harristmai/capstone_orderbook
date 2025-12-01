#include <iostream>
#include <fstream>
#include <memory>
#include <string>

#include "orderbook.h"

// Tee stream - writes to both cout and file
class TeeStream
{
   private:
    std::ostream& stream1_;
    std::ostream& stream2_;
    
   public:
    TeeStream(std::ostream& s1, std::ostream& s2) : stream1_(s1), stream2_(s2) {}
    
    template<typename T>
    TeeStream& operator<<(const T& data)
    {
        stream1_ << data;
        stream2_ << data;
        return *this;
    }
    
    // Handle stream manipulators (like std::endl)
    TeeStream& operator<<(std::ostream& (*manip)(std::ostream&))
    {
        stream1_ << manip;
        stream2_ << manip;
        return *this;
    }
};

// Helper to build ITCH 5.0 messages
class MessageBuilder
{
   public:
    // Build Add Order (No MPID) - 'A' - 36 bytes
    static std::vector<uint8_t> build_add_order(uint64_t order_id, uint32_t price,
                                                uint32_t quantity, char side, uint64_t timestamp)
    {
        std::vector<uint8_t> msg;
        msg.push_back('A');  // Message Type
        
        // Stock Locate (2 bytes) - use 0 for prototype
        push_u16(msg, 0);
        
        // Tracking Number (2 bytes) - use 0 for prototype
        push_u16(msg, 0);
        
        // Timestamp (6 bytes) - nanoseconds since midnight, little-endian
        for (int i = 0; i < 6; ++i)
        {
            msg.push_back((timestamp >> (8 * i)) & 0xFF);
        }
        
        // Order Reference Number (8 bytes)
        push_u64(msg, order_id);
        
        // Buy/Sell Indicator (1 byte) - 'B' or 'S'
        msg.push_back(side);
        
        // Shares (4 bytes)
        push_u32(msg, quantity);
        
        // Stock (8 bytes) - right-padded with spaces, use "TEST    "
        msg.push_back('T');
        msg.push_back('E');
        msg.push_back('S');
        msg.push_back('T');
        msg.push_back(' ');
        msg.push_back(' ');
        msg.push_back(' ');
        msg.push_back(' ');
        
        // Price (4 bytes) - 4 decimal places
        push_u32(msg, price);
        
        return msg;  // Total: 36 bytes
    }

    // Build Order Cancel - 'X' - 23 bytes
    static std::vector<uint8_t> build_cancel_order(uint64_t order_id, uint32_t cancelled_shares = 0)
    {
        std::vector<uint8_t> msg;
        msg.push_back('X');  // Message Type
        
        // Stock Locate (2 bytes)
        push_u16(msg, 0);
        
        // Tracking Number (2 bytes)
        push_u16(msg, 0);
        
        // Timestamp (6 bytes) - use current timestamp or 0
        for (int i = 0; i < 6; ++i)
        {
            msg.push_back(0);
        }
        
        // Order Reference Number (8 bytes)
        push_u64(msg, order_id);
        
        // Cancelled Shares (4 bytes) - 0 means full cancel
        push_u32(msg, cancelled_shares);
        
        return msg;  // Total: 23 bytes
    }

    // Build Order Executed - 'E' - 31 bytes
    static std::vector<uint8_t> build_execute_order(uint64_t order_id, uint32_t quantity)
    {
        std::vector<uint8_t> msg;
        msg.push_back('E');  // Message Type
        
        // Stock Locate (2 bytes)
        push_u16(msg, 0);
        
        // Tracking Number (2 bytes)
        push_u16(msg, 0);
        
        // Timestamp (6 bytes)
        for (int i = 0; i < 6; ++i)
        {
            msg.push_back(0);
        }
        
        // Order Reference Number (8 bytes)
        push_u64(msg, order_id);
        
        // Executed Shares (4 bytes)
        push_u32(msg, quantity);
        
        // Match Number (8 bytes) - use 0 for prototype
        push_u64(msg, 0);
        
        return msg;  // Total: 31 bytes
    }

    // Build Order Replace - 'U' - 35 bytes
    static std::vector<uint8_t> build_replace_order(uint64_t old_order_id, uint64_t new_order_id,
                                                     uint32_t new_price, uint32_t new_quantity,
                                                     uint64_t timestamp = 0)
    {
        std::vector<uint8_t> msg;
        msg.push_back('U');  // Message Type
        
        // Stock Locate (2 bytes)
        push_u16(msg, 0);
        
        // Tracking Number (2 bytes)
        push_u16(msg, 0);
        
        // Timestamp (6 bytes)
        for (int i = 0; i < 6; ++i)
        {
            msg.push_back((timestamp >> (8 * i)) & 0xFF);
        }
        
        // Original Order Reference Number (8 bytes)
        push_u64(msg, old_order_id);
        
        // New Order Reference Number (8 bytes)
        push_u64(msg, new_order_id);
        
        // Shares (4 bytes)
        push_u32(msg, new_quantity);
        
        // Price (4 bytes)
        push_u32(msg, new_price);
        
        return msg;  // Total: 35 bytes
    }

   private:
    static void push_u16(std::vector<uint8_t>& msg, uint16_t value)
    {
        for (int i = 0; i < 2; ++i)
        {
            msg.push_back((value >> (8 * i)) & 0xFF);
        }
    }
    
    static void push_u64(std::vector<uint8_t>& msg, uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
        {
            msg.push_back((value >> (8 * i)) & 0xFF);
        }
    }

    static void push_u32(std::vector<uint8_t>& msg, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            msg.push_back((value >> (8 * i)) & 0xFF);
        }
    }
};

int main()
{
    // Create debug directory if it doesn't exist (one level up from executable)
    #ifdef _WIN32
        system("if not exist ..\\debug mkdir ..\\debug");
    #else
        system("mkdir -p ../debug");
    #endif
    
    // Open log file in debug folder
    std::ofstream logfile("../debug/orderbook_verification_test_results.log");
    if (!logfile.is_open())
    {
        std::cerr << "ERROR: Could not open log file for writing\n";
        return 1;
    }
    
    // Create tee stream to write to both console and file
    TeeStream out(std::cout, logfile);
    
    out << "=== OrderBook with Data Fabric Simulation ===\n";
    out << "Test Run Date: 2025-11-30\n";
    out << "Log File: ../debug/orderbook_verification_test_results.log\n\n";

    // Create data fabric (simulates FPGA soft-core FIFO)
    DataFabric fabric;

    // Create orderbook
    OrderBook orderbook(fabric);

    // Register callback to see events (capture logfile by reference)
    orderbook.set_event_callback(
        [&logfile](char type, const Order& order)
        {
            const char* event_name = (type == 'A') ? "ADD" : 
                                     (type == 'X') ? "CANCEL" : 
                                     (type == 'E') ? "EXECUTE" : 
                                     (type == 'U') ? "REPLACE" : "UNKNOWN";
            std::string event_msg = "[EVENT] " + std::string(event_name) + " - Order " + std::to_string(order.order_id)
                      + " | Price: " + std::to_string(order.price) + " | Qty: " + std::to_string(order.quantity)
                      + " | Side: " + std::string(1, order.side) + " | Timestamp: " + std::to_string(order.timestamp)
                      + " | Active: " + (order.active ? "Yes" : "No") + "\n";
            std::cout << event_msg;
            logfile << event_msg;
        });

    // ========================================================================
    // Test 1: Add orders with chunked delivery
    // ========================================================================
    out << "--- Test 1: Add Orders (with chunking) ---\n";

    auto msg1 = MessageBuilder::build_add_order(12345, 10000, 50, 'B', 1000000);
    auto msg2 = MessageBuilder::build_add_order(12346, 10050, 100, 'S', 1000100);

    // Simulate chunked delivery - split first message into 2 chunks
    DataFabric::Chunk chunk1(msg1.begin(), msg1.begin() + 10);
    DataFabric::Chunk chunk2(msg1.begin() + 10, msg1.end());

    fabric.write_chunk(chunk1);
    orderbook.process();  // Not enough data yet
    out << "After chunk 1: " << orderbook.get_active_order_count() << " orders\n";

    fabric.write_chunk(chunk2);
    orderbook.process();  // Now complete message
    out << "After chunk 2: " << orderbook.get_active_order_count() << " orders\n";

    // Send second message in one chunk
    fabric.write_chunk(msg2);
    orderbook.process();
    out << "After msg2: " << orderbook.get_active_order_count() << " orders\n\n";

    // ========================================================================
    // Test 2: Execute partial order
    // ========================================================================
    out << "--- Test 2: Execute Partial Order ---\n";

    auto exec_msg = MessageBuilder::build_execute_order(12345, 20);  // Execute 20 of 50
    fabric.write_chunk(exec_msg);
    orderbook.process();

    const Order* order = orderbook.find_order(12345);
    if (order)
    {
        out << "Order 12345 after execution: qty=" << order->quantity << "\n\n";
    }

    // ========================================================================
    // Test 3: Cancel order
    // ========================================================================
    out << "--- Test 3: Cancel Order ---\n";

    auto cancel_msg = MessageBuilder::build_cancel_order(12346);
    fabric.write_chunk(cancel_msg);
    orderbook.process();
    out << "After cancel: " << orderbook.get_active_order_count() << " active orders\n\n";

    // ========================================================================
    // Test 4: Order Replace
    // ========================================================================
    out << "--- Test 4: Order Replace ---\n";

    // Replace order 12345 (currently 30 shares at 10000) with new order 12347 (100 shares at 10050)
    out << "Before replace:\n";
    const Order* old_order = orderbook.find_order(12345);
    if (old_order)
    {
        out << "  Order 12345: price=" << old_order->price << ", qty=" << old_order->quantity << "\n";
    }

    auto replace_msg = MessageBuilder::build_replace_order(12345, 12347, 10050, 100, 3500000);
    fabric.write_chunk(replace_msg);
    orderbook.process();

    out << "After replace:\n";
    old_order = orderbook.find_order(12345);
    out << "  Order 12345 exists: " << (old_order != nullptr ? "Yes" : "No") << "\n";
    
    const Order* new_order = orderbook.find_order(12347);
    if (new_order)
    {
        out << "  Order 12347: price=" << new_order->price << ", qty=" << new_order->quantity << "\n";
    }
    out << "\n";

    // ========================================================================
    // Test 5: Add multiple orders in batch
    // ========================================================================
    out << "--- Test 5: Batch Add Orders ---\n";

    // Add bid orders
    for (uint64_t i = 20000; i < 20005; ++i)
    {
        auto msg = MessageBuilder::build_add_order(i, 9900 + (i % 10), 10, 'B', 2000000 + i);
        fabric.write_chunk(msg);
    }

    // Add ask orders
    for (uint64_t i = 30000; i < 30005; ++i)
    {
        auto msg = MessageBuilder::build_add_order(i, 10100 + (i % 10), 15, 'S', 3000000 + i);
        fabric.write_chunk(msg);
    }

    orderbook.process();  // Process all at once
    out << "Total orders: " << orderbook.get_order_count()
              << " | Active: " << orderbook.get_active_order_count() << "\n\n";

    // ========================================================================
    // Test 6: Market Data Queries
    // ========================================================================
    out << "--- Test 6: Market Data Queries ---\n";

    uint64_t bid_price, bid_qty, ask_price, ask_qty;
    if (orderbook.get_best_bid(bid_price, bid_qty))
    {
        out << "Best Bid: " << bid_price << " @ " << bid_qty << "\n";
    }
    if (orderbook.get_best_ask(ask_price, ask_qty))
    {
        out << "Best Ask: " << ask_price << " @ " << ask_qty << "\n";
    }

    uint64_t spread;
    if (orderbook.get_spread(spread))
    {
        out << "Spread: " << spread << "\n";
    }

    out << "\nMarket Depth (Top 5 levels):\n";
    auto depth = orderbook.get_depth(5);
    
    out << "  BIDS:\n";
    for (const auto& [price, qty] : depth.bids)
    {
        out << "    " << price << " @ " << qty << "\n";
    }
    
    out << "  ASKS:\n";
    for (const auto& [price, qty] : depth.asks)
    {
        out << "    " << price << " @ " << qty << "\n";
    }
    out << "\n";

    // ========================================================================
    // Test 7: Error Handling Tests
    // ========================================================================
    out << "--- Test 7: Error Handling ---\n";
    
    // Reset error stats before tests
    orderbook.reset_error_stats();
    
    // Test 7a: Unknown message type
    out << "Test 7a: Unknown message type\n";
    std::vector<uint8_t> unknown_msg = {0xFF, 0x01, 0x02, 0x03};  // Invalid type 0xFF
    fabric.write_chunk(unknown_msg);
    orderbook.process();
    
    auto stats = orderbook.get_error_stats();
    out << "  Unknown message types: " << stats.unknown_message_types << "\n";
    
    // Test 7b: Buffer overflow protection
    out << "Test 7b: Buffer overflow (simulated large garbage data)\n";
    std::vector<uint8_t> garbage(600, 0xAA);  // 600 bytes of garbage > MAX_BUFFER_SIZE
    fabric.write_chunk(garbage);
    orderbook.process();
    
    stats = orderbook.get_error_stats();
    out << "  Buffer overflows: " << stats.buffer_overflows << "\n";
    
    // Test 7c: Incomplete message (partial chunk)
    out << "Test 7c: Incomplete message handling\n";
    auto partial_add = MessageBuilder::build_add_order(99999, 15000, 200, 'B', 5000000);
    std::vector<uint8_t> partial_chunk(partial_add.begin(), partial_add.begin() + 15);  // Only 15 of 36 bytes
    fabric.write_chunk(partial_chunk);
    orderbook.process();
    
    stats = orderbook.get_error_stats();
    out << "  Incomplete messages (waiting for data): " << stats.incomplete_messages << "\n";
    
    // Complete the message
    std::vector<uint8_t> remaining_chunk(partial_add.begin() + 15, partial_add.end());
    fabric.write_chunk(remaining_chunk);
    orderbook.process();
    out << "  Message completed successfully, order count: " << orderbook.get_order_count() << "\n";
    
    // Test 7d: Invalid operations
    out << "Test 7d: Invalid operations (cancel non-existent order)\n";
    size_t invalid_before = orderbook.get_error_stats().invalid_operations;
    bool result = orderbook.cancel_order(999999);  // Non-existent order
    stats = orderbook.get_error_stats();
    out << "  Cancel result: " << (result ? "Success" : "Failed (expected)") << "\n";
    out << "  Invalid operations: " << (stats.invalid_operations - invalid_before) << " new\n";
    
    // Test 7e: Execute with excessive quantity
    out << "Test 7e: Execute with excessive quantity\n";
    invalid_before = orderbook.get_error_stats().invalid_operations;
    result = orderbook.execute_order(99999, 10000);  // Order only has 200 shares
    stats = orderbook.get_error_stats();
    out << "  Execute result: " << (result ? "Success" : "Failed (expected)") << "\n";
    out << "  Invalid operations: " << (stats.invalid_operations - invalid_before) << " new\n";
    
    // Test 7f: Replace non-existent order
    out << "Test 7f: Replace non-existent order\n";
    invalid_before = orderbook.get_error_stats().invalid_operations;
    result = orderbook.replace_order(888888, 888889, 12000, 50);  // Non-existent order
    stats = orderbook.get_error_stats();
    out << "  Replace result: " << (result ? "Success" : "Failed (expected)") << "\n";
    out << "  Invalid operations: " << (stats.invalid_operations - invalid_before) << " new\n";
    
    // Final error statistics
    out << "\nFinal Error Statistics:\n";
    stats = orderbook.get_error_stats();
    out << "  Unknown message types: " << stats.unknown_message_types << "\n";
    out << "  Buffer overflows: " << stats.buffer_overflows << "\n";
    out << "  Incomplete messages: " << stats.incomplete_messages << "\n";
    out << "  Invalid operations: " << stats.invalid_operations << "\n";
    out << "\n";

    // ========================================================================
    // Test 8: FIFO Backpressure (simulating Network I/O overload)
    // ========================================================================
    out << "--- Test 8: FIFO Backpressure ---\n";
    
    // Create a small FIFO to demonstrate backpressure
    DataFabric small_fabric(256);  // Only 256 bytes
    OrderBook test_orderbook(small_fabric);
    
    out << "FIFO Configuration: " << 256 << " bytes max\n";
    
    // Try to flood the FIFO with messages (each message = 36 bytes for 'A')
    int successful_writes = 0;
    int backpressure_count = 0;
    
    for (int i = 0; i < 20; ++i) {
        auto msg = MessageBuilder::build_add_order(
            80000 + i,      // order_id
            10000 + i * 10, // price
            100,            // quantity
            'B',            // side
            8000000 + i     // timestamp
        );
        
        bool accepted = small_fabric.write_chunk(msg);
        if (accepted) {
            successful_writes++;
        } else {
            backpressure_count++;
        }
    }
    
    out << "Attempted writes: 20 messages (720 bytes total)\n";
    out << "Successful writes: " << successful_writes << " messages\n";
    out << "Backpressure events: " << backpressure_count << " (FIFO full)\n";
    
    auto fifo_stats = small_fabric.get_stats();
    out << "\nFIFO Statistics:\n";
    out << "  Current depth: " << small_fabric.depth_bytes() << " bytes\n";
    out << "  Utilization: " << (small_fabric.utilization() * 100) << "%\n";
    out << "  High-water mark: " << fifo_stats.max_depth_reached << " bytes\n";
    out << "  Total bytes written: " << fifo_stats.total_bytes_written << "\n";
    out << "  Total bytes dropped: " << fifo_stats.total_bytes_dropped << "\n";
    out << "  Backpressure events: " << fifo_stats.backpressure_events << "\n";
    
    // Now drain FIFO by processing
    out << "\nDraining FIFO...\n";
    test_orderbook.process();
    out << "After processing: " << test_orderbook.get_order_count() << " orders added\n";
    out << "FIFO depth after drain: " << small_fabric.depth_bytes() << " bytes\n";
    out << "\n";

    // ========================================================================
    // Final state
    // ========================================================================
    out << "--- Final OrderBook State ---\n";
    orderbook.print_orders(std::cout);
    orderbook.print_orders(logfile);
    
    out << "\n=== Test Run Complete ===\n";
    out << "Results saved to: ../debug/orderbook_verification_test_results.log\n";
    
    logfile.close();
    return 0;
}
