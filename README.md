A lock-free work-stealing deque
---

This is a C++ implementation of the Chase-Lev deque, a concurrent
single-producer, multi-consumer queue presented in the paper "Dynamic
Circular Work-Stealing Deque". Additionally, this deque handles
reclamation of unused storage at the cost of some coordination between
the stealers and the worker.

This implementation is heavily based on the improved version presented
in the paper "Correct and Efficient Work-Stealing for Weak Memory
Models". The Rust `crossbeam` crate also provided a ton of
inspiration.

### Usage

With `deque.hpp` in your include path:

```c++
#include "deque.hpp"

auto ws = deque::deque<int>();
auto worker = std::move(ws.first);
auto stealer = std::move(ws.second);

// The worker can push and pop from one end of the queue.
worker.push(1);
worker.pop();

// Stealers can pop from the other end of the queue.
worker.push(1);
std::thread stealer_thread([&stealer]() {
  // Each stealer thread creates a copy.
  auto stealer_copy = stealer;
  stealer_copy.steal();
});

stealer.steal();
stealer_thread.join();
```
