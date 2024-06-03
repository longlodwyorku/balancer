#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <utility>
#include "defer.hpp"
template <typename T>
class sync_stack {
  T * data;
  std::atomic_size_t number;
  std::atomic_size_t executions_number;
  size_t capacity;
public:
  sync_stack() = default;
  sync_stack(T *const data, size_t size, size_t capacity);
  sync_stack(const sync_stack& other);
  sync_stack& operator=(const sync_stack& other);
  template <typename It>
  size_t pop_half(It iter, size_t n);
  size_t size() const;
  bool pop(T *res);
  template <typename S>
  bool push(S&& v);
  bool executing() const;
};

template <typename T>
sync_stack<T>::sync_stack(T *const data, size_t size, size_t capacity)
  : data(data), number(size), executions_number(0), capacity(capacity) {}

template <typename T>
sync_stack<T>::sync_stack(const sync_stack& other)
  : data(other.data), number(other.number.load()), executions_number(other.executions_number.load()), capacity(other.capacity) {}

template <typename T>
sync_stack<T>& sync_stack<T>::operator=(const sync_stack& other) {
  data = other.data;
  number = other.number.load();
  executions_number = other.executions_number.load();
  capacity = other.capacity;
  return *this;
}

template <typename T>
template <typename It>
size_t sync_stack<T>::pop_half(It iter, size_t n) {
  executions_number++;
  defer(executions_number--);
  size_t current_number = number.load();
  size_t next_number = current_number / 2;
  next_number = (current_number - next_number) <= n ? next_number : (current_number - n);
  while (!number.compare_exchange_strong(current_number, next_number)) {
    std::this_thread::yield();
    current_number = number.load();
    next_number = current_number / 2 + 1;
    next_number = (current_number - next_number) <= n ? next_number : (current_number - n);
  }
  for (size_t k = next_number; k < current_number; ++k, iter++) {
    *iter = std::move(data[k]);
  }
  return current_number - next_number;
}

template <typename T>
size_t sync_stack<T>::size() const {
  return number.load();
}

template <typename T>
bool sync_stack<T>::pop(T *res) {
  executions_number++;
  defer(executions_number--);
  size_t current_number = number.load();
  if (current_number == 0) {
    return false;
  }
  while (!number.compare_exchange_strong(current_number, current_number - 1)) {
    std::this_thread::yield();
    current_number = number.load();
    if (current_number == 0) {
      return false;
    }
  }
  *res = std::move(data[current_number - 1]);
  return true;
}

template <typename T>
template <typename S>
bool sync_stack<T>::push(S&& v) {
  executions_number++;
  defer(executions_number--);
  size_t current_number = number.load();
  if (current_number == capacity) {
    return false;
  }
  while (!number.compare_exchange_strong(current_number, current_number + 1)) {
    std::this_thread::yield();
    current_number = number.load();
    if (current_number == capacity) {
      return false;
    }
  }
  data[current_number] = std::forward<S>(v);
  return true;
}

template <typename T>
bool sync_stack<T>::executing() const {
  return executions_number > 0;
}
