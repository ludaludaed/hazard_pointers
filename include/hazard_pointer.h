#ifndef __HAZARD_POINTERS_H__
#define __HAZARD_POINTERS_H__

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include "intrusive/forward_list.h"
#include "intrusive/options.h"
#include "intrusive/unordered_set.h"

namespace lu {
    class HazardPointerDomain;

    class hazard_tag {};

    using hazard_hook = lu::unordered_set_base_hook<lu::tag<hazard_tag>>;

    class HazardPointerObjBaseBase : public hazard_hook {
        friend class lu::HazardPointerDomain;

    public:
        virtual ~HazardPointerObjBaseBase() {}

    private:
        virtual void destroy() noexcept = 0;

    private:
        bool protected_{false};
    };

    template<class HazardObj, size_t NumOfBuckets>
    class RetiredSet {
    private:
        struct KeyOfValue {
            using type = const HazardObj *;

            const HazardObj *operator()(const HazardObj &node) const {
                return &node;
            }
        };

        using retired_set = lu::unordered_set<HazardObj, lu::base_hook<hazard_hook>, lu::key_of_value<KeyOfValue>>;

    public:
        using retired_element = HazardObj;

        using iterator = typename retired_set::iterator;
        using const_iterator = typename retired_set::const_iterator;

        using pointer = retired_element *;
        using const_pointer = const retired_element *;

        using reference = retired_element &;
        using const_reference = const retired_element &;

        using value_type = retired_element;
        using key_type = const_pointer;

    public:
        RetiredSet() noexcept
            : retired_set_(typename retired_set::bucket_traits(buckets_.data(), buckets_.size())) {}

        RetiredSet(const RetiredSet &) = delete;

        RetiredSet(RetiredSet &&other) = delete;

        RetiredSet &operator=(const RetiredSet &) = delete;

        RetiredSet &operator=(RetiredSet &&other) = delete;

    public:
        void push(reference value) noexcept {
            retired_set_.insert(value);
        }

        void erase(key_type key) noexcept {
            retired_set_.erase(key);
        }

        void erase(const_iterator position) noexcept {
            retired_set_.erase(position);
        }

        bool contains(key_type key) const noexcept {
            return retired_set_.find(key) != retired_set_.end();
        }

        iterator find(key_type key) noexcept {
            return retired_set_.find(key);
        }

        const_iterator find(key_type key) const noexcept {
            return retired_set_.find(key);
        }

        void merge(RetiredSet &other) noexcept {
            retired_set_.merge(other.retired_set_);
        }

        bool empty() const noexcept {
            return retired_set_.empty();
        }

        iterator begin() noexcept {
            return retired_set_.begin();
        }

        iterator end() noexcept {
            return retired_set_.end();
        }

        const_iterator begin() const noexcept {
            return retired_set_.begin();
        }

        const_iterator end() const noexcept {
            return retired_set_.end();
        }

    private:
        std::array<typename retired_set::bucket_type, NumOfBuckets> buckets_{};
        retired_set retired_set_;
    };

    template<class HazardObj>
    class ProtectionHolder : public lu::forward_list_hook<> {
    public:
        inline void protect(const HazardObj *new_ptr) {
            hazard_ptr_.store(new_ptr);
        }

        inline void reset() {
            hazard_ptr_.store(nullptr);
        }

        inline const HazardObj *get_protected() const {
            return hazard_ptr_.load();
        }

        inline bool empty() const {
            return hazard_ptr_.load() == nullptr;
        }

    private:
        std::atomic<const HazardObj *> hazard_ptr_{nullptr};
    };

    template<class HazardObj, size_t Size>
    class ProtectedList {
        using ProtectionHolder = ProtectionHolder<HazardObj>;

        using FreeList = lu::forward_list<ProtectionHolder>;
        using Array = std::array<ProtectionHolder, Size>;

    public:
        using value_type = ProtectionHolder;

