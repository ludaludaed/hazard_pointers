#ifndef __STRUCTURES_H__
#define __STRUCTURES_H__

#include <lu/atomic_shared_ptr.h>
#include <lu/hazard_pointer.h>

#include <atomic>
#include <optional>


namespace lu {
namespace asp {

template <class ValueType, class BackOff>
class TreiberStack {
    struct Node {
        ValueType value{};
        lu::shared_ptr<Node> next{};

        template <class... Args>
        Node(Args &&...args)
            : value(std::forward<Args>(args)...) {}
    };

public:
    void push(ValueType value) {
        BackOff back_off;
        auto new_node = lu::make_shared<Node>(value);
        auto head = head_.load();
        new_node->next = head;
        while (true) {
            if (head_.compare_exchange_weak(new_node->next, new_node)) {
                return;
            }
            back_off();
        }
    }

    std::optional<ValueType> pop() {
        BackOff back_off;
        while (true) {
            lu::shared_ptr<Node> head = head_.load();
            if (!head) {
                return std::nullopt;
            }
            if (head_.compare_exchange_weak(head, head->next)) {
                return {head->value};
            }
            back_off();
        }
    }

private:
    lu::atomic_shared_ptr<Node> head_{};
};

template <class ValueType, class BackOff>
class MSQueue {
    struct Node {
        ValueType value{};
        lu::atomic_shared_ptr<Node> next{};

        Node() = default;

        template <class... Args>
        Node(Args &&...args)
            : value(std::forward<Args>(args)...) {}
    };

public:
    MSQueue() {
        auto dummy = lu::make_shared<Node>();
        head_.store(dummy);
        tail_.store(dummy);
    }

    void push(ValueType value) {
        BackOff back_off;
        auto new_item = lu::make_shared<Node>(value);
        while (true) {
            auto tail = tail_.load();
            if (tail->next.load()) {
                tail_.compare_exchange_weak(tail, tail->next.load());
            } else {
                lu::shared_ptr<Node> null;
                if (tail->next.compare_exchange_weak(null, new_item)) {
                    tail_.compare_exchange_weak(tail, new_item);
                    return;
                }
            }
            back_off();
        }
    }

    std::optional<ValueType> pop() {
        BackOff back_off;
        while (true) {
            auto head = head_.load();
            auto head_next = head->next.load();
            if (!head_next) {
                return std::nullopt;
            }
            if (head_.compare_exchange_weak(head, head_next)) {
                return {head_next->value};
            }
            back_off();
        }
    }

private:
    lu::atomic_shared_ptr<Node> head_;
    lu::atomic_shared_ptr<Node> tail_;
};

}// namespace asp

namespace hp {
template <class ValueType, class BackOff>
class TreiberStack {
    struct Node : public lu::hazard_pointer_obj_base<Node> {
        template <class... Args>
        Node(Args &&...args)
            : value(std::forward<Args>(args)...) {}

        ValueType value;
        Node *next{};
    };

public:
    ~TreiberStack() {
        auto head = head_.load(std::memory_order_acquire);
        while (head) {
            auto next = head->next;
            head->retire();
            head = next;
        }
    }

    void push(ValueType value) {
        BackOff back_off;
        auto new_node = new Node(value);
        auto head = head_.load(std::memory_order_relaxed);
        new_node->next = head;
        while (true) {
            if (head_.compare_exchange_weak(new_node->next, new_node, std::memory_order_release)) {
                return;
            }
            back_off();
        }
    }

    std::optional<ValueType> pop() {
        BackOff back_off;
        auto head_guard = lu::make_hazard_pointer();
        while (true) {
            auto head = head_guard.protect(head_);
            if (!head) {
                return std::nullopt;
            }
            if (head_.compare_exchange_strong(head, head->next, std::memory_order_relaxed)) {
                head->retire();
                return {std::move(head->value)};
            }
            back_off();
        }
    }

private:
    std::atomic<Node *> head_{nullptr};
};

template <class ValueType, class BackOff>
class MSQueue {
    struct Node : lu::hazard_pointer_obj_base<Node> {
        ValueType value{};
        std::atomic<Node *> next{};

        Node() = default;

        template <class... Args>
        Node(Args &&...args)
            : value(std::forward<Args>(args)...) {}
    };

public:
    MSQueue() {
        auto dummy_node = new Node();
        head_.store(dummy_node);
        tail_.store(dummy_node);
    }

    ~MSQueue() {
        auto head = head_.load(std::memory_order_acquire);
        while (head) {
            auto next = head->next.load(std::memory_order_acquire);
            head->retire();
            head = next;
        }
    }

    template <class... Args>
    void push(Args &&...args) {
        BackOff back_off;

        auto new_node = new Node(std::forward<Args>(args)...);
        auto tail_guard = lu::make_hazard_pointer();

        while (true) {
            auto tail = tail_guard.protect(tail_);
            auto tail_next = tail->next.load(std::memory_order_acquire);
            if (tail_next) {
                tail_.compare_exchange_weak(tail, tail_next, std::memory_order_release);
            } else {
                if (tail->next.compare_exchange_weak(tail_next,
                                                     new_node,
                                                     std::memory_order_release)) {
                    tail_.compare_exchange_weak(tail, new_node, std::memory_order_release);
                    return;
                }
            }
            back_off();
        }
    }

    std::optional<ValueType> pop() {
        BackOff back_off;
        auto head_guard = lu::make_hazard_pointer();
        auto head_next_guard = lu::make_hazard_pointer();
        while (true) {
            auto head = head_guard.protect(head_);
            auto head_next = head_next_guard.protect(head->next);
            if (!head_next) {
                return std::nullopt;
            }
            if (head_.compare_exchange_weak(head, head_next, std::memory_order_release)) {
                head->retire();
                return {std::move(head_next->value)};
            }
            back_off();
        }
    }

private:
    std::atomic<Node *> head_;
    std::atomic<Node *> tail_;
};

}// namespace hp
}// namespace lu

#endif
