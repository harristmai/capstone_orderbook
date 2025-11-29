#include <iostream>

#include "orderbook.h"

// Helper to build ITCH messages
class MessageBuilder
{
   public:
    static std::vector<uint8_t> build_add_order(uint64_t order_id, uint32_t price,
                                                uint32_t quantity, char side, uint64_t timestamp)
    {
        std::vector<uint8_t> msg;
        msg.push_back('A');  // Message type

        push_u64(msg, order_id);
        push_u32(msg, price);
        push_u32(msg, quantity);
        msg.push_back(side);
        push_u64(msg, timestamp);

        return msg;
    }

    static std::vector<uint8_t> build_cancel_order(uint64_t order_id)
    {
        std::vector<uint8_t> msg;
        msg.push_back('C');  // Message type
        push_u64(msg, order_id);
        return msg;
    }

    static std::vector<uint8_t> build_execute_order(uint64_t order_id, uint32_t quantity)
    {
        std::vector<uint8_t> msg;
        msg.push_back('E');  // Message type
        push_u64(msg, order_id);
        push_u32(msg, quantity);
        return msg;
    }

   private:
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
    std::cout << "=== OrderBook with Data Fabric Simulation ===\n\n";

    // Create data fabric (simulates FPGA soft-core FIFO)
    DataFabric fabric;

    // Create orderbook
    OrderBook orderbook(fabric);

    // Register callback to see events
    orderbook.set_event_callback(
        [](char type, const Order& order)
        {
            const char* event_name = (type == 'A') ? "ADD" : (type == 'C') ? "CANCEL" : "EXECUTE";
            std::cout << "[EVENT] " << event_name << " - Order " << order.order_id
                      << " | Price: " << order.price << " | Qty: " << order.quantity
                      << " | Side: " << order.side << " | Timestamp: " << order.timestamp
                      << " | Active: " << (order.active ? "Yes" : "No") << "\n";
        });

    // ========================================================================
    // Test 1: Add orders with chunked delivery
    // ========================================================================
    std::cout << "--- Test 1: Add Orders (with chunking) ---\n";

    auto msg1 = MessageBuilder::build_add_order(12345, 10000, 50, 'B', 1000000);
    auto msg2 = MessageBuilder::build_add_order(12346, 10050, 100, 'S', 1000100);

    // Simulate chunked delivery - split first message into 2 chunks
    DataFabric::Chunk chunk1(msg1.begin(), msg1.begin() + 10);
    DataFabric::Chunk chunk2(msg1.begin() + 10, msg1.end());

    fabric.write_chunk(chunk1);
    orderbook.process();  // Not enough data yet
    std::cout << "After chunk 1: " << orderbook.get_active_order_count() << " orders\n";

    fabric.write_chunk(chunk2);
    orderbook.process();  // Now complete message
    std::cout << "After chunk 2: " << orderbook.get_active_order_count() << " orders\n";

    // Send second message in one chunk
    fabric.write_chunk(msg2);
    orderbook.process();
    std::cout << "After msg2: " << orderbook.get_active_order_count() << " orders\n\n";

    // ========================================================================
    // Test 2: Execute partial order
    // ========================================================================
    std::cout << "--- Test 2: Execute Partial Order ---\n";

    auto exec_msg = MessageBuilder::build_execute_order(12345, 20);  // Execute 20 of 50
    fabric.write_chunk(exec_msg);
    orderbook.process();

    const Order* order = orderbook.find_order(12345);
    if (order)
    {
        std::cout << "Order 12345 after execution: qty=" << order->quantity << "\n\n";
    }

    // ========================================================================
    // Test 3: Cancel order
    // ========================================================================
    std::cout << "--- Test 3: Cancel Order ---\n";

    auto cancel_msg = MessageBuilder::build_cancel_order(12346);
    fabric.write_chunk(cancel_msg);
    orderbook.process();
    std::cout << "After cancel: " << orderbook.get_active_order_count() << " active orders\n\n";

    // ========================================================================
    // Test 4: Add multiple orders in batch
    // ========================================================================
    std::cout << "--- Test 4: Batch Add Orders ---\n";

    for (uint64_t i = 20000; i < 20005; ++i)
    {
        auto msg = MessageBuilder::build_add_order(i, 9900 + (i % 10), 10, 'B', 2000000 + i);
        fabric.write_chunk(msg);
    }

    orderbook.process();  // Process all at once
    std::cout << "Total orders: " << orderbook.get_order_count()
              << " | Active: " << orderbook.get_active_order_count() << "\n\n";

    // ========================================================================
    // Final state
    // ========================================================================
    std::cout << "--- Final OrderBook State ---\n";
    orderbook.print_orders(std::cout);

    return 0;
}