        using reference = typename Array::reference;
        using const_reference = typename Array::const_reference;
        using pointer = typename Array::pointer;
        using const_pointer = typename Array::const_pointer;

        using iterator = typename Array::iterator;
        using const_iterator = typename Array::const_iterator;

    public:
        ProtectedList() noexcept
            : array_(), free_list_(array_.begin(), array_.end()) {}

        ProtectedList(const ProtectedList &) = delete;

        ProtectedList(ProtectedList &&other) = delete;

        ProtectedList &operator=(const ProtectedList &) = delete;

        ProtectedList &operator=(ProtectedList &&other) = delete;

    public:
        reference acquire() noexcept {
            assert(!full());
            reference hazard_pointer = free_list_.front();
            free_list_.pop_front();
            return hazard_pointer;
        }

        void release(reference hazard_pointer) noexcept {
            hazard_pointer.reset();
            free_list_.push_front(hazard_pointer);
        }

        bool full() const noexcept {
            return free_list_.empty();
        }

        iterator begin() noexcept {
            return array_.begin();
        }

        iterator end() noexcept {
            return array_.end();
        }

        const_iterator begin() const noexcept {
            return array_.begin();
        }

        const_iterator end() const noexcept {
            return array_.end();
        }

    private:
        std::array<value_type, Size> array_;
        FreeList free_list_;
    };

    class HazardPointerDomain {
        friend class HazardPointer;

        using HazardObj = HazardPointerObjBaseBase;
        using ProtectionHolder = ProtectionHolder<HazardObj>;

    private:
        class HazardThreadData {
            friend class HazardPointerDomain;
            friend class HazardThreadDataOwner;

            using ProtectedList = ProtectedList<HazardObj, 8>;
            using RetiredList = RetiredSet<HazardObj, 64>;

        public:
            explicit HazardThreadData(HazardPointerDomain &domain) noexcept
                : domain_(domain) {}

            ~HazardThreadData() {
                auto current = retired_set_.begin();
                while (current != retired_set_.end()) {
                    auto next = std::next(current);
                    destroy_retired(*current);
                    current = next;
                }
            }

        private:
            void destroy_retired(HazardObj &to_erase) {
                retired_set_.erase(&to_erase);
                to_erase.destroy();
                --num_of_retires_;
            }

            void merge(HazardThreadData &other) noexcept {
                num_of_retires_ += other.num_of_retires_;
                other.num_of_retires_ = 0;
                retired_set_.merge(other.retired_set_);
            }

            bool acquired() noexcept {
                return in_use_.load();
            }

            bool try_acquire() noexcept {
                return !in_use_.exchange(true);
            }

            void release() noexcept {
                in_use_.store(false);
            }

        public:
            ProtectionHolder *acquire_protection() noexcept {
                assert(!protections_list_.full());
                return &protections_list_.acquire();
            }

            void release_protection(ProtectionHolder *protection) noexcept {
                protections_list_.release(*protection);
            }

            void retire(HazardObj *retired_ptr) noexcept {
                retired_set_.push(*retired_ptr);
                if (++num_of_retires_ >= scan_threshold) [[unlikely]] {
                    scan();
                }
            }

            void scan() noexcept {
                auto current_thread_data = domain_.head_.load();
                while (current_thread_data != nullptr) {
                    if (current_thread_data->acquired()) {
                        auto &protections = current_thread_data->protections_list_;
                        for (auto it = protections.begin(); it != protections.end(); ++it) {
                            auto protection = it->get_protected();
                            auto found = retired_set_.find(protection);
                            if (found != retired_set_.end()) {
                                found->protected_ = true;
                            }
                        }
                    }
                    current_thread_data = current_thread_data->next_;
                }

                auto current = retired_set_.begin();
                while (current != retired_set_.end()) {
                    if (current->protected_) {
                        current->protected_ = false;
                        ++current;
                    } else {
                        auto next = std::next(current);
                        destroy_retired(*current);
                        current = next;
                    }
                }
            }

