#include "../headers/worker.hpp"
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <sys/epoll.h>
#include <unistd.h>

worker::worker(queue_t *fd_queue, int epoll_fd) : fd_queue(fd_queue), connections(epoll_fd), epoll_fd(epoll_fd) {}

worker::~worker() {
  close(epoll_fd);
}

void worker::handle_server_connect(const epoll_event& ev, int client_fd) {
  if (ev.events & EPOLLERR) {
    perror(nullptr);
    std::cerr << "failed to connect server socket" << std::endl;
    epoll_del(epoll_fd, ev.data.fd);
    close(ev.data.fd);
    close(client_fd);
    handlers.erase(ev.data.fd);
    return;
  }

  int err = 0;
  socklen_t err_len = sizeof(err);
  getsockopt(ev.data.fd, SOL_SOCKET, SO_ERROR, &err, &err_len);
  if (err) {
    if (err == EINPROGRESS) {
      std::cout << "connecting..." << std::endl;
      return;
    }
    std::cerr << "failed to connect server socket: " << std::strerror(err) << std::endl;
    epoll_del(epoll_fd, ev.data.fd);
    close(ev.data.fd);
    close(client_fd);
    handlers.erase(ev.data.fd);
  }
  connection conn;
  conn.client = client_fd;
  conn.server = ev.data.fd;
  conn.ep = epoll_fd;
  if (pipe2(conn.pipes, O_DIRECT | O_NONBLOCK) < 0) {
    perror(nullptr);
    std::cerr << "failed to create pipe" << std::endl;
    conn.clean_up();
    handlers.erase(ev.data.fd);
    return;
  }
  if (epoll_add(epoll_fd, client_fd, EPOLLIN | EPOLLRDHUP) < 0) {
    perror(nullptr);
    conn.clean_up();
    handlers.erase(ev.data.fd);
    std::cerr << "failed to add client to epoll" << std::endl;
    return;
  }
  if (epoll_mod(epoll_fd, conn.server, EPOLLRDHUP) < 0) {
    perror(nullptr);
    conn.clean_up();
    handlers.erase(ev.data.fd);
    std::cerr << "failed to add server to epoll" << std::endl;
    return;
  }

  connections.add(conn);
  handlers[conn.server] = std::bind(&worker::handle_data_transfer, this, std::placeholders::_1);
  handlers[conn.client] = std::bind(&worker::handle_data_transfer, this, std::placeholders::_1);
}

void worker::handle_data_transfer(const epoll_event& ev) {
  connection* conn = connections.get(ev.data.fd);
  if (conn == nullptr) {
    std::cerr << "no connection for fd " << ev.data.fd << std::endl;
    handlers.erase(ev.data.fd);
    return;
  }
  if (ev.events & EPOLLERR) {
    perror(nullptr);
    handlers.erase(conn->server);
    handlers.erase(conn->client);
    connections.remove(ev.data.fd);
    return;
  }
  ssize_t n = 0;
  if (ev.events & EPOLLOUT) {
    n = conn->write(ev.data.fd);
  }
  else if (ev.events & EPOLLIN) {
    n = conn->read(ev.data.fd);
  }

  if ((ev.events & EPOLLRDHUP || ev.events & EPOLLHUP) && conn->bytes_in_pipe == 0) {
    std::cout << "connection closed server: " << conn->server << " client: " << conn->client << " fd: " << ev.data.fd<< std::endl;
    handlers.erase(conn->server);
    handlers.erase(conn->client);
    connections.remove(ev.data.fd);
    return;
  }

  switch (n) {
    case -1:
      perror(nullptr);
      [[fallthrough]];
    case -2:
      std::cerr << "failed to splice server: " << conn->server << " client: " << conn->client << " fd: " << ev.data.fd << std::endl;
      handlers.erase(conn->client);
      handlers.erase(conn->server);
      connections.remove(ev.data.fd);
      break;
    default:
      break;
  }
}

bool valid_timestamp(uint64_t timestamp) {
  return timestamp > (uint64_t) std::chrono::duration_cast<std::chrono::seconds>((std::chrono::system_clock::now() - std::chrono::seconds(5)).time_since_epoch()).count();
}

int init_server_connection(const sockaddr_in& server) {
  int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (server_fd < 0) {
    std::cerr << "failed to create server socket" << std::endl;
    return -1;
  }
  if (connect(server_fd, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) < 0) {
    if (errno != EINPROGRESS) {
      std::cerr << "failed to connect server socket" << std::endl;
      close(server_fd);
      return -1;
    }
  }
  return server_fd;
}
