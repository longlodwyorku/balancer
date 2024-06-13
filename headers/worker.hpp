#pragma once

#include "connection.hpp"
#include <cstdio>
#include <shared_mutex>
#include "sync_queue.hpp"
#include <unistd.h>
#include <unordered_map>
#include <functional>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <iostream>

struct worker {
  static constexpr size_t MAX_EVENTS = 255;
  using queue_t = ring::sync_queue<int, MAX_EVENTS>;
  queue_t *fd_queue;
  connections_manager connections;
  std::unordered_map<int, std::function<void(const epoll_event&)>> handlers;
  int epoll_fd;

  worker(queue_t *fd_queue, int epoll_fd);
  ~worker();

  template <typename Iter>
  void run(Iter servers_begin, Iter servers_end, std::shared_mutex& sm);

  template <typename Iter>
  void dequeue(Iter servers_begin, Iter servers_end, std::shared_mutex &sm);

  void handle_server_connect(const epoll_event& ev, int client_fd);
  void handle_data_transfer(const epoll_event& ev);
};

bool valid_timestamp(uint64_t timestamp);

int init_server_connection(const sockaddr_in& addr);

template <typename Iter>
void worker::dequeue(Iter servers_begin, Iter servers_end, std::shared_mutex &sm) {
  
  int client_fd;
  while (fd_queue->dequeue(client_fd)) {
    std::shared_lock<std::shared_mutex> sl(sm);
    Iter server = get_server(servers_begin, servers_end);
    if (server == servers_end) {
      close(client_fd);
      return;
    }
    int server_fd = init_server_connection(std::get<0>(*server));
    if (server_fd < 0) {
      std::cerr << "failed to create server connection" << std::endl;
      close(client_fd);
      continue;
    }
    if (epoll_add(epoll_fd, server_fd, EPOLLOUT) < 0) {
      perror(nullptr);
      std::cerr << "failed to add server socket to epoll" << std::endl;
      close(server_fd);
      close(client_fd);
      continue;
    }
    handlers[server_fd] = std::bind(&worker::handle_server_connect, this, std::placeholders::_1, client_fd);
  }
}

template <typename Iter>
Iter get_server(Iter begin, Iter end) {
  Iter first_valid_time = end;
  for (auto it = begin; it != end; ++it) {
    std::tuple<sockaddr_in, size_t, float, float> cur = *it;
    if (valid_timestamp(std::get<1>(cur))) {
      first_valid_time = it;
      break;
    }
  }
  if (first_valid_time == end) {
    return end;
  }
  Iter res = first_valid_time;
  float m = std::get<2>(*res) + std::get<3>(*res);
  for (auto it = first_valid_time + 1; it != end; ++it) {
    std::tuple<sockaddr_in, size_t, float, float> cur = *it;
    float cur_m = std::get<2>(cur) + std::get<3>(cur);
    if (valid_timestamp(std::get<1>(cur)) && cur_m < m) {
      res = it;
      m = cur_m;
    }
  }
  return res;
}

template <typename Iter>
void worker::run(Iter servers_begin, Iter servers_end, std::shared_mutex& sm) {
  epoll_event *events = new epoll_event[MAX_EVENTS];
  size_t events_size = MAX_EVENTS;
  while (true) {
    dequeue(servers_begin, servers_end, sm);
    if (events_size < handlers.size()) {
      delete[] events;
      events = new epoll_event[handlers.size()];
      events_size = handlers.size();
    }
    ssize_t n = epoll_wait(epoll_fd, events, events_size, MAX_EVENTS + 1 - fd_queue->size());
    if (n < 0) {
      perror(nullptr);
      std::cerr << "epoll_wait failed" << std::endl;
      continue;
    }
    for (ssize_t i = 0; i < n; ++i) {
      auto it = handlers.find(events[i].data.fd);
      if (it == handlers.end()) {
        std::cerr << "no handler for fd " << events[i].data.fd << std::endl;
        continue;
      }
      it->second(events[i]);
    }
  }
  delete[] events;
}
