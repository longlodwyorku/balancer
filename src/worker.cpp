#include "../headers/worker.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <ostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

worker::worker(std::atomic_int *number_of_connections, std::atomic_bool *has_connections, int epoll_fd) : number_of_connections(number_of_connections), has_connections(has_connections), connections(epoll_fd), epoll_fd(epoll_fd) {}

worker::~worker() {
  close(epoll_fd);
}

static size_t get_buffer_size(int number_of_connections) {
  return 1 + (1023 / (number_of_connections / 1024 + 1));
}

static void write_to(std::unordered_map<int, std::function<void(const epoll_event&)>> &handlers, connections_manager &connections, std::atomic_int *number_of_connections, connection *conn, int fd, int peer, uint32_t *fd_events, uint32_t *peer_events) {
  while (*fd_events & EPOLLOUT && conn->bytes_in_pipe && conn->des == fd) {
    int n = conn->write(fd);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        *fd_events ^= EPOLLOUT;
      }
      else {
        perror(nullptr);
        std::cerr << "failed to splice server: " << conn->server << " client: " << conn->client << " fd: " << fd << std::endl;
        handlers.erase(conn->client);
        handlers.erase(conn->server);
        connections.remove(fd);
        (*number_of_connections)--;
        return;
      }
    }
    //std::cout << "write " << n << " to " << fd << std::endl;
    if (n == 0) {
      std::cerr << "write 0 to " << fd << std::endl;
      handlers.erase(conn->client);
      handlers.erase(conn->server);
      connections.remove(fd);
      (*number_of_connections)--;
    }
    if (conn->bytes_in_pipe) {
      return;
    }
    if (*peer_events & EPOLLIN) {
      if (*peer_events & EPOLLRDHUP) {
        //std::cout << "shutting down write " << fd << std::endl;
        if(shutdown(fd, SHUT_WR) < 0) {
          perror(nullptr);
          std::cerr << "failed to shutdown write " << fd << std::endl;
          handlers.erase(conn->client);
          handlers.erase(conn->server);
          connections.remove(fd);
        }
        return;
      }
      n = conn->read(peer, get_buffer_size(number_of_connections->load()));
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          *peer_events ^= EPOLLIN;
        }
        else {
          perror(nullptr);
          std::cerr << "failed to splice server: " << conn->server << " client: " << conn->client << " fd: " << fd << std::endl;
          handlers.erase(conn->client);
          handlers.erase(conn->server);
          connections.remove(fd);
          return;
        }
      }
      //std::cout << "read " << n << " from " << peer << std::endl;
      if (n == 0 || *peer_events & EPOLLRDHUP) {
        //std::cout << "shutting down write " << fd << std::endl;
        if(shutdown(fd, SHUT_WR) < 0) {
          perror(nullptr);
          std::cerr << "failed to shutdown write " << fd << std::endl;
          handlers.erase(conn->client);
          handlers.erase(conn->server);
          connections.remove(fd);
        }
        *peer_events ^= ((*peer_events & EPOLLRDHUP) | EPOLLIN);
      }
      conn->des = fd;
    }
  }
}

static void read_from(std::unordered_map<int, std::function<void(const epoll_event&)>> &handlers, connections_manager &connections, std::atomic_int *number_of_connections, connection *conn, int fd, int peer, uint32_t *fd_events, uint32_t *peer_events) {
  while (*fd_events & EPOLLIN && conn->bytes_in_pipe == 0) {
    if (*fd_events & EPOLLRDHUP) {
      //std::cout << "shutting down write " << peer << std::endl;
      if (shutdown(peer, SHUT_WR) < 0) {
        perror(nullptr);
        std::cerr << "failed to shutdown write " << peer << std::endl;
        handlers.erase(conn->client);
        handlers.erase(conn->server);
        connections.remove(fd);
        (*number_of_connections)--;
        return;
      }
      *fd_events ^= ((*fd_events & EPOLLRDHUP) | EPOLLIN);
      return;
    }
    int n = conn->read(fd, get_buffer_size(number_of_connections->load()));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        *fd_events ^= EPOLLIN;
      }
      else {
        perror(nullptr);
        std::cerr << "failed to splice server: " << conn->server << " client: " << conn->client << " fd: " << fd << std::endl;
        handlers.erase(conn->client);
        handlers.erase(conn->server);
        connections.remove(fd);
        (*number_of_connections)--;
        return;
      }
    }
    //std::cout << "read " << n << " from " << fd << std::endl;
    if (n == 0 || *fd_events & EPOLLRDHUP) {
      //std::cout << "shutting down write " << peer << std::endl;
      if (shutdown(peer, SHUT_WR) < 0) {
        perror(nullptr);
        std::cerr << "failed to shutdown write " << peer << std::endl;
        handlers.erase(conn->client);
        handlers.erase(conn->server);
        connections.remove(fd);
        (*number_of_connections)--;
        return;
      }
      *fd_events ^= ((*fd_events & EPOLLRDHUP) | EPOLLIN);
      return;
    }
    conn->des = peer;
    if (*peer_events & EPOLLOUT && conn->bytes_in_pipe) {
      n = conn->write(peer);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          *peer_events ^= EPOLLIN;
        }
        else {
          perror(nullptr);
          std::cerr << "failed to splice server: " << conn->server << " client: " << conn->client << " fd: " << fd << std::endl;
          handlers.erase(conn->client);
          handlers.erase(conn->server);
          connections.remove(fd);
          (*number_of_connections)--;
          return;
        }
      }
      //std::cout << "write " << n << " from " << peer << std::endl;
      if (n == 0) {
        std::cerr << "write 0 to " << fd << std::endl;
        handlers.erase(conn->client);
        handlers.erase(conn->server);
        connections.remove(fd);
        (*number_of_connections)--;
      }
    }
  }
}

