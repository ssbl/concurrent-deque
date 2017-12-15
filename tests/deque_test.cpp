#define CATCH_CONFIG_MAIN

#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

#include "catch.hpp"
#include "deque.hpp"

TEST_CASE("basic operations", "[deque]") {
  auto ws = deque::deque<int>();
  auto worker = std::move(ws.first);
  auto stealer = std::move(ws.second);

  // Empty deque.
  REQUIRE(!worker.pop());

  // Single push, pop.
  worker.push(100);
  REQUIRE(*worker.pop() == 100);

  // Steal when empty.
  REQUIRE(!stealer.steal());

  // Single push, steal.
  worker.push(100);
  REQUIRE(*stealer.steal() == 100);
}

TEST_CASE("multiple steals on deque of length 1", "[deque]") {
  auto ws = deque::deque<int>();
  auto worker = std::move(ws.first);
  auto stealer = std::move(ws.second);

  worker.push(100);
  auto nthreads = 4;
  std::atomic<int> seen(0);
  std::vector<std::thread> stealers;

  for (int i = 0; i < nthreads; ++i) {
    stealers.emplace_back([&stealer, &seen]() {
      auto clone = stealer;
      auto x = clone.steal();
      if (x)
        seen.fetch_add(1);
    });
  }

  for (auto &s : stealers)
    s.join();

  REQUIRE(seen == 1);
}

TEST_CASE("push against steals", "[deque]") {
  auto ws = deque::deque<int>();
  auto worker = std::move(ws.first);
  auto stealer = std::move(ws.second);

  auto max = 100000;
  auto nthreads = 4;
  std::vector<std::thread> threads;
  std::atomic<int> remaining(max);

  for (auto i = 0; i < nthreads; ++i) {
    threads.emplace_back([&stealer, &remaining]() {
      auto clone = stealer;
      while (remaining.load(std::memory_order_seq_cst) > 0) {
        auto x = clone.steal();
        if (x) {
          // Can't use REQUIRE here because it isn't thread-safe.
          assert(*x == 1);
          remaining.fetch_sub(1);
        }
      }
    });
  }

  for (auto i = 0; i < max; ++i)
    worker.push(1);

  for (auto &t : threads)
    t.join();

  REQUIRE(remaining == 0);
}

// Dummy work struct.
struct work {
  int label;
  std::string path;
};

TEST_CASE("pop and steal", "[deque]") {
  auto ws = deque::deque<work>();
  auto worker = std::move(ws.first);
  auto stealer = std::move(ws.second);

  auto max = 100000;
  auto nthreads = 4;
  std::vector<std::thread> threads;
  std::atomic<int> remaining(max);

  for (auto i = 0; i < max; ++i)
    worker.push(work{1, "/some/random/path"});

  for (auto i = 0; i < nthreads; ++i) {
    threads.emplace_back([&stealer, &remaining]() {
      auto clone = stealer;
      while (remaining.load(std::memory_order_seq_cst) > 0) {
        auto x = clone.steal();
        if (x) {
          // Can't use REQUIRE here because it isn't thread-safe.
          assert((*x).label == 1);
          remaining.fetch_sub(1);
        }
      }
    });
  }

  while (remaining.load(std::memory_order_seq_cst) > 0) {
    auto x = worker.pop();
    if (x) {
      assert((*x).label == 1);
      remaining.fetch_sub(1);
    }
  }

  for (auto &t : threads)
    t.join();

  REQUIRE(remaining == 0);
}
