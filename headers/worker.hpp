#pragma once

#include "connection.hpp"
#include <cstdio>
#include <cstdlib>
#include <shared_mutex>
#include <unistd.h>
#include <unordered_map>
#include <functional>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <iostream>
#include <atomic>

struct worker {
  static constexpr size_t MAX_EVENTS = 511;
  static inline int max_connections = 0;
  std::atomic_int *number_of_connections;
  std::atomic_bool *has_connections;
  connections_manager connections;
  std::unordered_map<int, std::function<void(const epoll_event&)>> handlers;
  int epoll_fd;

  worker(std::atomic_int *number_of_connections, std::atomic_bool *has_connections, int epoll_fd);
  ~worker();

  template <typename Iter>
  void run(Iter servers_begin, Iter servers_end, std::shared_mutex& sm, int listens_socket);
  template<typename Iter>
  void accept_connections(Iter servers_begin, Iter servers_end, std::shared_mutex& sm, int listens_socket);
  void handle_server_connect(const epoll_event& ev);
  void handle_data_transfer(const epoll_event& ev);
  void handle_preread_client(const epoll_event &ev);
  void on_client_connect(int client_fd, const sockaddr_in &addr);
};

bool valid_timestamp(uint64_t timestamp);

template <typename Iter>
Iter get_server(Iter begin, Iter end) {
  float s = 0;
  for (auto it = begin; it != end; ++it) {
    std::tuple<sockaddr_in, size_t, float, float> cur = *it;
    if (valid_timestamp(std::get<1>(cur))) {
      float cur_m = std::get<2>(cur) * std::get<3>(cur);
      cur_m < 1 && (s += (1 - cur_m));
    }
  }
  float r = static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / s));
  if (r >= s) {
    r = s;
  }
  s = 0;
  for (auto it = begin; it != end; ++it) {
    std::tuple<sockaddr_in, size_t, float, float> cur = *it;
    if (valid_timestamp(std::get<1>(cur))) {
      float cur_m = std::get<2>(cur) + std::get<3>(cur);
      if (cur_m < 1 && (s += (1 - cur_m)) >= r) {
        return it;
      }
    }
  }
  return end;
}

template <typename Iter>
void worker::accept_connections(Iter servers_begin, Iter servers_end, std::shared_mutex& sm, int listen_socket) {
  while (*number_of_connections < max_connections) {
    sm.lock_shared();
    auto server = get_server(servers_begin, servers_end);
    sm.unlock_shared();
    if (server == servers_end) {
      std::cerr << "no server available" << std::endl;
      return;
    }
    int client = accept4(listen_socket, nullptr, nullptr, SOCK_NONBLOCK);
    //std::cout << "accepted " << client << std::endl;
    if (client < 0) {
      *has_connections = false;
      break;
    }
    
    on_client_connect(client, std::get<0>(*server));
    //std::cout << "done on client" << std::endl;
  }
}

template <typename Iter>
void worker::run(Iter servers_begin, Iter servers_end, std::shared_mutex& sm, int listen_socket) {
  epoll_event *events = new epoll_event[MAX_EVENTS];
  size_t events_size = MAX_EVENTS;
  handlers[listen_socket] = [servers_begin, servers_end, this, &sm](const epoll_event &ev) {
    *has_connections = true;
    accept_connections(servers_begin, servers_end, sm, ev.data.fd);
  };
  while (true) {
    if (*has_connections) {
      accept_connections(servers_begin, servers_end, sm, listen_socket);
    }
    if (events_size < handlers.size()) {
      epoll_event *events_new = new epoll_event[handlers.size()];
      if (events_new == nullptr) {
        perror(nullptr);
        std::cerr << "failed to allocate memory" << std::endl;
      }
      else {
        delete[] events;
        events = events_new;
        events_size = handlers.size();
      }
    }
    ssize_t n = epoll_wait(epoll_fd, events, events_size, -1);
    if (n < 0) {
      perror(nullptr);
      std::cerr << "epoll_wait failed" << std::endl;
      continue;
    }
    for (ssize_t i = 0; i < n; ++i) {
      auto it = handlers.find(events[i].data.fd);
      if (it == handlers.end()) {
        std::cerr << "no handler for fd " << events[i].data.fd << std::endl;
        epoll_del(epoll_fd, events[i].data.fd);
        close(events[i].data.fd);
        continue;
      }
      it->second(events[i]);
    }
  }
  delete[] events;
}
