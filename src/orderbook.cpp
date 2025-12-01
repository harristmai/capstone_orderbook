#include "orderbook.h"

#include <iomanip>
#include <iostream>

// ============================================================================
// ITCH Parser Implementation
// ============================================================================

// Helper function to get message length by type
static size_t get_itch_message_length(char msg_type)
{
    switch (msg_type)
    {
        case 'A': return ITCHParser::ADD_MSG_SIZE;
        case 'X': return ITCHParser::CANCEL_MSG_SIZE;
        case 'E': return ITCHParser::EXECUTE_MSG_SIZE;
        case 'U': return ITCHParser::REPLACE_MSG_SIZE;
        default: return 0;  // Unknown message type
    }
}

// Helper to skip common ITCH header: Stock Locate (2) + Tracking Number (2)
static void skip_itch_header(size_t& offset)
{
    offset += 4;  // Skip to timestamp
}

// Helper to read 6-byte timestamp
static uint64_t read_timestamp(const std::vector<uint8_t>& buffer, size_t& offset)
{
    uint64_t timestamp = 0;
    for (int i = 0; i < 6; ++i)
    {
        timestamp |= static_cast<uint64_t>(buffer[offset + i]) << (8 * i);
    }
    offset += 6;
    return timestamp;
}

uint64_t ITCHParser::read_u64(const std::vector<uint8_t>& buf, size_t& offset) const
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
    {
        value |= static_cast<uint64_t>(buf[offset + i]) << (8 * i);
    }
    offset += 8;
    return value;
}

uint32_t ITCHParser::read_u32(const std::vector<uint8_t>& buf, size_t& offset) const
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
    {
        value |= static_cast<uint32_t>(buf[offset + i]) << (8 * i);
    }
    offset += 4;
    return value;
}

std::optional<ITCHParser::ParseResult> ITCHParser::parse_one(const std::vector<uint8_t>& buffer) const
{
    if (buffer.empty())
        return std::nullopt;  // No data available

    char msg_type = static_cast<char>(buffer[0]);
    size_t expected_length = get_itch_message_length(msg_type);
    
    // Unknown message type
    if (expected_length == 0)
    {
        std::cerr << "[ERROR] Unknown ITCH message type: '" << msg_type 
                  << "' (0x" << std::hex << static_cast<int>(static_cast<uint8_t>(msg_type)) 
                  << std::dec << ")\n";
        return std::nullopt;
    }
    
    // Incomplete message - need more data
    if (buffer.size() < expected_length)
        return std::nullopt;
    
    ParseResult result{0, false, 0, 0, 0, 0, 0, 0, 0};
    size_t offset = 1;  // Skip message type byte

    // Add Order (No MPID Attribution): 'A' - 36 bytes
    if (msg_type == 'A')
    {
        result.type = 'A';
        skip_itch_header(offset);           // Skip Locate + Tracking
        result.timestamp = read_timestamp(buffer, offset);
        result.order_id = read_u64(buffer, offset);
        result.side = static_cast<char>(buffer[offset++]);
        result.quantity = read_u32(buffer, offset);
        offset += 8;                        // Skip Stock symbol
        result.price = read_u32(buffer, offset);
        result.bytes_consumed = ADD_MSG_SIZE;
        result.valid = true;
        return result;
    }
    // Order Cancel: 'X' - 23 bytes
    else if (msg_type == 'X')
    {
        result.type = 'X';
        skip_itch_header(offset);           // Skip Locate + Tracking
        offset += 6;                        // Skip Timestamp
        result.order_id = read_u64(buffer, offset);
        result.quantity = read_u32(buffer, offset);  // Cancelled shares
        result.bytes_consumed = CANCEL_MSG_SIZE;
        result.valid = true;
        return result;
    }
    // Order Executed: 'E' - 31 bytes
    else if (msg_type == 'E')
    {
        result.type = 'E';
        skip_itch_header(offset);           // Skip Locate + Tracking
        offset += 6;                        // Skip Timestamp
        result.order_id = read_u64(buffer, offset);
        result.quantity = read_u32(buffer, offset);
        offset += 8;                        // Skip Match Number
        result.bytes_consumed = EXECUTE_MSG_SIZE;
        result.valid = true;
        return result;
    }
    // Order Replace: 'U' - 35 bytes
    else if (msg_type == 'U')
    {
        result.type = 'U';
        skip_itch_header(offset);           // Skip Locate + Tracking
        result.timestamp = read_timestamp(buffer, offset);
        result.order_id = read_u64(buffer, offset);      // Original order
        result.new_order_id = read_u64(buffer, offset);  // New order
        result.quantity = read_u32(buffer, offset);
        result.price = read_u32(buffer, offset);
        result.bytes_consumed = REPLACE_MSG_SIZE;
        result.valid = true;
        return result;
    }
    
    return std::nullopt;  // Should never reach here
}

