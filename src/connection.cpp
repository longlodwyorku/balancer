#include "../headers/connection.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

void connection::clean_up(int ep) const {
  epoll_del(ep, server);
  epoll_del(ep, client);
  ::close(client);
  ::close(server);
  ::close(pipes[0]);
  ::close(pipes[1]);
}

int connection::write(int fd) {
  ssize_t r = 0; 
  while(bytes_in_pipe && (r = ::splice(pipes[0], nullptr, fd, nullptr, bytes_in_pipe, SPLICE_F_NONBLOCK | SPLICE_F_MORE)) > 0) {
    bytes_in_pipe -= r;
  }
  return r;
}

int connection::read(int fd, size_t buffer_size) {
  ssize_t s = ::splice(fd, nullptr, pipes[1], nullptr, buffer_size, SPLICE_F_NONBLOCK | SPLICE_F_MORE);
  if (s < 0) {
    return s;
  }
  bytes_in_pipe = s;
  return s;
}

int connection::get_peer(int fd) const {
  if (fd == client) {
    return server;
  }
  else if (fd == server) {
    return client;
  }
  return -1;
}

uint32_t *connection::get_event(int fd) {
  if (fd == client) {
    return &client_event;
  }
  else if (fd == server) {
    return &server_event;
  }
  return nullptr;
}

connections_manager::connections_manager(int& ep)
  :ep(ep) {}

connections_manager::~connections_manager() {
  for (auto& conn : connections) {
    conn.clean_up(ep);
  }
}

void connections_manager::add(const connection& conn) {
  size_t current_number = connections.size();
  if (!empty_slots.empty()) {
    current_number = empty_slots.back();
    empty_slots.pop_back();
    connections[current_number] = conn;
  }
  else {
    connections.push_back(conn);
  }
  fd_to_index[conn.server] = current_number;
  fd_to_index[conn.client] = current_number;
}

void connections_manager::remove(int fd) {
  if (fd_to_index.find(fd) == fd_to_index.end()) {
    return;
  }
  size_t conn_index = fd_to_index[fd];
  connection& removed_conn = connections[conn_index];
  removed_conn.clean_up(ep);
  fd_to_index.erase(removed_conn.server);
  fd_to_index.erase(removed_conn.client);
  empty_slots.push_back(conn_index);
}

connection* connections_manager::get(int fd) {
  if (fd_to_index.find(fd) == fd_to_index.end()) {
    return nullptr;
  }
  size_t conn_index = fd_to_index.at(fd);
  return &(connections[conn_index]);
}

int epoll_add(int ep, int fd, uint32_t event) {
  epoll_event ev;
  ev.events = event;
  ev.data.fd = fd;
  return epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
}

int epoll_del(int ep, int fd) {
  return epoll_ctl(ep, EPOLL_CTL_DEL, fd, nullptr);
}

int epoll_mod(int ep, int fd, uint32_t event) {
  epoll_event ev;
  ev.events = event;
  ev.data.fd = fd;
  return epoll_ctl(ep, EPOLL_CTL_MOD, fd, &ev);
}
