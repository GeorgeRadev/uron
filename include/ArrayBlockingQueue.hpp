#pragma once

#include <condition_variable>
#include <mutex>

namespace util {

template <typename E> class ArrayBlockingQueue {

  private:
    std::mutex mutex;
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    E **queueItems;
    unsigned int queueSize;
    unsigned int queueIndex;
    unsigned int dequeueIndex;
    unsigned int count;

    void put(E *e) {
        queueItems[queueIndex] = e;
        if (++queueIndex >= queueSize) {
            queueIndex = 0;
        }
        count++;
    }

    E *get() {
        E *e = queueItems[dequeueIndex];
        queueItems[dequeueIndex] = nullptr;
        if (++dequeueIndex >= queueSize) {
            dequeueIndex = 0;
        }
        count--;
        return e;
    }

  public:
    // create queue with given size
    ArrayBlockingQueue(unsigned int size) {
        queueItems = new E *[size];
        queueSize = size;
        queueIndex = 0;
        dequeueIndex = 0;
        count = 0;
    }

    // create queue with given size
    ~ArrayBlockingQueue() { delete queueItems; }

    // add element to the queue; blocks if queue is full
    void enqueue(E *e) {
        std::unique_lock<std::mutex> lock(mutex);
        while (count >= queueSize) {
            notFull.wait(lock);
        }
        put(e);
        notEmpty.notify_one();
    }

    // take element from the queue; blocks if queue is empty
    E *dequeue() {
        std::unique_lock<std::mutex> lock(mutex);
        while (count == 0) {
            notEmpty.wait(lock);
        }
        E *e = get();
        notFull.notify_one();
        return e;
    }

    // add element to the queue; blocks if queue is full
    bool enqueue_for(E *e, const std::chrono::milliseconds &__rtime) {
        std::unique_lock<std::mutex> lock(mutex);
        if (count >= queueSize) {
            notFull.wait_for(lock, __rtime);
        }
        if (count < queueSize) {
            put(e);
            notEmpty.notify_one();
            return true;
        } else {
            return false;
        }
    }

    // take element from the queue; blocks if queue is empty
    E *dequeue_for(const std::chrono::milliseconds &__rtime) {
        std::unique_lock<std::mutex> lock(mutex);
        if (count == 0) {
            auto status = notEmpty.wait_for(lock, __rtime);
        }
        if (count > 0) {
            E *e = get();
            notFull.notify_one();
            return e;
        } else {
            return nullptr;
        }
    }

    // take element from the queue; blocks if queue is empty
    E *dequeue_nowait() {
        std::unique_lock<std::mutex> lock(mutex);
        if (count > 0) {
            E *e = get();
            notFull.notify_one();
            return e;
        } else {
            return nullptr;
        }
    }

    // no assignments allowed
    ArrayBlockingQueue &operator=(const ArrayBlockingQueue &) = delete;
    ArrayBlockingQueue &operator=(ArrayBlockingQueue &&) = delete;
};

} // namespace util