void worker::handle_server_connect(const epoll_event& ev) {
  connection *conn = connections.get(ev.data.fd);
  if (!conn) {
    std::cerr << "no connection for " << ev.data.fd << std::endl;
    handlers.erase(ev.data.fd);
    return;
  }
  if (conn->server != ev.data.fd) {
    std::cerr << "invalid connection for " << ev.data.fd << std::endl;
    handlers.erase(ev.data.fd);
    handlers.erase(conn->server);
    handlers.erase(conn->client);
    connections.remove(ev.data.fd);
    (*number_of_connections)--;
  }
  if (ev.events & EPOLLERR || ev.events & EPOLLHUP || ev.events & EPOLLPRI) {
    perror(nullptr);
    std::cerr << "failed to connect server socket" << std::endl;
    handlers.erase(ev.data.fd);
    handlers.erase(conn->client);
    connections.remove(ev.data.fd);
    (*number_of_connections)--;
    return;
  }

  int err = 0;
  socklen_t err_len = sizeof(err);
  getsockopt(ev.data.fd, SOL_SOCKET, SO_ERROR, &err, &err_len);
  if (err) {
    if (err == EINPROGRESS) {
      //std::cout << "server connection in progress for " << conn->client << std::endl;
      return;
    }
    std::cerr << "failed to connect server socket: " << std::strerror(err) << std::endl;
    handlers.erase(ev.data.fd);
    handlers.erase(conn->client);
    connections.remove(ev.data.fd);
    (*number_of_connections)--;
    return;
  }
  conn->server_event |= ev.events;
  if (conn->bytes_in_pipe) {
    //std::cout << "data in pipes before connecting: " << conn->bytes_in_pipe << std::endl;
    write_to(handlers, connections, number_of_connections, conn, conn->server, conn->client, &(conn->server_event), &(conn->client_event));
  }
  handlers[conn->server] = std::bind(&worker::handle_data_transfer, this, std::placeholders::_1);
  handlers[conn->client] = std::bind(&worker::handle_data_transfer, this, std::placeholders::_1);
  //std::cout << conn->server << " and " << conn->client << " connected" << std::endl;
}

void worker::handle_data_transfer(const epoll_event& ev) {
  connection* conn = connections.get(ev.data.fd);
  if (conn == nullptr) {
    std::cerr << "no connection for fd " << ev.data.fd << std::endl;
    handlers.erase(ev.data.fd);
    epoll_del(epoll_fd, ev.data.fd);
    close(ev.data.fd);
    return;
  }
  if (ev.events & EPOLLERR || ev.events & EPOLLPRI) {
    perror(nullptr);
    std::cerr << "epoll error when transfer data" << std::endl;
    handlers.erase(conn->server);
    handlers.erase(conn->client);
    connections.remove(ev.data.fd);
    (*number_of_connections)--;
    return;
  } 
  int peer = conn->get_peer(ev.data.fd);
  if (peer < 0) {
    std::cerr << "failed to get peer, peer does not exists" << std::endl;
    handlers.erase(ev.data.fd);
    handlers.erase(conn->client);
    handlers.erase(conn->server);
    connections.remove(ev.data.fd);
    (*number_of_connections)--;
    return;
  }
  uint32_t *conn_event = conn->get_event(ev.data.fd);
  uint32_t *peer_event = conn->get_event(peer);

  if (ev.events & EPOLLHUP) {
    *conn_event = EPOLLHUP;
    if (*peer_event == EPOLLHUP) {
      handlers.erase(ev.data.fd);
      handlers.erase(peer);
      connections.remove(peer);
      (*number_of_connections)--;
    }
    if ((conn->bytes_in_pipe && peer == conn->des) || !(*peer_event & EPOLLOUT)) {
      return;
    }
    //std::cout << "shutting down write " << peer << std::endl;
    if (shutdown(peer, SHUT_WR) < 0) {
      perror(nullptr);
      std::cerr << "failed to shutdown write " << peer << std::endl;
      handlers.erase(conn->client);
      handlers.erase(conn->server);
      connections.remove(peer);
      (*number_of_connections)--;
      return;
    }
    *peer_event ^= EPOLLOUT;
    return;
  }
  *conn_event |= ev.events;
  read_from(handlers, connections, number_of_connections, conn, ev.data.fd, peer, conn_event, peer_event);

  write_to(handlers, connections, number_of_connections, conn, ev.data.fd, peer, conn_event, peer_event);
}

