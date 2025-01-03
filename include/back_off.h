#ifndef __BACK_OF_H__
#define __BACK_OF_H__

#include <thread>


namespace lu {

struct none_backoff {
    void operator()() const {}
};

struct yield_backoff {
    void operator()() const {
        std::this_thread::yield();
    }
};

}// namespace lu

#endif