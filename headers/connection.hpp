#pragma once
#include <cstddef>
#include <sys/types.h>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct connection {
  ssize_t bytes_in_pipe;
  int pipes[2];
  int server;
  int client;
  uint32_t server_event;
  uint32_t client_event;
  int des;

  void clean_up(int ep) const;
  int write(int fd);
  int read(int fd, size_t buffer_size);
  int get_peer(int fd) const;
  uint32_t *get_event(int fd);
};

class connections_manager {
  std::vector<size_t> empty_slots;
  std::vector<connection> connections;
  std::unordered_map<int, size_t> fd_to_index;
  const int ep;
public:
  connections_manager(int& ep);
  ~connections_manager();
  void add(const connection& conn);
  void remove(int fd);
  connection* get(int fd);
};

int epoll_add(int ep, int fd, uint32_t event);
int epoll_del(int ep, int fd);
int epoll_mod(int ep, int fd, uint32_t event);