            void help_scan() noexcept {
                auto current_thread_data = domain_.head_.load();
                while (current_thread_data != nullptr) {
                    if (current_thread_data->try_acquire()) {
                        current_thread_data->scan();
                        merge(*current_thread_data);
                        current_thread_data->release();
                    }
                    current_thread_data = current_thread_data->next_;
                }
                scan();
            }

        private:
            constexpr static std::size_t scan_threshold = 64;
            std::size_t num_of_retires_{0};

            HazardPointerDomain &domain_;

            ProtectedList protections_list_{};
            RetiredList retired_set_{};

            std::atomic<bool> in_use_{true};
            HazardThreadData *next_{nullptr};
        };

        struct HazardThreadDataOwner {
        public:
            explicit HazardThreadDataOwner(HazardThreadData *thread_data)
                : thread_data(thread_data) {}

            ~HazardThreadDataOwner() {
                if (thread_data != nullptr) {
                    thread_data->help_scan();
                    thread_data->release();
                }
            }

        public:
            HazardThreadData *thread_data;
        };

    public:
        HazardPointerDomain() = default;

        HazardPointerDomain(const HazardPointerDomain &other) = delete;

        HazardPointerDomain(HazardPointerDomain &&other) = delete;

        HazardPointerDomain &operator=(const HazardPointerDomain &other) = delete;

        HazardPointerDomain &operator=(HazardPointerDomain &&other) = delete;

        ~HazardPointerDomain() {
            HazardThreadData *current = head_.load();
            while (current != nullptr) {
                HazardThreadData *next = current->next_;
                delete current;
                current = next;
            }
        }

    public:
        HazardThreadData &get_thread_data() noexcept {
            static thread_local HazardThreadDataOwner owner(nullptr);
            if (owner.thread_data == nullptr) {
                HazardThreadData *current = head_.load();
                while (current != nullptr) {
                    if (current->try_acquire()) {
                        owner.thread_data = current;
                        return *owner.thread_data;
                    }
                    current = current->next_;
                }
                HazardThreadData *new_thread_data = new HazardThreadData(*this);
                HazardThreadData *head = head_.load();
                do {
                    new_thread_data->next_ = head;
                } while (!head_.compare_exchange_strong(head, new_thread_data));
                owner.thread_data = new_thread_data;
            }
            return *owner.thread_data;
        }

    private:
        std::atomic<HazardThreadData *> head_{nullptr};
    };

    inline HazardPointerDomain &get_default_domain() {
        static HazardPointerDomain domain;
        return domain;
    }

    class HazardPointer {
        using ProtectionHolder = HazardPointerDomain::ProtectionHolder;

    public:
        HazardPointer() noexcept
            : domain_(),
              protection_() {}

        explicit HazardPointer(HazardPointerDomain *domain) noexcept
            : domain_(domain),
              protection_(domain_->get_thread_data().acquire_protection()) {}

        HazardPointer(const HazardPointer &) = delete;

        HazardPointer(HazardPointer &&other) noexcept
            : domain_(other.domain_), protection_(other.protection_) {
            other.protection_ = nullptr;
        }

        HazardPointer &operator=(const HazardPointer &) = delete;

        HazardPointer &operator=(HazardPointer &&other) noexcept {
            HazardPointer dummy(std::move(other));
            swap(dummy);
            return *this;
        }

        ~HazardPointer() {
            if (protection_) {
                auto &thread_data = domain_->get_thread_data();
                thread_data.release_protection(protection_);
            }
        }

    public:
        bool empty() const noexcept {
            return !protection_ || protection_->empty();
        }

        template<class Ptr>
        Ptr protect(const std::atomic<Ptr> &src) noexcept {
            return protect(src, [](auto &&p) { return std::forward<decltype(p)>(p); });
        }

