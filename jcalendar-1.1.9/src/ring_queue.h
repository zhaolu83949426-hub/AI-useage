#pragma once

#include <stdint.h>
#include <string.h>

template <size_t Capacity, size_t MaxBytes>
class PacketQueue {
public:
    struct Item {
        uint8_t data[MaxBytes];
        uint16_t len;
    };

    bool push(const uint8_t* data, uint16_t len) {
        if (!data || len == 0 || len > MaxBytes || full()) {
            return false;
        }
        memcpy(items_[head_].data, data, len);
        items_[head_].len = len;
        head_ = next(head_);
        return true;
    }

    bool pop(Item& item) {
        if (empty()) {
            return false;
        }
        item = items_[tail_];
        tail_ = next(tail_);
        return true;
    }

    bool empty() const {
        return head_ == tail_;
    }

    bool full() const {
        return next(head_) == tail_;
    }

private:
    size_t next(size_t value) const {
        return (value + 1) % Capacity;
    }

    Item items_[Capacity] = {};
    size_t head_ = 0;
    size_t tail_ = 0;
};