// ============================================================================
// OrderBook Implementation
// ============================================================================

OrderBook::OrderBook(DataFabric& fabric) : fabric_(fabric) {}

void OrderBook::process()
{
    // 1) Drain all chunks from fabric into message buffer
    DataFabric::Chunk chunk;
    while (fabric_.read_chunk(chunk))
    {
        message_buffer_.insert(message_buffer_.end(), chunk.begin(), chunk.end());
    }
    
    // 2) Buffer overflow protection
    if (message_buffer_.size() > ITCHParser::MAX_BUFFER_SIZE)
    {
        std::cerr << "[ERROR] Buffer overflow detected (" << message_buffer_.size() 
                  << " bytes). Likely truncated frame or connection issue. Clearing buffer.\n";
        message_buffer_.clear();
        error_stats_.buffer_overflows++;
        return;
    }

    // 3) Parse complete messages from buffer
    while (true)
    {
        auto result_opt = parser_.parse_one(message_buffer_);
        
        // No valid message available
        if (!result_opt.has_value())
        {
            // Check if we have data that looks like an unknown message type
            if (!message_buffer_.empty())
            {
                char msg_type = static_cast<char>(message_buffer_[0]);
                size_t expected_len = get_itch_message_length(msg_type);
                
                if (expected_len == 0)
                {
                    // Unknown message type - skip this byte and try again
                    std::cerr << "[ERROR] Skipping unknown message type byte: 0x" 
                              << std::hex << static_cast<int>(static_cast<uint8_t>(msg_type)) 
                              << std::dec << "\n";
                    message_buffer_.erase(message_buffer_.begin());
                    error_stats_.unknown_message_types++;
                    continue;
                }
                else
                {
                    // Incomplete message - wait for more data
                    error_stats_.incomplete_messages++;
                }
            }
            break;
        }
        
        auto& result = result_opt.value();
        
        if (!result.valid || result.bytes_consumed == 0)
            break;

        handle_message(result);

        // Remove processed bytes from buffer
        message_buffer_.erase(message_buffer_.begin(),
                              message_buffer_.begin() + result.bytes_consumed);
    }
}

bool OrderBook::add_order(const Order& order)
{
    auto [it, inserted] = orders_.emplace(order.order_id, order);
    if (!inserted) return false;

    // Create OrderInfo for bid/ask processor
    OrderInfo& info = order_info_[order.order_id];
    
    // Convert char side to Side enum
    Side book_side = (order.side == 'B' || order.side == 'b') ? Side::Bid : Side::Ask;
    
    // Add to price-level book
    book_.onAdd(order.order_id, book_side, order.price, order.quantity, info);
    
    // Link Order to OrderInfo
    it->second.book_info = &info;

    if (callback_)
    {
        callback_('A', order);
    }
    return true;
}

bool OrderBook::cancel_order(uint64_t order_id)
{
    auto it = orders_.find(order_id);
    if (it == orders_.end())
    {
        error_stats_.invalid_operations++;
        return false;
    }

    // Remove from bid/ask processor
    auto info_it = order_info_.find(order_id);
    if (info_it != order_info_.end())
    {
        book_.onCancel(order_id, info_it->second);
        order_info_.erase(info_it);
    }

    it->second.active = false;
    if (callback_) callback_('X', it->second);

    // Cleanup
    orders_.erase(it);
    return true;
}

bool OrderBook::execute_order(uint64_t order_id, uint32_t quantity)
{
    auto it = orders_.find(order_id);
    if (it == orders_.end() || !it->second.active || it->second.quantity < quantity)
    {
        error_stats_.invalid_operations++;
        return false;
    }

    // Update quantity
    it->second.quantity -= quantity;
    bool fully_filled = (it->second.quantity == 0);
    if (fully_filled) it->second.active = false;

    // Update bid/ask processor
    auto info_it = order_info_.find(order_id);
    if (info_it != order_info_.end())
    {
        book_.onExecute(order_id, info_it->second, quantity);
        if (fully_filled) order_info_.erase(info_it);
    }

    if (callback_) callback_('E', it->second);

    // Cleanup if fully filled
    if (fully_filled) orders_.erase(it);

    return true;
}

