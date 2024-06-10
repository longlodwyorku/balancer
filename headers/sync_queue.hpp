#pragma once
#include <type_traits>
#include <utility>
#include <atomic>

namespace ring {
  static constexpr bool OUT = true;
  static constexpr bool IN = false;
  static constexpr bool SOFT = true;
  static constexpr bool HARD = false;
  /**
   * @brief A thread-safe queue with a fixed capacity
   * 
   * @tparam T The type of the items in the queue
   * @tparam N The capacity of the queue
   */
  template <typename T, std::size_t N>
  class sync_queue {
    public:
      static constexpr std::size_t capacity = N;
      
      static_assert(N > 0, "Queue capacity must be greater than 0");
      
      /**
       * @brief A move object for the queue, created by a move maker, used to move and commit items in the queue
       * 
       * @tparam Out Whether the move is for moving items out of the queue
       */
      template <bool Out = false, bool Soft = true>
      class transaction;
      
      /**
       * @brief Enqueues an item into the queue can block if other threads are enqueuing or dequeuing
       * 
       * @tparam U The type of the item to enqueue
       * @param item The item to enqueue
       * @return true if the item was enqueued, false if the queue is full
       */
      template <typename U>
      constexpr auto enqueue(U&& item) -> std::enable_if_t<std::is_convertible_v<U, T>, bool>;

      /**
       * @brief Dequeues an item from the queue can block if other threads are enqueuing or dequeuing
       * 
       * @param item The variable to store the dequeued item
       * @return true if an item was dequeued, false if the queue is empty
       */
      constexpr bool dequeue(T& item);

      /**
       * @brief Returns the number of items in the queue
       * 
       * @return The number of items in the queue
       */
      constexpr std::size_t size() const;

      /**
       * @brief Returns the item at the front of the queue
       * 
       * @return The item at the front of the queue
       */
      constexpr T& back() const;

      /**
       * @brief Returns the item at the back of the queue
       * 
       * @return The item at the back of the queue
       */
      constexpr T& front() const;
      std::atomic<std::size_t> head = 0;
      std::atomic<std::size_t> tail = 0;
      std::atomic<std::size_t> base_head = 0;
      std::atomic<std::size_t> base_tail = 0;
    private:
      static constexpr std::size_t increment(const std::size_t idx) {
        return (idx + 1) % buffer_size;
      }
      template <bool Out, bool Soft>
      friend class transaction;
      static constexpr std::size_t buffer_size = N + 1;
      T items[buffer_size];
  };

  template<typename T, std::size_t N>
  template<bool Out, bool Soft>
  class sync_queue<T, N>::transaction {
    public:
      /**
       * @brief Reserves slots in the queue for moving items and initializes the move
       *
       * @param queue The queue to move items from or to
       * @param count The number of slots to reserve
       * @return The number of slots reserved
       */
      constexpr std::size_t prepare(sync_queue &qu, const std::size_t count);

      /**
       * @brief Fills the queue with items from iterator if Out is false else fills the iterator with items from the queue
       * 
       * @tparam It The type of the iterator
       * @param begin The iterator to fill the queue from if Out is false else the iterator is filled with items from the queue
       * @param count The number of items to be moved
       * @return The iterator after filling
       */
      template <typename It>
      constexpr auto execute(const It begin, const std::size_t count) -> std::enable_if_t<std::is_convertible_v<decltype(*std::declval<It>()), T>, It>;
      
      /**
       * @brief Commits the transaction
       * 
       * @return true if the transaction was committed, false if the transaction was not committed
       */
      constexpr bool commit();

      /**
       * @brief Returns the number of items to be moved
       * 
       * @return The number of items to be moved
       */
      constexpr std::size_t size() const;
    private:
      sync_queue *queue = nullptr;
      std::size_t start = 0;
      std::size_t end = 0;
      std::size_t current_start = 0;
  };

  template <typename T, std::size_t N>
  template <typename U>
  constexpr auto sync_queue<T, N>::enqueue(U&& item) -> std::enable_if_t<std::is_convertible_v<U, T>, bool> {
    std::size_t current_tail;
    std::size_t current_head_base;
    std::size_t next_tail;
    do {
      current_tail = tail.load();
      next_tail = increment(current_tail);
      current_head_base = base_head.load();
      if (next_tail == current_head_base) {
        return false;
      }
    } while (!tail.compare_exchange_strong(current_tail, next_tail));
    items[current_tail] = std::forward<U>(item);
    std::size_t current_tail_base;
    do {
      current_tail_base = base_tail.load();
    } while (!(current_tail_base == current_tail && base_tail.compare_exchange_strong(current_tail_base, increment(current_tail_base))));
    return true;
  }

