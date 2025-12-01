#include "bid_ask.h"

// ============================================================================
// BookSide Implementation
// ============================================================================

OrderNode* BookSide::addOrder(uint64_t order_id, uint64_t price, uint64_t qty) {
    PriceLevel& level = getOrCreateLevel(price);

    OrderNode* node = new OrderNode{order_id, qty, nullptr, nullptr};

    // FIFO enqueue at tail
    if (!level.tail) {
        level.head = node;
        level.tail = node;
    } else {
        level.tail->next = node;
        node->prev = level.tail;
        level.tail = node;
    }

    level.total_qty += qty;
    return node;
}

void BookSide::cancelOrder(OrderNode* node, uint64_t price) {
    if (!node) return;

    auto it = levels_.find(price);
    if (it == levels_.end()) return;

    PriceLevel& level = it->second;
    level.total_qty -= node->quantity;

    // Unlink from doubly-linked FIFO
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        level.head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        level.tail = node->prev;
    }

    delete node;

    if (!level.head) {
        levels_.erase(it);
    }
}

void BookSide::updateQuantity(OrderNode* node, uint64_t price, uint64_t new_qty) {
    if (!node) return;

    auto it = levels_.find(price);
    if (it == levels_.end()) return;

    PriceLevel& level = it->second;
    
    // Update aggregate quantity
    level.total_qty = level.total_qty - node->quantity + new_qty;
    
    // Update node quantity
    node->quantity = new_qty;

    // If quantity is zero, remove the node
    if (new_qty == 0) {
        // Unlink from FIFO
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            level.head = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            level.tail = node->prev;
        }

        delete node;

        if (!level.head) {
            levels_.erase(it);
        }
    }
}

uint64_t BookSide::matchAtBest(
    uint64_t incoming_qty,
    std::vector<std::tuple<uint64_t,uint64_t,uint64_t>>& trades
) {
    uint64_t filled = 0;

    while (incoming_qty > 0 && !levels_.empty()) {
        LevelMap::iterator it =
            (side_ == Side::Bid) ? std::prev(levels_.end()) : levels_.begin();

        PriceLevel& level = it->second;
        OrderNode* node = level.head;
        
        while (node && incoming_qty > 0) {
            uint64_t trade_qty = (node->quantity < incoming_qty)
                                 ? node->quantity
                                 : incoming_qty;

            trades.emplace_back(node->order_id, trade_qty, level.price);

            node->quantity  -= trade_qty;
            level.total_qty -= trade_qty;
            incoming_qty    -= trade_qty;
            filled          += trade_qty;

            if (node->quantity == 0) {
                OrderNode* to_delete = node;
                node = node->next;

                if (to_delete->prev) {
                    to_delete->prev->next = to_delete->next;
                } else {
                    level.head = to_delete->next;
                }
                if (to_delete->next) {
                    to_delete->next->prev = to_delete->prev;
                } else {
                    level.tail = to_delete->prev;
                }

                delete to_delete;
            } else {
                break;
            }
        }

        if (!level.head) {
            levels_.erase(it);
        }

        if (incoming_qty == 0) break;
    }

    return filled;
}

bool BookSide::bestPrice(uint64_t& price_out, uint64_t& qty_out) const {
    if (levels_.empty()) return false;

    LevelMap::const_iterator it =
        (side_ == Side::Bid) ? std::prev(levels_.end()) : levels_.begin();

    price_out = it->second.price;
    qty_out   = it->second.total_qty;
    return true;
}

std::vector<std::pair<uint64_t,uint64_t>> BookSide::topK(std::size_t k) const {
    std::vector<std::pair<uint64_t,uint64_t>> result;
    result.reserve(k);

    if (levels_.empty() || k == 0) return result;

    if (side_ == Side::Bid) {
        for (auto it = levels_.rbegin();
             it != levels_.rend() && result.size() < k; ++it) {
            if (it->second.total_qty > 0) {
                result.emplace_back(it->second.price, it->second.total_qty);
            }
        }
    } else {
        for (auto it = levels_.begin();
             it != levels_.end() && result.size() < k; ++it) {
            if (it->second.total_qty > 0) {
                result.emplace_back(it->second.price, it->second.total_qty);
            }
        }
    }

    return result;
}

PriceLevel& BookSide::getOrCreateLevel(uint64_t price) {
    auto it = levels_.find(price);
    if (it == levels_.end()) {
        it = levels_.emplace(price, PriceLevel(price)).first;
    }
    return it->second;
}

// ============================================================================
// OrderBookEngine Implementation
// ============================================================================

void OrderBookEngine::onAdd(uint64_t order_id,
                            Side side,
                            uint64_t price,
                            uint64_t qty,
                            OrderInfo& info_out) {
    OrderNode* node =
        (side == Side::Bid)
            ? bids_.addOrder(order_id, price, qty)
            : asks_.addOrder(order_id, price, qty);

    info_out.side     = side;
    info_out.price    = price;
    info_out.quantity = qty;
    info_out.node     = node;
}

void OrderBookEngine::onCancel(uint64_t /*order_id*/, OrderInfo& info) {
    if (!info.node) return;

    if (info.side == Side::Bid) {
        bids_.cancelOrder(info.node, info.price);
    } else {
        asks_.cancelOrder(info.node, info.price);
    }

    info.node     = nullptr;
    info.quantity = 0;
}

void OrderBookEngine::onExecute(uint64_t /*order_id*/, OrderInfo& info, uint64_t executed_qty) {
    if (!info.node) return;
    if (info.quantity < executed_qty) return;

    uint64_t new_qty = info.quantity - executed_qty;
    info.quantity = new_qty;

    if (info.side == Side::Bid) {
        bids_.updateQuantity(info.node, info.price, new_qty);
    } else {
        asks_.updateQuantity(info.node, info.price, new_qty);
    }

    if (new_qty == 0) {
        info.node = nullptr;
    }
}

uint64_t OrderBookEngine::onAggressive(Side taking_side,
                                       uint64_t qty,
                                       std::vector<std::tuple<uint64_t,uint64_t,uint64_t>>& trades) {
    if (taking_side == Side::Bid) {
        return asks_.matchAtBest(qty, trades);
    } else {
        return bids_.matchAtBest(qty, trades);
    }
}

bool OrderBookEngine::getBestBid(uint64_t& price_out, uint64_t& qty_out) const {
    return bids_.bestPrice(price_out, qty_out);
}

bool OrderBookEngine::getBestAsk(uint64_t& price_out, uint64_t& qty_out) const {
    return asks_.bestPrice(price_out, qty_out);
}

std::vector<std::pair<uint64_t,uint64_t>>
OrderBookEngine::getTopKBids(std::size_t k) const {
    return bids_.topK(k);
}

std::vector<std::pair<uint64_t,uint64_t>>
OrderBookEngine::getTopKAsks(std::size_t k) const {
    return asks_.topK(k);
}
