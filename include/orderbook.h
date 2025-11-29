#pragma once
#include <cstdint>
#include <functional>
#include <ostream>
#include <queue>
#include <unordered_map>
#include <vector>

// ============================================================================
// Order and Event Structures
// ============================================================================

struct Order
{
    uint64_t order_id;
    uint32_t price;
    uint32_t quantity;
    char side;  // 'B' or 'S'
    uint64_t timestamp;
    bool active;

    Order() = default;
    Order(uint64_t id, uint32_t p, uint32_t q, char s, uint64_t ts)
        : order_id(id), price(p), quantity(q), side(s), timestamp(ts), active(true)
    {
    }
};

// ============================================================================
// Data Fabric Interface (simulates FPGA soft-core → FIFO)
// ============================================================================

class DataFabric
{
   public:
    using Chunk = std::vector<uint8_t>;

    // Soft core writes data chunks to FIFO
    void write_chunk(const Chunk& chunk)
    {
        fifo_.push(chunk);
    }

    // Orderbook reads chunks from FIFO
    bool read_chunk(Chunk& out)
    {
        if (fifo_.empty())
            return false;
        out = std::move(fifo_.front());
        fifo_.pop();
        return true;
    }

    bool empty() const
    {
        return fifo_.empty();
    }

   private:
    std::queue<Chunk> fifo_;
};

// ============================================================================
// ITCH Message Parser
// ============================================================================

class ITCHParser
{
   public:
    static constexpr size_t ADD_MSG_SIZE = 26;
    static constexpr size_t CANCEL_MSG_SIZE = 9;
    static constexpr size_t EXECUTE_MSG_SIZE = 13;

    // Parse result: (bytes_consumed, order_id, price, quantity, side, type)
    struct ParseResult
    {
        size_t bytes_consumed;
        bool valid;
        char type;  // 'A' = Add, 'C' = Cancel, 'E' = Execute
        uint64_t order_id;
        uint32_t price;
        uint32_t quantity;
        char side;
        uint64_t timestamp;
    };

    ParseResult parse_one(const std::vector<uint8_t>& buffer) const;

   private:
    uint64_t read_u64(const std::vector<uint8_t>& buf, size_t& offset) const;
    uint32_t read_u32(const std::vector<uint8_t>& buf, size_t& offset) const;
};

// ============================================================================
// OrderBook - Main Class
// ============================================================================

class OrderBook
{
   public:
    using EventCallback = std::function<void(char type, const Order& order)>;

    explicit OrderBook(DataFabric& fabric);

    // Set callback for order events (for downstream processing like bid/ask)
    void set_event_callback(EventCallback cb)
    {
        callback_ = std::move(cb);
    }

    // Main processing loop - call repeatedly to drain fabric and process messages
    void process();

    // Order operations
    bool add_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    bool execute_order(uint64_t order_id, uint32_t quantity);

    // Lookup
    const Order* find_order(uint64_t order_id) const;

    // Statistics
    size_t get_order_count() const
    {
        return orders_.size();
    }
    size_t get_active_order_count() const;

    // Debug output
    void print_orders(std::ostream& os) const;

   private:
    void handle_message(const ITCHParser::ParseResult& result);

    DataFabric& fabric_;
    std::vector<uint8_t> message_buffer_;  // Reassembly buffer
    ITCHParser parser_;
    std::unordered_map<uint64_t, Order> orders_;
    EventCallback callback_;
};
