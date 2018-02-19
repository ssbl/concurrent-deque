#ifndef DEQUE_HPP
#define DEQUE_HPP

#include <atomic>
#include <experimental/optional>
#include <memory>

namespace deque {

template <typename T>
class Buffer {
private:
  long id_;
  int log_size;
  T *segment;
  Buffer<T> *next;

public:
  Buffer(int ls, long id) {
    id_ = id;
    log_size = ls;
    segment = new T[1 << log_size];
    next = nullptr;
  }

  ~Buffer() {
    delete[] segment;
  }

  long id() const {
    return id_;
  }

  Buffer<T> *next_buffer() {
    return next;
  }

  long size() const {
    return static_cast<long>(1 << log_size);
  }

  T get(long i) const {
    return segment[i % size()];
  }

  void put(long i, T item) {
    segment[i % size()] = item;
  }

  Buffer<T> *resize(long b, long t, int delta) {
    auto buffer = new Buffer<T>(log_size + delta, id_ + 1);
    for (auto i = t; i < b; ++i)
      buffer->put(i, get(i));
    next = buffer;
    return buffer;
  }
};

// A buffer_tls is created for each stealer thread. It is intended to
// be local to that thread; the reclaimer creates one of these
// whenever a stealer thread is created.
struct buffer_tls {
  // The id of the buffer last used by the thread.
  std::atomic<long> id_last_used;
  // If set, we don't check `id_last_used`.
  std::atomic<bool> was_idle;
  // The next buffer_tls in the list.
  buffer_tls *next;
};

// The reclaimer deals with additions to and cleanup of the
// `buffer_tls` list.
class Reclaimer {
private:
  std::atomic<buffer_tls *> id_list;

public:
  Reclaimer() : id_list(nullptr) {
  }

  ~Reclaimer() {
    auto head = id_list.load(std::memory_order_relaxed);
    while (head) {
      auto reclaimed = head;
      head = head->next;
      delete reclaimed;
    }
  }

  buffer_tls *get_id_list() {
    return id_list.load(std::memory_order_relaxed);
  }

  // Each stealer thread registers before using the deque.
  buffer_tls *register_thread() {
    auto tls = new buffer_tls{{0}, {true}, nullptr};
    tls->next = get_id_list();

    while (!id_list.compare_exchange_weak(tls->next, tls)) {
      tls->next = id_list;
    }

    return tls;
  }
};

template <typename T>
class Deque {
private:
  std::atomic<long> top;
  std::atomic<long> bottom;
  Buffer<T> *unlinked;
  static const int log_initial_size = 4;

public:
  Reclaimer reclaimer;
  std::atomic<Buffer<T> *> buffer;

  Deque() : top(0), bottom(0), unlinked(), reclaimer(),
	    buffer(new Buffer<T>(log_initial_size, 0)) {
  }

  ~Deque() {
    auto b = buffer.load(std::memory_order_relaxed);

    while (unlinked && unlinked != b) {
      auto reclaimed = unlinked;
      unlinked = unlinked->next_buffer();
      delete reclaimed;
    }

    delete b;
  }

  void push_bottom(const T object) {
    auto b = bottom.load(std::memory_order_relaxed);
    auto t = top.load(std::memory_order_acquire);
    auto a = buffer.load(std::memory_order_relaxed);

    auto size = b - t;
    if (size >= a->size() - 1) {
      unlinked = unlinked ? unlinked : a;
      a = a->resize(b, t, 1);
      buffer.store(a, std::memory_order_release);
    }

    if (unlinked)
      reclaim_buffers(a);

    a->put(b, object);
    // This fence ensures that an object isn't stolen before we update
    // `bottom`.
    std::atomic_thread_fence(std::memory_order_release);
    bottom.store(b + 1, std::memory_order_relaxed);
  }

  std::experimental::optional<T> pop_bottom() {
    auto b = bottom.load(std::memory_order_relaxed);
    auto a = buffer.load(std::memory_order_acquire);

    bottom.store(b - 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto t = top.load(std::memory_order_relaxed);

    auto size = b - t;
    std::experimental::optional<T> popped = {};

    if (size <= 0) {
      // Deque empty: reverse the decrement to bottom.
      bottom.store(b, std::memory_order_relaxed);
    } else if (size == 1) {
      // Race against steals.
      if (top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                      std::memory_order_relaxed))
        popped = a->get(t);
      bottom.store(b, std::memory_order_relaxed);
    } else {
      popped = a->get(b - 1);

      if (size <= a->size() / 3 && size > 1 << log_initial_size) {
        unlinked = unlinked ? unlinked : a;
        a = a->resize(b, t, -1);
        buffer.store(a, std::memory_order_release);
      }

      if (unlinked)
        reclaim_buffers(a);
    }

    return popped;
  }

