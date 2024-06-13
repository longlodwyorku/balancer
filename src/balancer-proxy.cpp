#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <endian.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include "../headers/defer.hpp"
#include "../headers/worker.hpp"
#include "../headers/endian_convert.hpp"
#include <thread>

static constexpr size_t BC_MES_SIZE = 1 + sizeof(size_t) + sizeof(float) * 2;

int init_broadcast_listen(int ep, const char *address, uint16_t port) {
  int broadcast_socket = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (broadcast_socket < 0) {
    perror(nullptr);
    return broadcast_socket;
  }
  const int opt = 1;
  if (setsockopt(broadcast_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror(nullptr);
    close(broadcast_socket);
    return -1;
  }

  sockaddr_in broadcast_addr;
  broadcast_addr.sin_family = AF_INET;
  broadcast_addr.sin_addr.s_addr = inet_addr(address);
  broadcast_addr.sin_port = htons(port);

  if (bind(broadcast_socket, reinterpret_cast<sockaddr*>(&broadcast_addr), sizeof(broadcast_addr)) < 0) {
    perror(nullptr);
    close(broadcast_socket);
    return -1;
  }
  if (epoll_add(ep, broadcast_socket, EPOLLIN | EPOLLET) < 0) {
    perror(nullptr);
    close(broadcast_socket);
    return -1;
  }
  return broadcast_socket;
}

int init_tcp_listen(int ep, const char *address, uint16_t port, int max_connections) {
  int listen_socket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (listen_socket < 0) {
    perror(nullptr);
    return listen_socket;
  }
  
  sockaddr_in listen_addr;
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_addr.s_addr = inet_addr(address);
  listen_addr.sin_port = htons(port);

  if (bind(listen_socket, reinterpret_cast<sockaddr *>(&listen_addr), sizeof(listen_addr)) < 0) {
    perror(nullptr);
    close(listen_socket);
    return -1;
  }

  if (listen(listen_socket, max_connections) < 0) {
    perror(nullptr);
    close(listen_socket);
    return -1;
  }
  if (epoll_add(ep, listen_socket, EPOLLET | EPOLLIN) < 0) {
    perror(nullptr);
    close(listen_socket);
    return -1;
  }
  return listen_socket;
}

void close_all(std::vector<int>& fds) {
  for (int fd : fds) {
    close(fd);
  }
}

void handle_broadcast(const epoll_event &ev, int index, std::vector<std::tuple<sockaddr_in, uint64_t, float, float>> &servers, std::shared_mutex &sm) {
  std::unique_lock<std::shared_mutex> ul(sm);
  char buf[BC_MES_SIZE];
  auto &[server, timestamp, cpu, mem] = servers[index];
  ssize_t n = recvfrom(ev.data.fd, buf, BC_MES_SIZE, 0, nullptr, nullptr);
  if (n != BC_MES_SIZE) {
    std::cerr << "incorrect broadcast message size" << std::endl;
    timestamp = 0;
    cpu = 1;
    mem = 1;
    return;
  }
  timestamp = endian_convert::ntoh(*reinterpret_cast<size_t*>(buf + 1));
  cpu = endian_convert::ntoh(*reinterpret_cast<float*>(buf + 1 + sizeof(size_t)));
  mem = endian_convert::ntoh(*reinterpret_cast<float*>(buf + 1 + sizeof(size_t) + sizeof(float)));
}

void handle_tcp(const epoll_event &ev, worker::queue_t &fd_queue) {
  for (int client = accept4(ev.data.fd, nullptr, nullptr, SOCK_NONBLOCK); client > 0; client = accept4(ev.data.fd, nullptr, nullptr, SOCK_NONBLOCK)) {
    while (!fd_queue.enqueue(client)) {
      std::this_thread::yield();
    }
  }
}

int main (int argc, char *argv[]) {
  if (argc < 4 || argc % 4) {
    std::cerr << "incorrect number of argument" << std::endl << "proxy_server <max number of connections> <listen address> <listen port> [<server address> <server port> <server monitor address> <server monitor port>]..." << std::endl;
  }
  int max_connections = atoi(argv[1]);
  const char *listen_addr = argv[2];
  uint16_t listen_port = atoi(argv[3]);
  size_t servers_num = argc / 4 - 1;

  std::cout << "listening on " << listen_addr << " " << listen_port << std::endl;
  
  std::vector<std::tuple<sockaddr_in, size_t, float, float>> servers;
  servers.reserve(servers_num);

  std::vector<int> broadcast_sockets;
  broadcast_sockets.reserve(servers_num);
  
  std::shared_mutex sm;
  std::unordered_map<int, std::function<void(const epoll_event&)>> router;
  std::vector<worker> workers;
  
  auto counts = std::thread::hardware_concurrency();
  workers.reserve(counts);
  std::vector<std::thread> threads;
  worker::queue_t fd_queue;

  for (size_t k = 0; k < counts; ++k) {
    int worker_epoll_fd = epoll_create1(0);
    if (worker_epoll_fd < 0) {
      perror(nullptr);
      std::cerr << "failed to create worker epoll_fd" << std::endl;
      return 1;
    }
    workers.emplace_back(&fd_queue, worker_epoll_fd);
  }

  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror(nullptr);
    std::cerr << "failed to create epoll_fd" << std::endl;
    return 1;
  }
  defer(close(epoll_fd));

  for (size_t k = 0; k < servers_num; ++k) {
    size_t i = k + 1;
    std::cout << argv[i*4+2] << " " << argv[i*4+3] << std::endl;
    int broadcast_socket = init_broadcast_listen(epoll_fd, argv[i * 4 + 2], atoi(argv[i * 4 + 3]));
    if (broadcast_socket < 0) {
      std::cerr << "failed to create broadcast_socket for " << argv[i * 4 + 2] << " " << argv[i * 4 + 3] << std::endl;
      continue;
    }
    broadcast_sockets.push_back(broadcast_socket);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[i * 4]);
    server_addr.sin_port = htons(atoi(argv[i * 4 + 1]));

    servers.push_back(std::make_tuple(server_addr, 0, 1, 1));
    router[broadcast_socket] = std::bind(&handle_broadcast, std::placeholders::_1, k, std::ref(servers), std::ref(sm));
  }
  defer(close_all(broadcast_sockets));

  int listen_socket = init_tcp_listen(epoll_fd, listen_addr, listen_port, max_connections);
  if (listen_socket < 0) {
    std::cerr << "failed to create listen socket" << std::endl;
    return 1;
  }
  defer(close(listen_socket));

  router[listen_socket] = std::bind(&handle_tcp, std::placeholders::_1, std::ref(fd_queue));

  epoll_event *events = new epoll_event[servers_num + 1];
  defer(delete[] events);
  for (size_t k = 0; k < counts; ++k) {
    threads.emplace_back([&workers, k, &servers, &sm] {
      workers[k].run(servers.begin(), servers.end(), sm);
    });
  }

  while (true) {
    int n = epoll_wait(epoll_fd, events, servers_num + 1, -1);
    for (int k = 0; k < n; ++k) {
      router[events[k].data.fd](events[k]);
    }
  }
  for (auto &t : threads) {
    t.join();
  }
  return 0;
}
