#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include "activae_list.h"
#include "fixed_size_function.h"
#include "intrusive/options.h"
#include "intrusive/unordered_set.h"
#include <atomic>
#include <cstdint>
#include <type_traits>
namespace lu {

    template<class ValueType, class Tag = DefaultHookTag>
    class ThreadLocalListHook : public lu::unordered_set_base_hook<>,
                                public lu::active_list_base_hook<> {

        template<class, class>
        friend class ThreadLocalList;

    private:
        void IncRef(std::size_t refs = 1) {
            ref_count_.fetch_add(refs, std::memory_order_relaxed);
        }

        void DecRef(std::size_t refs = 1) {
            if (ref_count_.fetch_sub(refs, std::memory_order_release) == refs) {
                std::atomic_thread_fence(std::memory_order_acquire);
                deleter_(static_cast<ValueType *>(this));
            }
        }

    private:
        std::uintptr_t key_{};
        std::atomic<std::size_t> ref_count_{1};
        lu::fixed_size_function<void(ValueType *), 64> deleter_{};
    };

    template<class ValueType, class Tag = DefaultHookTag>
    class ThreadLocalList {
        using Hook = ThreadLocalListHook<ValueType, Tag>;

        struct KeyOfValue {
            using type = std::uintptr_t;

            std::uintptr_t operator()(const Hook &value) {
                return value.key_;
            }
        };

        using UnorderedSet = lu::unordered_set<ValueType, lu::key_of_value<KeyOfValue>>;
        using ActiveList = lu::active_list<ValueType>;

        static_assert(std::is_base_of_v<Hook, ValueType>, "ValueType must be inherited from ThreadLocalListHook");

    public:
        using value_type = ValueType;

        using pointer = typename ActiveList::pointer;
        using const_pointer = typename ActiveList::const_pointer;
        using reference = typename ActiveList::reference;
        using const_reference = typename ActiveList::const_reference;

        using iterator = typename ActiveList::iterator;
        using const_iterator = typename ActiveList::const_iterator;

    private:
        struct ThreadLocalOwner {
            using BucketTraits = typename UnorderedSet::bucket_traits;
            using BucketType = typename UnorderedSet::bucket_type;

            using Buckets = std::array<BucketType, 8>;

            ThreadLocalOwner()
                : set(BucketTraits(buckets.data(), buckets.size())) {}

            ~ThreadLocalOwner() {
                auto current = set.begin();
                while (current != set.end()) {
                    auto prev = current++;
                    set.erase(prev);
                    prev->release();
                    prev->DecRef();
                }
            }

            void insert(reference value) {
                value.IncRef();
                set.insert(value);
            }

            pointer get(std::uintptr_t key) {
                auto found = set.find(key);
                if (found == set.end()) {
                    return {};
                }
                return found.operator->();
            }

            void erase(std::uintptr_t key) {
                auto found = set.find(key);
                if (found != set.end()) {
                    set.erase(found);
                    found->DectRef();
                }
            }

        private:
            UnorderedSet set;
            Buckets buckets;
        };

    private:
        ThreadLocalOwner& GetOwner() {
            thread_local ThreadLocalOwner owner;
            return owner;
        }    

    public:
        bool try_acquire(reference item);

        bool is_acquired(reference item, std::memory_order order = std::memory_order_relaxed) const;

        void release(reference item);

        void attach_thread();

        void detach_thread();

        reference get_thread_local();

        iterator begin();

        iterator end();

        const_iterator cbegin() const;

        const_iterator cend() const;

        const_iterator begin() const;

        const_iterator end() const;

    private:
        ActiveList list_;
    };
}// namespace lu

#endif