void worker::handle_preread_client(const epoll_event &ev) {
  connection *conn = connections.get(ev.data.fd);
  if (!conn) {
    std::cerr << "no connection for fd " << ev.data.fd << std::endl;
    handlers.erase(ev.data.fd);
    close(ev.data.fd);
    epoll_del(epoll_fd, ev.data.fd);
    return;   
  }
  if (ev.events & EPOLLERR || ev.events & EPOLLHUP || ev.events & EPOLLPRI) {
    perror(nullptr);
    std::cerr << "epoll error when transfer data" << std::endl;
    handlers.erase(conn->server);
    handlers.erase(conn->client);
    connections.remove(ev.data.fd);
    (*number_of_connections)--;
    return;
  }
  conn->client_event |= ev.events;
  if (conn->bytes_in_pipe) {
    return;
  }

  if (conn->client_event & EPOLLIN) {
    int n = conn->read(ev.data.fd, get_buffer_size(number_of_connections->load()));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        conn->client_event ^= EPOLLIN;
      }
      else {
        perror(nullptr);
        std::cerr << "failed to splice server: " << conn->server << " client: " << conn->client << " fd: " << ev.data.fd << std::endl;
        handlers.erase(conn->client);
        handlers.erase(conn->server);
        connections.remove(ev.data.fd);
        (*number_of_connections)--;
        return;
      }
    }
    //std::cout << "read " << n << " from " << ev.data.fd << std::endl;
    conn->des = conn->server;
  }
}

void worker::on_client_connect(int client_fd, const sockaddr_in &addr) {
  connection conn;
  if (pipe2(conn.pipes, O_DIRECT | O_NONBLOCK) < 0) {
    perror(nullptr);
    std::cerr << "failed to create pipe" << std::endl;
    close(client_fd);
    return;
  }
  conn.client = client_fd;
  conn.server = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  conn.bytes_in_pipe = 0;
  conn.client_event = 0;
  conn.server_event = 0;
  if (conn.server < 0) {
    perror(nullptr);
    std::cerr << "failed to create server socket" << std::endl;
    conn.clean_up(epoll_fd);
  }
  if (connect(conn.server, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    if (errno != EINPROGRESS) {
      perror(nullptr);
      std::cerr << "failed to connect server socket for " << client_fd << std::endl;
      conn.clean_up(epoll_fd);
      return;
    }

    handlers[conn.server] = std::bind(&worker::handle_server_connect, this, std::placeholders::_1);
    handlers[client_fd] = std::bind(&worker::handle_preread_client, this, std::placeholders::_1);
  }
  else {
    handlers[conn.server] = std::bind(&worker::handle_data_transfer, this, std::placeholders::_1);
    handlers[conn.client] = std::bind(&worker::handle_data_transfer, this, std::placeholders::_1);
  }
  connections.add(conn);
  (*number_of_connections)++;
  if (epoll_add(epoll_fd, conn.server, EPOLLOUT | EPOLLRDHUP | EPOLLIN | EPOLLET | EPOLLPRI) < 0 || epoll_add(epoll_fd, client_fd, EPOLLOUT | EPOLLRDHUP | EPOLLIN | EPOLLET | EPOLLPRI) < 0) {
    perror(nullptr);
    std::cerr << "failed to add client and server to epoll" << std::endl;
    connections.remove(client_fd);
    (*number_of_connections)--;
    handlers.erase(conn.client);
    handlers.erase(conn.server);
  }
}

bool valid_timestamp(uint64_t timestamp) {
  return timestamp > (uint64_t) std::chrono::duration_cast<std::chrono::seconds>((std::chrono::system_clock::now() - std::chrono::seconds(5)).time_since_epoch()).count();
}

