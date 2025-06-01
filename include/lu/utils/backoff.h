#ifndef __BACK_OF_H__
#define __BACK_OF_H__

#include <thread>

namespace lu {

struct none_backoff {
    void operator()() const noexcept {}
};

struct yield_backoff {
    void operator()() const noexcept {
        std::this_thread::yield();
    }
};

template <class Backoff>
struct backoff {
    template <class Base>
    struct pack : public Base {
        using backoff = Backoff;
    };
};

}// namespace lu

#endif