bool OrderBook::replace_order(uint64_t old_order_id, uint64_t new_order_id, uint32_t new_price, uint32_t new_quantity)
{
    auto it = orders_.find(old_order_id);
    if (it == orders_.end() || !it->second.active)
    {
        error_stats_.invalid_operations++;
        return false;
    }

    // Save original order data
    char side = it->second.side;
    uint64_t timestamp = it->second.timestamp;

    // Get OrderInfo for bid/ask processor
    auto info_it = order_info_.find(old_order_id);
    if (info_it != order_info_.end())
    {
        book_.onCancel(old_order_id, info_it->second);
        order_info_.erase(info_it);
    }

    // Remove old order
    orders_.erase(it);

    // Add new order with new reference number
    Order new_order(new_order_id, new_price, new_quantity, side, timestamp);
    auto [new_it, inserted] = orders_.emplace(new_order_id, new_order);
    if (!inserted)
        return false;

    // Create OrderInfo for bid/ask processor
    OrderInfo& info = order_info_[new_order_id];
    
    // Convert char side to Side enum
    Side book_side = (side == 'B' || side == 'b') ? Side::Bid : Side::Ask;
    
    // Add to price-level book
    book_.onAdd(new_order_id, book_side, new_price, new_quantity, info);
    
    // Link Order to OrderInfo
    new_it->second.book_info = &info;

    if (callback_)
    {
        callback_('U', new_it->second);
    }

    return true;
}

const Order* OrderBook::find_order(uint64_t order_id) const
{
    auto it = orders_.find(order_id);
    if (it == orders_.end() || !it->second.active)
        return nullptr;
    return &it->second;
}

size_t OrderBook::get_active_order_count() const
{
    size_t count = 0;
    for (const auto& [id, order] : orders_)
    {
        if (order.active)
            ++count;
    }
    return count;
}

void OrderBook::handle_message(const ITCHParser::ParseResult& result)
{
    if (result.type == 'A')
    {
        Order order(result.order_id, result.price, result.quantity, result.side, result.timestamp);
        add_order(order);
    }
    else if (result.type == 'X')  // 'X' = Cancel per ITCH 5.0 spec
    {
        cancel_order(result.order_id);
    }
    else if (result.type == 'E')
    {
        execute_order(result.order_id, result.quantity);
    }
    else if (result.type == 'U')  // 'U' = Replace per ITCH 5.0 spec
    {
        replace_order(result.order_id, result.new_order_id, result.price, result.quantity);
    }
}

void OrderBook::print_orders(std::ostream& os) const
{
    os << "OrderBook: " << get_active_order_count() << " active orders\n";
    os << std::setw(12) << "OrderID" << std::setw(10) << "Price" << std::setw(10) << "Quantity"
       << std::setw(6) << "Side" << std::setw(15) << "Timestamp" << std::setw(10) << "Active"
       << "\n";
    os << std::string(73, '-') << "\n";

    for (const auto& [id, order] : orders_)
    {
        os << std::setw(12) << order.order_id << std::setw(10) << order.price << std::setw(10)
           << order.quantity << std::setw(6) << order.side << std::setw(15) << order.timestamp
           << std::setw(10) << (order.active ? "Yes" : "No") << "\n";
    }
}

// ============================================================================
// Market Data API Implementation
// ============================================================================

bool OrderBook::get_best_bid(uint64_t& price_out, uint64_t& qty_out) const
{
    return book_.getBestBid(price_out, qty_out);
}

bool OrderBook::get_best_ask(uint64_t& price_out, uint64_t& qty_out) const
{
    return book_.getBestAsk(price_out, qty_out);
}

bool OrderBook::get_spread(uint64_t& spread_out) const
{
    uint64_t bid_price, bid_qty, ask_price, ask_qty;
    
    if (!book_.getBestBid(bid_price, bid_qty)) return false;
    if (!book_.getBestAsk(ask_price, ask_qty)) return false;
    
    if (ask_price <= bid_price) return false;  // Crossed market
    
    spread_out = ask_price - bid_price;
    return true;
}

OrderBook::MarketDepth OrderBook::get_depth(size_t levels) const
{
    MarketDepth depth;
    depth.bids = book_.getTopKBids(levels);
    depth.asks = book_.getTopKAsks(levels);
    return depth;
}
