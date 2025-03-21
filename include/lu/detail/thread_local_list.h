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

struct DefaultAttacher {
    template<class ValueType>
    void operator()(ValueType *value) const {}
};

struct DefaultDetacher {
    template<class ValueType>
    void operator()(ValueType *value) const {}
};

struct DefaultCreator {
    template<class ValueType>
    ValueType *operator()() const {
        return new ValueType();
    }
};

struct DefaultDeleter {
    template<class ValueType>
    void operator()(ValueType *value) const {
        delete value;
    }
};

}// namespace detail

template<class ValueType>
class thread_local_list_base_hook : public lu::unordered_set_base_hook<lu::is_auto_unlink<false>>,
                                    public lu::active_list_base_hook<> {
    template<class>
    friend class thread_local_list;

public:
    using detacher_func = void(ValueType *);
    using attacher_func = void(ValueType *);
    using deleter_func = void(ValueType *);

protected:
    thread_local_list_base_hook() = default;

public:
    template<class Attacher>
    void set_attacher(Attacher attacher) noexcept {
        attacher_ = std::move(attacher);
    }

    template<class Detacher>
    void set_detacher(Detacher detacher) noexcept {
        detacher_ = std::move(detacher);
    }

    template<class Deleter>
    void set_deleter(Deleter deleter) noexcept {
        deleter_ = std::move(deleter);
    }

private:
    void do_attach() noexcept {
        attacher_(static_cast<ValueType *>(this));
    }

    void do_detach() noexcept {
        detacher_(static_cast<ValueType *>(this));
    }

    void do_delete() noexcept {
        deleter_(static_cast<ValueType *>(this));
    }

private:
    void *key_{};
    lu::fixed_size_function<attacher_func, 64> attacher_{detail::DefaultAttacher()};
    lu::fixed_size_function<detacher_func, 64> detacher_{detail::DefaultDetacher()};
    lu::fixed_size_function<deleter_func, 64> deleter_{detail::DefaultDeleter()};
};

template<class ValueType>
class thread_local_list : private lu::active_list<ValueType> {
    using Base = lu::active_list<ValueType>;
    using Hook = lu::thread_local_list_base_hook<ValueType>;

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
        struct KeyOfValue {
            using type = const thread_local_list *;

            type operator()(const Hook &value) const {
                return reinterpret_cast<type>(value.key_);
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

        pointer get_entry(const thread_local_list *list) noexcept {
            auto found = set_.find(list);
            if (found == set_.end()) {
                return {};
            }
            return found.operator->();
        }

        void attach(reference value) noexcept {
            set_.insert(value);
            value.do_attach();
        }

        void detach(const thread_local_list *list) noexcept {
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
    template<class Creator = detail::DefaultCreator>
    explicit thread_local_list(Creator creator = {})
        : creator_(std::move(creator)) {}

    thread_local_list(const thread_local_list &) = delete;

    thread_local_list(thread_local_list &&) = delete;

    ~thread_local_list() {
        auto current = begin();
        while (current != end()) {
            auto prev = current++;
            bool acquired = prev->is_acquired(std::memory_order_acquire);
            UNUSED(acquired);
            assert(!acquired && "Can't clear while all threads aren't detached");
            prev->do_delete();
        }
    }

private:
    ThreadLocalOwner &get_owner() {
        static thread_local ThreadLocalOwner owner;
        return owner;
    }

    pointer find_or_create() {
        auto found = this->acquire_free();
        if (found != end()) {
            return found.operator->();
        } else {
            auto new_item = creator_();
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
    lu::fixed_size_function<pointer(), 64> creator_;
};

}// namespace lu

#endif
