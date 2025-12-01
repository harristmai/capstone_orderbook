#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <tuple>

// ----------------------------
// Basic types
// ----------------------------

enum class Side : uint8_t { Bid = 0, Ask = 1 };

// Forward declaration
struct OrderNode;

// Shared order table entry
struct OrderInfo {
    Side side;
    uint64_t price;
    uint64_t quantity;
    OrderNode* node;

    OrderInfo() : side(Side::Bid), price(0), quantity(0), node(nullptr) {}
};

// ----------------------------
// Internal order book structs
// ----------------------------

// Node in the FIFO queue at a price level
struct OrderNode {
    uint64_t order_id;
    uint64_t quantity;
    OrderNode* prev;
    OrderNode* next;
};

// One price level: FIFO + aggregate qty
struct PriceLevel {
    uint64_t price;
    uint64_t total_qty;
    OrderNode* head;
    OrderNode* tail;

    PriceLevel(uint64_t p = 0)
        : price(p), total_qty(0), head(nullptr), tail(nullptr) {}
};

// ----------------------------
// BookSide: one side of book
// ----------------------------
class BookSide {
public:
    using LevelMap = std::map<uint64_t, PriceLevel>;

    explicit BookSide(Side s) : side_(s) {}

    // return pointer to FIFO node
    OrderNode* addOrder(uint64_t order_id, uint64_t price, uint64_t qty);

    void cancelOrder(OrderNode* node, uint64_t price);

    // Match an aggressive order against this side's best prices
    uint64_t matchAtBest(
        uint64_t incoming_qty,
        std::vector<std::tuple<uint64_t,uint64_t,uint64_t>>& trades
    );

    bool empty() const { return levels_.empty(); }

    bool bestPrice(uint64_t& price_out, uint64_t& qty_out) const;

    // Get top-K (price, total_qty) depth for this side
    std::vector<std::pair<uint64_t,uint64_t>> topK(std::size_t k) const;

    void updateQuantity(OrderNode* node, uint64_t price, uint64_t new_qty);

private:
    Side side_;
    LevelMap levels_;

    PriceLevel& getOrCreateLevel(uint64_t price);
};

// ----------------------------
// OrderBookEngine: combining both sides
// ----------------------------
class OrderBookEngine {
public:
    OrderBookEngine()
        : bids_(Side::Bid), asks_(Side::Ask) {}

    void onAdd(uint64_t order_id,
               Side side,
               uint64_t price,
               uint64_t qty,
               OrderInfo& info_out);

    void onCancel(uint64_t order_id, OrderInfo& info);

    void onExecute(uint64_t order_id, OrderInfo& info, uint64_t executed_qty);

    // Aggressive incoming order that trades against opposite side
    uint64_t onAggressive(Side taking_side,
                          uint64_t qty,
                          std::vector<std::tuple<uint64_t,uint64_t,uint64_t>>& trades);

    bool getBestBid(uint64_t& price_out, uint64_t& qty_out) const;
    bool getBestAsk(uint64_t& price_out, uint64_t& qty_out) const;
    
    std::vector<std::pair<uint64_t,uint64_t>> getTopKBids(std::size_t k) const;
    std::vector<std::pair<uint64_t,uint64_t>> getTopKAsks(std::size_t k) const;

private:
    BookSide bids_;
    BookSide asks_;
};
