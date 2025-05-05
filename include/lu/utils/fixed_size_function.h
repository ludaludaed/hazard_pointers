#ifndef __FIXED_SIZE_FUNCTION_H__
#define __FIXED_SIZE_FUNCTION_H__

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>


namespace lu {

template <class, std::size_t>
class fixed_size_function;

template <class ResultType, class... Args, std::size_t BufferLen>
class fixed_size_function<ResultType(Args...), BufferLen> {
    using storage = unsigned char[BufferLen];

    struct vtable {
        using call_func_ptr = ResultType (*)(void *, Args &&...);
        using destruct_func_ptr = void (*)(void *);
        using copy_func_ptr = void (*)(void *, const void *);
        using move_func_ptr = void (*)(void *, void *);

        call_func_ptr call{};
        destruct_func_ptr destruct{};
        copy_func_ptr copy{};
        move_func_ptr move{};
    };

public:
    using result_type = ResultType;

public:
    fixed_size_function() noexcept = default;

    fixed_size_function(std::nullptr_t) noexcept {}

    template <class Functor, class = std::enable_if_t<sizeof(Functor) <= BufferLen>>
    fixed_size_function(Functor &&func) {
        construct(std::forward<Functor>(func));
    }

    fixed_size_function(const fixed_size_function &other) { copy(other); }

    fixed_size_function(fixed_size_function &&other) { move(std::move(other)); }

    ~fixed_size_function() { destruct(); }

    fixed_size_function &operator=(const fixed_size_function &other) {
        destruct();
        copy(other);
        return *this;
    }

    fixed_size_function &operator=(fixed_size_function &&other) {
        destruct();
        move(std::move(other));
        return *this;
    }

    fixed_size_function &operator=(std::nullptr_t) {
        destruct();
        return *this;
    }

    template <class Functor, class = std::enable_if_t<sizeof(Functor) <= BufferLen>>
    fixed_size_function &operator=(Functor &&func) {
        destruct();
        construct(std::forward<Functor>(func));
        return *this;
    }

    void swap(fixed_size_function &other) {
        fixed_size_function temp(other);
        other = std::move(*this);
        *this = std::move(temp);
    }

    friend void swap(fixed_size_function &left, fixed_size_function &right) { left.swap(right); }

    explicit operator bool() const noexcept { return table_.call; }

    ResultType operator()(Args &&...args) const {
        if (table_.call) {
            return table_.call(const_cast<storage *>(&data_), std::forward<Args>(args)...);
        }
        throw std::bad_function_call();
    }

    friend bool operator==(const fixed_size_function &left, std::nullptr_t right) noexcept {
        return !left;
    }

    friend bool operator==(std::nullptr_t left, const fixed_size_function &right) noexcept {
        return !right;
    }

    friend bool operator!=(const fixed_size_function &left, std::nullptr_t right) noexcept {
        return !(left == right);
    }

    friend bool operator!=(std::nullptr_t left, const fixed_size_function &right) noexcept {
        return !(left == right);
    }

private:
    template <class Functor, class = std::enable_if_t<sizeof(Functor) <= BufferLen>>
    void construct(Functor &&func) {
        using functor_type = typename std::decay_t<Functor>;
        new (&data_) functor_type(std::forward<Functor>(func));

        table_.call = call<functor_type>;
        table_.destruct = destruct<functor_type>;
        if constexpr (std::is_copy_constructible_v<functor_type>) {
            table_.copy = copy_construct<functor_type>;
        }
        if constexpr (std::is_move_constructible_v<functor_type>) {
            table_.move = move_construct<functor_type>;
        }
    }

    void copy(const fixed_size_function &other) {
        if (other.table_.copy) {
            other.table_.copy(&data_, &other);
            table_ = other.table_;
        }
    }

    void move(fixed_size_function &&other) {
        if (other.table_.move) {
            other.table_.move(&data_, &other);
            table_ = other.table_;
            other.destruct();
        } else if (other.table_.copy) {
            copy(other);
        }
    }

    void destruct() {
        if (table_.destruct) {
            table_.destruct(&data_);
            table_ = vtable();
        }
    }

    template <class Functor>
    static void copy_construct(void *this_ptr, const void *other) {
        new (this_ptr) Functor(*reinterpret_cast<const Functor *>(other));
    }

    template <class Functor>
    static void move_construct(void *this_ptr, void *other) {
        new (this_ptr) Functor(std::move(*reinterpret_cast<Functor *>(other)));
    }

    template <class Functor>
    static void destruct(void *this_ptr) {
        reinterpret_cast<Functor *>(this_ptr)->~Functor();
    }

    template <class Functor>
    static ResultType call(void *this_ptr, Args &&...args) {
        return reinterpret_cast<Functor *>(this_ptr)->operator()(std::forward<Args>(args)...);
    }

private:
    vtable table_;
    storage data_;
};

}// namespace lu

#endif