  std::experimental::optional<T> steal() {
    auto t = top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto b = bottom.load(std::memory_order_acquire);

    int size = b - t;
    std::experimental::optional<T> stolen = {};

    if (size > 0) {
      auto a = buffer.load(std::memory_order_consume);
      // Race against other steals and a pop.
      if (top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                      std::memory_order_relaxed))
        stolen = a->get(t);
    }

    return stolen;
  }

  // An experimental mechanism to reclaim unlinked buffers. Each
  // stealer thread keeps track of the id of the buffer it last read
  // from. We reclaim all buffers with id strictly less than the
  // minimum of the ids seen by the stealers.
  //
  // `new_buffer` points to the current buffer.
  //
  // XXX: Ideally we shouldn't need the pointer to the new buffer.
  void reclaim_buffers(Buffer<T> *new_buffer) {
    auto min_id = new_buffer->id();
    auto head = reclaimer.get_id_list();

    while (head) {
      auto idle = head->was_idle.load(std::memory_order_acquire);
      if (!idle) {
        auto last_used_id = head->id_last_used.load(std::memory_order_relaxed);
        min_id = std::min(min_id, last_used_id);
      }
      head = head->next;
    }

    while (unlinked->id() < min_id) {
      auto reclaimed = unlinked;
      unlinked = unlinked->next_buffer();
      delete reclaimed;
    }
  }
};

template <typename T>
class Worker {
private:
  std::shared_ptr<Deque<T>> deque;

public:
  explicit Worker(std::shared_ptr<Deque<T>> d) : deque(d) {
  }

  // Copy constructor.
  // There can only be one worker end.
  Worker(const Worker<T> &w) = delete;

  // Move constructor.
  Worker(Worker<T> &&w) : deque(w.deque) {
    w.deque = nullptr;
  }

  ~Worker() {
  }

  void push(const T item) {
    deque->push_bottom(item);
  }

  std::experimental::optional<T> pop() {
    return deque->pop_bottom();
  }
};

template <typename T>
class Stealer {
private:
  std::shared_ptr<Deque<T>> deque;
  buffer_tls *buffer_data;

public:
  explicit Stealer(std::shared_ptr<Deque<T>> d)
    : deque(d)
    , buffer_data(deque->reclaimer.register_thread()) {
  }

  // Copy constructor.
  //
  // Used whenever a new stealer thread is created.
  Stealer(const Stealer<T> &s)
    : deque(s.deque)
    , buffer_data(deque->reclaimer.register_thread()) {
  }

  // Move constructor.
  //
  // Used when we're passing the stealer end around in the same
  // thread.
  Stealer(Stealer<T> &&s)
    : deque(std::move(s.deque))
    , buffer_data(s.buffer_data) {
  }

  ~Stealer() {
  }

  std::experimental::optional<T> steal() {
    // We use memory_order_release to synchronize with the read by the
    // reclaimer. It makes sense, but I'm not absolutely sure about
    // this.
    buffer_data->was_idle.store(false, std::memory_order_release);
    auto stolen = deque->steal();
    buffer_data->was_idle.store(true, std::memory_order_release);

    // Stealers load the buffer pointer using memory_order_consume.
    auto b = deque->buffer.load(std::memory_order_consume);
    buffer_data->id_last_used.store(b->id(), std::memory_order_relaxed);

    return stolen;
  }
};

// Create a worker and stealer end for a single deque. The stealer end
// can be cloned when spawning stealer threads.
//
// auto ws = deque::deque<T>();
// auto worker = std::move(ws.first);
// auto stealer = std::move(ws.second);
//
// std::thread foo([&stealer]() {
//   auto clone = stealer;
//   auto work = clone.steal();
//   /* ... */
// });
//
// foo.join();
//
// XXX: Would it be better to create a macro for this?
template <typename T>
std::pair<Worker<T>, Stealer<T>> deque() {
  auto d = std::make_shared<Deque<T>>();
  return {Worker<T>(d), Stealer<T>(d)};
}

} // namespace deque

#endif // DEQUE_HPP