  template <typename T, std::size_t N>
  constexpr bool sync_queue<T, N>::dequeue(T& item) {
    std::size_t current_head;
    std::size_t current_tail_base;
    do {
      current_head = head.load();
      current_tail_base = base_tail.load();
      if (current_head == current_tail_base) {
        return false;
      }
    } while (!head.compare_exchange_strong(current_head, increment(current_head)));
    
    item = std::move(items[current_head]);
    std::size_t current_head_base;
    do {
      current_head_base = base_head.load();
    } while (!(current_head_base == current_head && base_head.compare_exchange_strong(current_head_base, increment(current_head_base))));
    return true;
  }

  template <typename T, std::size_t N>
  constexpr std::size_t sync_queue<T, N>::size() const {
    std::size_t current_head = head.load();
    std::size_t current_tail = tail.load();
    return current_tail < current_head ? current_tail + buffer_size - current_head : (current_tail - current_head);
  }

  template <typename T, std::size_t N>
  constexpr T& sync_queue<T, N>::back() const {
    return items[(base_tail.load() + buffer_size - 1) % buffer_size];
  }

  template <typename T, std::size_t N>
  constexpr T& sync_queue<T, N>::front() const {
    return items[head.load()];
  }

  template <typename T, std::size_t N>
  template <bool Out, bool Soft>
  constexpr std::size_t sync_queue<T, N>::transaction<Out, Soft>::prepare(sync_queue &qu, const std::size_t count) {
    queue = &qu;
    std::size_t end_base;
    if (Out) {
      start = queue->head.load();
      end_base = queue->base_tail.load();
    } else {
      start = queue->tail.load();
      end_base = queue->base_head.load();
    }
    end = start + count;
    std::size_t end_base_comp = (((end_base + Out) % buffer_size > start) ? end_base : (end_base + buffer_size)) - !Out;
    if (Soft) {
      end = std::min(end, end_base_comp);
    } else if (end > end_base_comp) {
      return 0;
    }
    
    end = end % buffer_size;
    if (Out) {
      if (queue->head.compare_exchange_strong(start, end)) {
        current_start = start;
        return end >= start ? end - start : (buffer_size - start + end);
      }
      return 0;
    }
    if (queue->tail.compare_exchange_strong(start, end)) {
      current_start = start;
      return end >= start ? end - start : (buffer_size - start + end);
    }
    return 0;
  }

  template <typename T, std::size_t N>
  template <bool Out, bool Soft>
  constexpr bool sync_queue<T, N>::transaction<Out, Soft>::commit() {
    if (Out) {
      return current_start == end && queue->base_head.compare_exchange_strong(start, current_start);
    }
    return current_start == end && queue->base_tail.compare_exchange_strong(start, current_start);
  }

  template <typename T, std::size_t N>
  template <bool Out, bool Soft>
  template <typename It>
  constexpr auto sync_queue<T, N>::transaction<Out, Soft>::execute(const It begin, const std::size_t count) -> std::enable_if_t<std::is_convertible_v<decltype(*std::declval<It>()), T>, It> {
    auto current = begin;
    if (current_start < end ? (count > end - current_start) : (count > buffer_size - current_start + end)) {
      return current;
    }
    if (Out) {
      for (std::size_t i = 0; current_start != end && i != count; ++i) {
        *current = std::move(queue->items[current_start]);
        ++current;
        current_start = (current_start + 1) % buffer_size;
      }
    }
    else {
      for (std::size_t i = 0; current_start != end && i != count; ++i) {
        queue->items[current_start] = std::move(*current);
        ++current;
        current_start = (current_start + 1) % buffer_size;
      }
    }
    
    return current;
  }

  template <typename T, std::size_t N>
  template <bool Out, bool Soft>
  constexpr std::size_t sync_queue<T, N>::transaction<Out, Soft>::size() const {
    return start < end ? end - start : (buffer_size - start + end);
  }
}
