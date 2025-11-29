# Bid/Ask Processor Design

## What is a Bid/Ask Processor?

The **Bid/Ask Processor** (also called **Price Level Book** or **Market Depth**) aggregates individual orders by price level to show:
- Best Bid (highest buy price)
- Best Ask/Offer (lowest sell price)
- Market depth at each price level
- Total volume available

## Input Requirements

### What the Bid/Ask Processor Needs:

1. **Order Events** (from OrderBook callbacks):
   ```cpp
   - Type: Add/Cancel/Execute
   - Order ID
   - Price
   - Quantity
   - Side (B or S)
   - Timestamp
   ```

2. **Not the Full OrderBook State**:
   - The processor doesn't need to scan the entire orderbook
   - It receives **incremental updates** via events
   - This is much more efficient for high-frequency updates

## Data Structure for Bid/Ask Processor

### Core Data Structure: Price Level Map

```cpp
struct PriceLevel {
    uint32_t price;
    uint64_t total_quantity;    // Sum of all orders at this price
    size_t order_count;         // Number of orders at this price
    
    // Optional: track individual orders for detailed views
    std::unordered_map<uint64_t, uint32_t> orders;  // order_id -> quantity
};

class BidAskProcessor {
private:
    // Key insight: Use ordered maps for fast best bid/ask lookup
    std::map<uint32_t, PriceLevel, std::greater<uint32_t>> bids_;  // Descending (best bid first)
    std::map<uint32_t, PriceLevel, std::less<uint32_t>>    asks_;  // Ascending (best ask first)
};
```

### Why `std::map` instead of `unordered_map`?

- **Sorted by price** automatically
- **Best bid** = bids_.begin() → O(1)
- **Best ask** = asks_.begin() → O(1)
- **Market depth** = iterate in price order

## How It Reads OrderBook State

### Method 1: Event-Driven (Recommended for Real-Time)

The processor **subscribes to OrderBook events** and updates incrementally:

```cpp
orderbook.set_event_callback([&processor](char type, const Order& order) {
    processor.handle_event(type, order);
});
```

**Advantages:**
- ✅ Real-time updates
- ✅ Low latency
- ✅ No scanning needed
- ✅ Scales well with order volume

**Event Handling:**
```cpp
void BidAskProcessor::handle_event(char type, const Order& order) {
    if (type == 'A') {
        add_to_price_level(order.side, order.price, order.order_id, order.quantity);
    }
    else if (type == 'C') {
        remove_from_price_level(order.side, order.price, order.order_id);
    }
    else if (type == 'E') {
        reduce_price_level(order.side, order.price, order.order_id, executed_qty);
    }
}
```

### Method 2: Snapshot (For Initialization)

On startup, the processor can build initial state from orderbook:

```cpp
void BidAskProcessor::initialize_from_orderbook(const OrderBook& book) {
    // Scan all active orders once at startup
    for (const auto& [id, order] : book.get_all_orders()) {
        if (order.active) {
            add_to_price_level(order.side, order.price, order.order_id, order.quantity);
        }
    }
}
```

Then switch to event-driven updates.

## Complete Example Design

```cpp
class BidAskProcessor {
public:
    struct PriceLevel {
        uint32_t price;
        uint64_t total_quantity;
        size_t order_count;
        std::unordered_map<uint64_t, uint32_t> orders;  // order_id -> qty
    };
    
    struct BookState {
        uint32_t best_bid_price;
        uint32_t best_ask_price;
        uint64_t best_bid_qty;
        uint64_t best_ask_qty;
        uint32_t spread;  // ask - bid
    };

    // Handle events from OrderBook
    void handle_event(char type, const Order& order) {
        if (type == 'A') {
            add_order(order);
        } else if (type == 'C') {
            cancel_order(order);
        } else if (type == 'E') {
            execute_order(order);
        }
    }

    // Get current market state
    BookState get_book_state() const {
        BookState state{};
        if (!bids_.empty()) {
            state.best_bid_price = bids_.begin()->first;
            state.best_bid_qty = bids_.begin()->second.total_quantity;
        }
        if (!asks_.empty()) {
            state.best_ask_price = asks_.begin()->first;
            state.best_ask_qty = asks_.begin()->second.total_quantity;
        }
        if (!bids_.empty() && !asks_.empty()) {
            state.spread = state.best_ask_price - state.best_bid_price;
        }
        return state;
    }

    // Get market depth (top N levels)
    std::vector<PriceLevel> get_bid_depth(size_t levels) const {
        std::vector<PriceLevel> depth;
        auto it = bids_.begin();
        for (size_t i = 0; i < levels && it != bids_.end(); ++i, ++it) {
            depth.push_back(it->second);
        }
        return depth;
    }

    std::vector<PriceLevel> get_ask_depth(size_t levels) const {
        std::vector<PriceLevel> depth;
        auto it = asks_.begin();
        for (size_t i = 0; i < levels && it != asks_.end(); ++i, ++it) {
            depth.push_back(it->second);
        }
        return depth;
    }

private:
    void add_order(const Order& order) {
        auto& levels = (order.side == 'B') ? bids_ : asks_;
        
        auto& level = levels[order.price];
        level.price = order.price;
        level.total_quantity += order.quantity;
        level.order_count++;
        level.orders[order.order_id] = order.quantity;
    }

    void cancel_order(const Order& order) {
        auto& levels = (order.side == 'B') ? bids_ : asks_;
        
        auto it = levels.find(order.price);
        if (it != levels.end()) {
            auto& level = it->second;
            auto order_it = level.orders.find(order.order_id);
            if (order_it != level.orders.end()) {
                level.total_quantity -= order_it->second;
                level.order_count--;
                level.orders.erase(order_it);
                
                // Remove price level if empty
                if (level.order_count == 0) {
                    levels.erase(it);
                }
            }
        }
    }

    void execute_order(const Order& order) {
        auto& levels = (order.side == 'B') ? bids_ : asks_;
        
        auto it = levels.find(order.price);
        if (it != levels.end()) {
            auto& level = it->second;
            auto order_it = level.orders.find(order.order_id);
            if (order_it != level.orders.end()) {
                uint32_t old_qty = order_it->second;
                uint32_t new_qty = order.quantity;  // Quantity remaining after execution
                uint32_t executed = old_qty - new_qty;
                
                level.total_quantity -= executed;
                
                if (new_qty == 0) {
                    level.orders.erase(order_it);
                    level.order_count--;
                    if (level.order_count == 0) {
                        levels.erase(it);
                    }
                } else {
                    order_it->second = new_qty;
                }
            }
        }
    }

    // Bids: descending order (highest price first)
    std::map<uint32_t, PriceLevel, std::greater<uint32_t>> bids_;
    
    // Asks: ascending order (lowest price first)
    std::map<uint32_t, PriceLevel, std::less<uint32_t>> asks_;
};
```

