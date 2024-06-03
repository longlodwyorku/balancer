#include "../headers/connection.hpp"
#include <cstddef>
#include <unistd.h>

connection::connection(int client, int server, int pipe0, int pipe1)
  : client(client), server(server), pipe0(pipe0), pipe1(pipe1), state(0) {}

void connection::close() const {
  ::close(client);
  ::close(server);
  ::close(pipe0);
  ::close(pipe1);
}

void connection::flip_read_pipe() {
  state ^= connection::READ_PIPE;
}

void connection::flip_write_pipe() {
  state ^= connection::WRITE_PIPE;
}

void connection::flip_reverse_direction() {
  state ^= connection::REVERSE_DIRECTION;
}

void connection::flip_ready() {
  state ^= connection::READY;
}

bool connection::is_read_pipe() const {
  return state & connection::READ_PIPE;
}

bool connection::is_write_pipe() const {
  return state & connection::WRITE_PIPE;
}

bool connection::is_reverse_direction() const {
  return state & connection::REVERSE_DIRECTION;
}

bool connection::is_ready() const {
  return state & connection::READY;
}

connections_manager::connections_manager(size_t cap)
  : connections(new connection[cap]), entries(new entry[cap]), empty_slots(new size_t[cap]), number(0), capacity(cap) {
  empty_slots_stack = sync_stack<size_t>(empty_slots, 0, capacity);
  entries[0].key = -1;
}

connections_manager::~connections_manager() {
  for (size_t k = 0, n = 0; k < capacity || n == number.load(); ++k) {
    if (entries[k].key == k) {
      connections[entries[k].conn_index].close();
      ++n;
    }
  }
  delete [] connections;
  delete [] entries;
  delete [] empty_slots;
}

bool connections_manager::add(int client, int server, int pipe0, int pipe1) {
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
  empty_slots_stack.pop(&current_number);
  connections[current_number] = connection(client, server, pipe0, pipe1);
  entries[client] = entry(client, current_number);
  entries[server] = entry(server, current_number);
  entries[pipe0] = entry(pipe0, current_number);
  entries[pipe1] = entry(pipe1, current_number);
  return true;
}

void connections_manager::remove(int fd) {
  if (entries[fd].key != fd) {
    return;
  }
  size_t last = number.load();
  while (!number.compare_exchange_strong(last, last - 1)) {
    std::this_thread::yield();
    last = number.load();
  }
  size_t conn_index = entries[fd].conn_index;
  connection& removed_conn = connections[conn_index];
  removed_conn.close();
  entries[removed_conn.server].key = 0;
  entries[removed_conn.client].key = 0;
  entries[removed_conn.pipe0].key = 0;
  entries[removed_conn.pipe1].key = 0;
  empty_slots_stack.push(conn_index);
}

connection* connections_manager::get(int fd) const {
  if (fd >= capacity || entries[fd].key != fd) {
    return nullptr;
  }
  return connections + entries[fd].conn_index;
}
