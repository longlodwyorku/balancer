#pragma once
#include "sync_stack.hpp"
#include <cstddef>
#include <cstdint>
#include <atomic>

struct connection {
  static constexpr uint8_t READ_PIPE = 1;
  static constexpr uint8_t WRITE_PIPE = 2;
  static constexpr uint8_t REVERSE_DIRECTION = 4;
  static constexpr uint8_t READY = 8;
  int server;
  int client;
  int pipe0;
  int pipe1;
  uint8_t state;

  connection(int client, int server, int pipe0, int pipe1);
  connection() = default;
  void close() const;
  void flip_read_pipe();
  void flip_write_pipe();
  void flip_reverse_direction();
  void flip_ready();
  bool is_read_pipe() const;
  bool is_write_pipe() const;
  bool is_reverse_direction() const;
  bool is_ready() const;
};

class connections_manager {
  struct entry {
    int key;
    size_t conn_index;
    entry() = default;
    entry(int key, size_t conn_index)
      : key(key), conn_index(conn_index) {}
  };
  sync_stack<size_t> empty_slots_stack;
  connection * connections;
  entry * entries;
  size_t * empty_slots;
  std::atomic_size_t number;
  const size_t capacity;
public:
  connections_manager(size_t cap);
  ~connections_manager();
  bool add(int client, int server, int pipe0, int pipe1);
  void remove(int fd);
  connection* get(int fd) const;
};

