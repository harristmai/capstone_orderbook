#include "orderbook.h"

#include <iomanip>
#include <iostream>

// ============================================================================
// ITCH Parser Implementation
// ============================================================================

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

ITCHParser::ParseResult ITCHParser::parse_one(const std::vector<uint8_t>& buffer) const
{
    ParseResult result{0, false, 0, 0, 0, 0, 0, 0};

    if (buffer.empty())
        return result;

    char msg_type = static_cast<char>(buffer[0]);
    size_t offset = 1;

    // Add Order: 'A' + order_id(8) + price(4) + quantity(4) + side(1) + timestamp(8) = 26
    if (msg_type == 'A')
    {
        if (buffer.size() < ADD_MSG_SIZE)
            return result;

        result.type = 'A';
        result.order_id = read_u64(buffer, offset);
        result.price = read_u32(buffer, offset);
        result.quantity = read_u32(buffer, offset);
        result.side = static_cast<char>(buffer[offset++]);
        result.timestamp = read_u64(buffer, offset);
        result.bytes_consumed = ADD_MSG_SIZE;
        result.valid = true;
    }
    // Cancel Order: 'C' + order_id(8) = 9
    else if (msg_type == 'C')
    {
        if (buffer.size() < CANCEL_MSG_SIZE)
            return result;

        result.type = 'C';
        result.order_id = read_u64(buffer, offset);
        result.bytes_consumed = CANCEL_MSG_SIZE;
        result.valid = true;
    }
    // Execute Order: 'E' + order_id(8) + quantity(4) = 13
    else if (msg_type == 'E')
    {
        if (buffer.size() < EXECUTE_MSG_SIZE)
            return result;

        result.type = 'E';
        result.order_id = read_u64(buffer, offset);
        result.quantity = read_u32(buffer, offset);
        result.bytes_consumed = EXECUTE_MSG_SIZE;
        result.valid = true;
    }

    return result;
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

    // 2) Parse complete messages from buffer
    while (true)
    {
        auto result = parser_.parse_one(message_buffer_);
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
    if (inserted && callback_)
    {
        callback_('A', order);
    }
    return inserted;
}

bool OrderBook::cancel_order(uint64_t order_id)
{
    auto it = orders_.find(order_id);
    if (it == orders_.end())
        return false;

    it->second.active = false;

    if (callback_)
    {
        callback_('C', it->second);
    }

    return true;
}

bool OrderBook::execute_order(uint64_t order_id, uint32_t quantity)
{
    auto it = orders_.find(order_id);
    if (it == orders_.end() || !it->second.active)
        return false;

    if (it->second.quantity < quantity)
        return false;

    it->second.quantity -= quantity;
    if (it->second.quantity == 0)
    {
        it->second.active = false;
    }

    if (callback_)
    {
        callback_('E', it->second);
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
    else if (result.type == 'C')
    {
        cancel_order(result.order_id);
    }
    else if (result.type == 'E')
    {
        execute_order(result.order_id, result.quantity);
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
