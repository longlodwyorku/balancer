#include "../headers/connection.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

static constexpr size_t CONNECTION_BUFFER_SIZE = 65536;

void connection::clean_up() const {
  epoll_del(ep, server);
  epoll_del(ep, client);
  ::close(client);
  ::close(server);
  ::close(pipes[0]);
  ::close(pipes[1]);
}

int connection::write(int fd) {
  int peer;
  if (fd == server) {
    peer = client;
  }
  else if (fd == client) {
    peer = server;
  }
  else {
    return -2;
  }

  ssize_t r; 
  while(bytes_in_pipe && (r = ::splice(pipes[0], nullptr, fd, nullptr, bytes_in_pipe, SPLICE_F_NONBLOCK | SPLICE_F_MORE)) > 0) {
    bytes_in_pipe -= r;
  }

  if (bytes_in_pipe) {
    return fd;
  }
  if (epoll_mod(ep, peer, EPOLLIN | EPOLLRDHUP) < 0 || epoll_mod(ep, fd, EPOLLIN | EPOLLRDHUP) < 0) {
    perror(nullptr);
    return -1;
  }
  return peer;
}

int connection::read(int fd) {
  int peer;
  if (fd == server) {
    peer = client;
  }
  else if (fd == client) {
    peer = server;
  }
  else {
    return -2;
  }
  ssize_t s = ::splice(fd, nullptr, pipes[1], nullptr, CONNECTION_BUFFER_SIZE, SPLICE_F_NONBLOCK | SPLICE_F_MORE);
  if (s < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      return fd;
    }
    perror(nullptr);
    return -1;
  }
  bytes_in_pipe = s;
  if (epoll_mod(ep, fd, EPOLLRDHUP) < 0 || epoll_mod(ep, peer, EPOLLET | EPOLLOUT) < 0) {
    perror(nullptr);
    return -1;
  }
  return peer;
}

connections_manager::connections_manager(int& ep)
  :ep(ep) {}

connections_manager::~connections_manager() {
  for (auto& conn : connections) {
    conn.clean_up();
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
  removed_conn.clean_up();
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
