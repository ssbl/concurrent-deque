A lock-free work-stealing deque
---

This is a C++ implementation of the concurrent work stealing queue
presented in the paper "Dynamic Circular Work-Stealing Deque". An
experimental memory reclamation mechanism is also provided by keeping
a chain of "unlinked" buffers.

### Example

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
  auto clone = stealer;
  clone.steal();
});

stealer.steal();
stealer_thread.join();
```

### References

This implementation is heavily based on the improved version presented
in the paper "Correct and Efficient Work-Stealing for Weak Memory
Models". The Rust `crossbeam` crate also provided a ton of
inspiration.

- The Rust `deque` crate, which has been moved to [crossbeam](https://github.com/crossbeam-rs/crossbeam/blob/master/src/sync/chase_lev.rs).
- [Correct and Efficient Work-stealing for Weak Memory Models](http://www.di.ens.fr/~zappa/readings/ppopp13.pdf).
- Chase-Lev [paper](http://neteril.org/~jeremie/Dynamic_Circular_Work_Queue.pdf).
- Jeff Preshing's [blog](https://preshing.com).
- Erez Petrank's lectures on memory reclamation: (Parts [1](https://www.youtube.com/watch?v=aedEe0Zx_g0), [2](https://youtu.be/BCXrG1M65HU), [3](https://www.youtube.com/watch?v=u9pbPpZXu18)).