## Integration with OrderBook

```cpp
int main() {
    DataFabric fabric;
    OrderBook orderbook(fabric);
    BidAskProcessor processor;
    
    // Connect processor to orderbook events
    orderbook.set_event_callback([&processor](char type, const Order& order) {
        processor.handle_event(type, order);
        
        // Display market state after each update
        auto state = processor.get_book_state();
        std::cout << "Best Bid: " << state.best_bid_price 
                  << " (" << state.best_bid_qty << ")\n";
        std::cout << "Best Ask: " << state.best_ask_price 
                  << " (" << state.best_ask_qty << ")\n";
        std::cout << "Spread: " << state.spread << "\n\n";
    });
    
    // Process messages
    orderbook.process();
}
```

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|----------------|-------|
| Add order | O(log P) | P = number of price levels |
| Cancel order | O(log P) | Find price level |
| Execute order | O(log P) | Update quantity |
| Get best bid | O(1) | First element in map |
| Get best ask | O(1) | First element in map |
| Get depth (N levels) | O(N) | Iterate N entries |

**Key Insight**: P (price levels) << N (total orders), so O(log P) is very fast!

## Memory Optimization

### Option 1: Track Individual Orders (Detailed)
```cpp
std::unordered_map<uint64_t, uint32_t> orders;  // Per price level
```
- **Pros**: Can handle partial executions, cancels specific orders
- **Cons**: Higher memory usage
- **Use case**: Full order tracking

### Option 2: Aggregate Only (Compact)
```cpp
struct PriceLevel {
    uint32_t price;
    uint64_t total_quantity;  // Just the sum
    size_t order_count;       // Count only
};
```
- **Pros**: Lower memory, faster
- **Cons**: Can't track individual orders
- **Use case**: Market data feed, display only

## What Data Flows Between Components

```
OrderBook                    BidAskProcessor
┌──────────────┐            ┌──────────────────┐
│              │            │                  │
│ Individual   │  Events    │  Price Level     │
│ Orders       │ ─────────> │  Aggregation     │
│              │  (A/C/E)   │                  │
│              │            │  bids_: map      │
│ Hash Map     │            │  asks_: map      │
│ by order_id  │            │                  │
│              │            │  Best Bid/Ask    │
│              │            │  Market Depth    │
└──────────────┘            └──────────────────┘
```

**Flow:**
1. OrderBook receives ITCH messages
2. OrderBook updates its internal order map
3. OrderBook fires event callbacks
4. BidAskProcessor handles event
5. BidAskProcessor updates price level maps
6. BidAskProcessor can answer "what's the best bid/ask?" in O(1)

## Summary

### Input to Bid/Ask Processor:
✅ **Event-driven**: Receives Add/Cancel/Execute events  
✅ **Incremental updates**: No need to scan entire orderbook  
✅ **Real-time**: Updates immediately as events occur

### Data Structure:
✅ **`std::map`** for bids (descending) and asks (ascending)  
✅ **Price levels** with aggregated quantity and order count  
✅ **Optional order tracking** for detailed views

### How It Reads OrderBook:
✅ **Subscribe to events** via callback  
✅ **No direct orderbook access needed** after initialization  
✅ **Self-contained state** maintained by processor

This design is used in real trading systems because it's:
- **Fast**: O(1) for best bid/ask
- **Scalable**: Handles high message rates
- **Decoupled**: OrderBook and Processor are independent
- **Real-time**: Updates as events occur
