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
#include <memory>
#include <type_traits>


namespace lu {
namespace detail {

template <class ValueType>
struct DefaultFactory {
    ValueType *operator()() const { return new ValueType(); }
};

}// namespace detail

template <class ValueType>
class thread_local_list_base_hook : public lu::unordered_set_base_hook<lu::is_auto_unlink<false>>,
                                    public lu::active_list_base_hook<> {
    template <class>
    friend class thread_local_list;

public:
    using deleter_func = void(ValueType *);

protected:
    thread_local_list_base_hook() = default;

public:
    template <class Deleter>
    void set_deleter(Deleter deleter) noexcept(std::is_nothrow_move_assignable_v<Deleter>) {
        deleter_ = std::move(deleter);
    }

    void on_attach() {}

    void on_detach() {}

private:
    void do_attach() { static_cast<ValueType *>(this)->on_attach(); }

    void do_detach() { static_cast<ValueType *>(this)->on_detach(); }

    void do_delete() { deleter_(static_cast<ValueType *>(this)); }

private:
    const void *key_{};
    lu::fixed_size_function<deleter_func, 64> deleter_{std::default_delete<ValueType>{}};
};

template <class ValueType>
class thread_local_list : private lu::active_list<ValueType> {
    using Base = lu::active_list<ValueType>;
    using Hook = lu::thread_local_list_base_hook<ValueType>;

public:
    using value_type = typename Base::value_type;

    using pointer = typename Base::pointer;
    using const_pointer = typename Base::const_pointer;
    using difference_type = typename Base::difference_type;

    using reference = typename Base::reference;
    using const_reference = typename Base::const_reference;

    using iterator = typename Base::iterator;
    using const_iterator = typename Base::const_iterator;

    using Base::begin;
    using Base::end;

    using Base::cbegin;
    using Base::cend;

    using factory_func = pointer();

private:
    class ThreadLocalOwner {
        struct KeyOfValue {
            using type = const thread_local_list *;

            type operator()(const Hook &value) const noexcept {
                return static_cast<type>(value.key_);
            }
        };

        using BucketTraits = detail::StaticBucketTraits<
                8, lu::unordered_bucket_type<lu::base_hook<lu::unordered_set_base_hook<>>>>;
        using UnorderedSet
                = lu::unordered_set<value_type, lu::key_of_value<KeyOfValue>,
                                    lu::is_power_2_buckets<true>, lu::hash<detail::FastPointerHash>,
                                    lu::bucket_traits<BucketTraits>>;

    public:
        ThreadLocalOwner() noexcept = default;

        ~ThreadLocalOwner() {
            auto current = set_.begin();
            while (current != set_.end()) {
                auto prev = current++;
                detach(*prev);
            }
        }

        pointer get_entry(const thread_local_list *list) noexcept {
            auto found = set_.find(list);
            if (found == set_.end()) {
                return {};
            }
            return found.operator->();
        }

        void attach(reference value) {
            set_.insert(value);
            value.do_attach();
        }

        void detach(const thread_local_list *list) {
            auto found = set_.find(list);
            if (found != set_.end()) [[likely]] {
                detach(*found);
            }
        }

    private:
        void detach(reference value) {
            value.do_detach();
            set_.erase(set_.iterator_to(value));
            value.release();
        }

    private:
        UnorderedSet set_{};
    };

public:
    thread_local_list() noexcept = default;

    template <class Factory>
    explicit thread_local_list(Factory factory)
        : factory_(std::move(factory)) {}

    thread_local_list(const thread_local_list &) = delete;

    thread_local_list(thread_local_list &&) = delete;

    ~thread_local_list() {
        auto current = begin();
        while (current != end()) {
            auto prev = current++;
            bool acquired = prev->is_acquired(std::memory_order_acquire);
            assert(!acquired && "Can't clear while all threads aren't detached");
            (void) acquired;
            prev->do_delete();
        }
    }

private:
    ThreadLocalOwner &get_owner() noexcept {
        static thread_local ThreadLocalOwner owner;
        return owner;
    }

    pointer find_or_create() {
        auto found = this->acquire_free();
        if (found != end()) {
            return found.operator->();
        } else {
            auto new_item = factory_();
            new_item->key_ = this;
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
    lu::fixed_size_function<factory_func, 64> factory_{detail::DefaultFactory<value_type>{}};
};

}// namespace lu

#endif
