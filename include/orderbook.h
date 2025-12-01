#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <queue>
#include <unordered_map>
#include <vector>

#include "bid_ask.h"

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
    OrderInfo* book_info;  // Pointer to bid/ask processor info

    Order() = default;
    Order(uint64_t id, uint32_t p, uint32_t q, char s, uint64_t ts)
        : order_id(id), price(p), quantity(q), side(s), timestamp(ts), active(true), book_info(nullptr)
    {
    }
};

// ============================================================================
// Data Fabric Interface (simulates FPGA soft-core → AXI-Stream FIFO)
// ============================================================================

class DataFabric
{
   public:
    using Chunk = std::vector<uint8_t>;

    // FIFO depth configuration (bytes)
    // In FPGA: This would be BRAM allocation for AXI-Stream FIFO
    // Typical values: 512B-4KB for low latency, 16KB-64KB for buffering
    static constexpr size_t DEFAULT_FIFO_DEPTH = 4096;  // 4KB FIFO

    explicit DataFabric(size_t max_depth = DEFAULT_FIFO_DEPTH) 
        : max_depth_bytes_(max_depth), current_depth_bytes_(0) {}

    // AXI-Stream write with backpressure (returns TREADY signal)
    // Returns true if write succeeded, false if FIFO full (backpressure asserted)
    bool write_chunk(const Chunk& chunk)
    {
        // Check if FIFO has space (TREADY signal)
        if (current_depth_bytes_ + chunk.size() > max_depth_bytes_) {
            stats_.backpressure_events++;
            stats_.total_bytes_dropped += chunk.size();
            return false;  // TREADY = 0, apply backpressure
        }

        fifo_.push(chunk);
        current_depth_bytes_ += chunk.size();
        stats_.total_bytes_written += chunk.size();
        
        // Track high-water mark
        if (current_depth_bytes_ > stats_.max_depth_reached) {
            stats_.max_depth_reached = current_depth_bytes_;
        }
        
        return true;  // TREADY = 1, write accepted
    }

    // Orderbook reads chunks from FIFO (consumer side)
    bool read_chunk(Chunk& out)
    {
        if (fifo_.empty())
            return false;
        
        size_t chunk_size = fifo_.front().size();
        out = std::move(fifo_.front());
        fifo_.pop();
        current_depth_bytes_ -= chunk_size;
        stats_.total_bytes_read += chunk_size;
        
        return true;
    }

    // Status queries
    bool empty() const { return fifo_.empty(); }
    bool full() const { return current_depth_bytes_ >= max_depth_bytes_; }
    size_t depth_bytes() const { return current_depth_bytes_; }
    size_t available_bytes() const { return max_depth_bytes_ - current_depth_bytes_; }
    float utilization() const { 
        return static_cast<float>(current_depth_bytes_) / max_depth_bytes_; 
    }

    // Flow control statistics
    struct FIFOStats {
        size_t backpressure_events = 0;    // Number of times FIFO was full
        size_t total_bytes_written = 0;     // Total accepted bytes
        size_t total_bytes_dropped = 0;     // Total dropped due to backpressure
        size_t total_bytes_read = 0;        // Total consumed bytes
        size_t max_depth_reached = 0;       // High-water mark
    };
    
    const FIFOStats& get_stats() const { return stats_; }
    void reset_stats() { stats_ = FIFOStats{}; }

   private:
    std::queue<Chunk> fifo_;
    size_t max_depth_bytes_;         // Maximum FIFO depth in bytes
    size_t current_depth_bytes_;     // Current occupancy in bytes
    FIFOStats stats_;                // Performance monitoring
};

// ============================================================================
// ITCH Message Parser
// ============================================================================

class ITCHParser
{
   public:
    // NASDAQ ITCH 5.0 message lengths
    static constexpr size_t ADD_MSG_SIZE = 36;      // 'A' - Add Order (No MPID Attribution)
    static constexpr size_t CANCEL_MSG_SIZE = 23;   // 'X' - Order Cancel
    static constexpr size_t EXECUTE_MSG_SIZE = 31;  // 'E' - Order Executed
    static constexpr size_t REPLACE_MSG_SIZE = 35;  // 'U' - Order Replace
    
    // Buffer overflow protection
    static constexpr size_t MAX_BUFFER_SIZE = 512;  // Max accumulation before reset

    struct ParseResult
    {
        size_t bytes_consumed;
        bool valid;
        char type;  // 'A' = Add, 'X' = Cancel, 'E' = Execute, 'U' = Replace
        uint64_t order_id;
        uint64_t new_order_id;
        uint32_t price;
        uint32_t quantity;
        char side;
        uint64_t timestamp;
    };

    std::optional<ParseResult> parse_one(const std::vector<uint8_t>& buffer) const;

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

    // for downstream processing like bid/ask
    void set_event_callback(EventCallback cb)
    {
        callback_ = std::move(cb);
    }

    // call repeatedly to drain fabric and process messages
    void process();

    bool add_order(const Order& order);
    bool cancel_order(uint64_t order_id);
    bool execute_order(uint64_t order_id, uint32_t quantity);
    bool replace_order(uint64_t old_order_id, uint64_t new_order_id, uint32_t new_price, uint32_t new_quantity);

    const Order* find_order(uint64_t order_id) const;

    size_t get_order_count() const
    {
        return orders_.size();
    }
    size_t get_active_order_count() const;
    
    struct ErrorStats {
        size_t unknown_message_types = 0;
        size_t buffer_overflows = 0;
        size_t incomplete_messages = 0;
        size_t invalid_operations = 0;
    };
    
    const ErrorStats& get_error_stats() const { return error_stats_; }
    void reset_error_stats() { error_stats_ = ErrorStats{}; }

    // Debug output
    void print_orders(std::ostream& os) const;

    // Market data API
    bool get_best_bid(uint64_t& price_out, uint64_t& qty_out) const;
    bool get_best_ask(uint64_t& price_out, uint64_t& qty_out) const;
    bool get_spread(uint64_t& spread_out) const;
    
    struct MarketDepth {
        std::vector<std::pair<uint64_t,uint64_t>> bids;
        std::vector<std::pair<uint64_t,uint64_t>> asks;
    };
    MarketDepth get_depth(size_t levels) const;

private:
    void handle_message(const ITCHParser::ParseResult& result);

    DataFabric& fabric_;
    std::vector<uint8_t> message_buffer_;
    ITCHParser parser_;
    std::unordered_map<uint64_t, Order> orders_;
    std::unordered_map<uint64_t, OrderInfo> order_info_;  // Bid/ask processor info
    OrderBookEngine book_;  // Price-level matching engine
    EventCallback callback_;
    ErrorStats error_stats_;
};