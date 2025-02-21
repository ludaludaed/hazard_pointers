#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include <lu/detail/activae_list.h>
#include <lu/detail/static_bucket_traits.h>
#include <lu/detail/utils.h>
#include <lu/intrusive/options.h>
#include <lu/intrusive/unordered_set.h>
#include <lu/utils/fixed_size_function.h>

#include <atomic>
#include <cassert>


namespace lu {
namespace detail {

template<class Pointer>
struct DefaultDetacher {
    void operator()(Pointer value) const {}
};

template<class Pointer>
struct DefaultCreator {
    using value_type = typename std::pointer_traits<Pointer>::element_type;

    Pointer operator()() const {
        return new value_type();
    }
};

template<class Pointer>
struct DefaultDeleter {
    using value_type = typename std::pointer_traits<Pointer>::element_type;

    void operator()(Pointer value) const {
        delete value;
    }
};

}// namespace detail

class thread_local_list_base_hook : public lu::unordered_set_base_hook<>, public lu::active_list_base_hook<> {
    template<class>
    friend class thread_local_list;

    void *key{};
};

template<class ValueType>
class thread_local_list : private lu::active_list<ValueType> {
    using Base = lu::active_list<ValueType>;

public:
    using value_type = ValueType;

    using pointer = typename Base::pointer;
    using const_pointer = typename Base::const_pointer;
    using reference = typename Base::reference;
    using const_reference = typename Base::const_reference;

    using iterator = typename Base::iterator;
    using const_iterator = typename Base::const_iterator;

    using Base::begin;
    using Base::end;

    using Base::cbegin;
    using Base::cend;

private:
    class ThreadLocalOwner {
        using key_type = const thread_local_list *;

        struct KeyOfValue {
            using type = key_type;

            type operator()(const thread_local_list_base_hook &value) const {
                return reinterpret_cast<type>(value.key);
            }
        };

        using BucketTraits = detail::StaticBucketTraits<8, unordered_bucket_type<base_hook<unordered_set_base_hook<>>>>;
        using UnorderedSet = lu::unordered_set<value_type, lu::key_of_value<KeyOfValue>, lu::is_power_2_buckets<true>,
                                               lu::hash<detail::PointerHash>, lu::bucket_traits<BucketTraits>>;

    public:
        ThreadLocalOwner() = default;

        ~ThreadLocalOwner() {
            auto current = set_.begin();
            while (current != set_.end()) {
                auto prev = current++;
                detach(*prev);
            }
        }

        pointer get_entry(key_type key) noexcept {
            auto found = set_.find(key);
            if (found == set_.end()) {
                return {};
            }
            return found.operator->();
        }

        void attach(reference value) noexcept {
            set_.insert(value);
        }

        void detach(key_type key) noexcept {
            auto found = set_.find(key);
            if (found != set_.end()) [[likely]] {
                detach(*found);
            }
        }

        void detach(reference value) {
            auto list = reinterpret_cast<key_type>(value.key);
            list->detacher_(&value);
            set_.erase(set_.iterator_to(value));
            value.release();
        }

    private:
        UnorderedSet set_{};
    };

public:
    template<class Detacher = detail::DefaultDetacher<pointer>, class Creator = detail::DefaultCreator<pointer>,
             class Deleter = detail::DefaultDeleter<pointer>>
    explicit thread_local_list(Detacher detacher = {}, Creator creator = {}, Deleter deleter = {})
        : detacher_(std::move(detacher))
        , creator_(std::move(creator))
        , deleter_(std::move(deleter)) {}

    thread_local_list(const thread_local_list &) = delete;

    thread_local_list(thread_local_list &&) = delete;

    ~thread_local_list() {
        auto current = begin();
        while (current != end()) {
            auto prev = current++;
            bool acquired = prev->is_acquired(std::memory_order_acquire);
            UNUSED(acquired);
            assert(!acquired && "Can't clear while all threads aren't detached");
            deleter_(prev.operator->());
        }
    }

private:
    ThreadLocalOwner &get_owner() {
        static thread_local ThreadLocalOwner owner;
        return owner;
    }

    pointer find_or_create() {
        auto found = this->find_free();
        if (found != end()) {
            return found.operator->();
        } else {
            auto new_item = creator_();
            new_item->key = this;
            this->push(*new_item);
            return new_item;
        }
    }

public:
    void attach_thread() {
        auto &owner = get_owner();
        auto result = owner.get_entry(this);
        if (!result) [[likely]] {
            auto new_item = find_or_create();
            owner.attach(*new_item);
        }
    }

    void detach_thread() {
        auto &owner = get_owner();
        owner.detach(this);
    }

    reference get_thread_local() {
        auto &owner = get_owner();
        auto result = owner.get_entry(this);
        if (!result) [[unlikely]] {
            result = find_or_create();
            owner.attach(*result);
        }
        return *result;
    }

private:
    lu::fixed_size_function<pointer(), 64> creator_;
    lu::fixed_size_function<void(pointer), 64> deleter_;
    lu::fixed_size_function<void(pointer), 64> detacher_;
};

}// namespace lu

#endif