        template<class Ptr, class Func>
        Ptr protect(const std::atomic<Ptr> &src, Func &&func) noexcept {
            auto ptr = src.load();
            while (!try_protect(ptr, src, std::forward<Func>(func))) {}
            return ptr;
        }

        template<class Ptr>
        bool try_protect(Ptr &ptr, const std::atomic<Ptr> &src) noexcept {
            return try_protect(ptr, src, [](auto &&p) { return std::forward<decltype(p)>(p); });
        }

        template<class Ptr, class Func>
        bool try_protect(Ptr &ptr, const std::atomic<Ptr> &src, Func &&func) noexcept {
            assert(protection_ && "hazard_ptr must be initialized");
            auto old = ptr;
            reset_protection(func(old));
            ptr = src.load();
            if (old != ptr) {
                reset_protection();
                return false;
            }
            return true;
        }

        template<class Ptr>
        void reset_protection(const Ptr ptr) noexcept {
            assert(protection_ && "hazard_ptr must be initialized");
            protection_->protect(ptr);
        }

        void reset_protection(nullptr_t = nullptr) noexcept {
            assert(protection_ && "hazard_ptr must be initialized");
            protection_->reset();
        }

        void swap(HazardPointer &other) noexcept {
            std::swap(domain_, other.domain_);
            std::swap(protection_, other.protection_);
        }

    public:
        friend void swap(HazardPointer &left, HazardPointer &right) noexcept {
            left.swap(right);
        }

    private:
        HazardPointerDomain *domain_;
        ProtectionHolder *protection_{nullptr};
    };

    template<class ValueType, class Deleter>
    class HazardPointerObjBase : public HazardPointerObjBaseBase {
    protected:
        HazardPointerObjBase() noexcept = default;

        HazardPointerObjBase(const HazardPointerObjBase &) noexcept = default;

        HazardPointerObjBase(HazardPointerObjBase &&) noexcept = default;

        HazardPointerObjBase &operator=(const HazardPointerObjBase &) noexcept = default;

        HazardPointerObjBase &operator=(HazardPointerObjBase &&) noexcept = default;

    public:
        void retire(Deleter deleter = Deleter(), HazardPointerDomain &domain = get_default_domain()) noexcept {
            deleter_ = std::move(deleter);
            auto &thread_data = domain.get_thread_data();
            thread_data.retire(static_cast<HazardPointerObjBaseBase *>(this));
        }

    private:
        void destroy() noexcept override {
            deleter_(static_cast<ValueType *>(this));
        }

    private:
        Deleter deleter_{};
    };

    inline HazardPointer make_hazard_pointer(HazardPointerDomain &domain = get_default_domain()) {
        return HazardPointer(&domain);
    }

    template<class ValueType, class Deleter = std::default_delete<ValueType>>
    using hazard_pointer_obj_base = HazardPointerObjBase<ValueType, Deleter>;

    using hazard_pointer_domain = HazardPointerDomain;

    using hazard_pointer = HazardPointer;

    template<class ValueType>
    class GuardedPointer {
    public:
        using value_type = ValueType;

        using pointer = ValueType *;
        using reference = ValueType &;
        using const_reference = const ValueType &;

    public:
        GuardedPointer() = default;

        GuardedPointer(hazard_pointer guard, pointer ptr)
            : guard_(std::move(guard)), ptr_(ptr) {}

        GuardedPointer(const GuardedPointer &) = delete;

        GuardedPointer &operator=(const GuardedPointer &) = delete;

        pointer operator->() const {
            return ptr_;
        }

        reference operator*() {
            return *ptr_;
        }

        const_reference operator*() const {
            return *ptr_;
        }

        explicit operator bool() const {
            return ptr_;
        }

    private:
        hazard_pointer guard_{};
        pointer ptr_{};
    };

    template<class ValueType>
    using guarded_ptr = GuardedPointer<ValueType>;
}// namespace lu

#endif