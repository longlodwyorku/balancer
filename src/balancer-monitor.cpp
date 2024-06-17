#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <endian.h>
#include <iostream>
#include <netinet/in.h>
#include <ostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <monitor.hpp>
#include <cstdlib>
#include <chrono>
#include <unistd.h>
#include "../headers/endian_convert.hpp"
#include "../headers/defer.hpp"

static constexpr std::size_t MES_SIZE = 1 + sizeof(std::size_t) + sizeof(float) * 2;

int main (int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "not enough argument" << std::endl << "monitor_server <broadcast ip> <broadcast port>" << std::endl;
    return 1;
  }
  int broadcast_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (broadcast_socket < 0) {
    std::cerr << "failed to create socket" << std::endl;
    return 1;
  }
  defer(close(broadcast_socket));

  int broadcast = 1;
  if (setsockopt(broadcast_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
    std::cerr << "failed to set broadcast socket" << std::endl;
    return 1;
  }

  sockaddr_in broadcast_address;
  broadcast_address.sin_family = AF_INET;
  broadcast_address.sin_addr.s_addr = inet_addr(argv[1]);
  broadcast_address.sin_port = htons(atoi(argv[2]));

  if (connect(broadcast_socket, reinterpret_cast<sockaddr *>(&broadcast_address), sizeof(broadcast_address)) < 0) {
    std::cerr << "failed to connect to broadcast address" << std::endl;
    return 1;
  }
  std::pair<uint64_t, uint64_t> cpu0;
  if (monitor::sample_cpu_usage(cpu0) != 0) {
    perror(nullptr);
    std::cerr << "failed to get cpu sample" << std::endl;
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));

  while (true) {
    auto current_time = std::chrono::system_clock::now();
    uint64_t current_time_uint64 = std::chrono::duration_cast<std::chrono::seconds>(current_time.time_since_epoch()).count();

    std::pair<uint64_t, uint64_t> cpu1;
    if (monitor::sample_cpu_usage(cpu1) != 0) {
      perror(nullptr);
      std::cerr << "failed to get cpu sample" << std::endl;
      continue;
    }
    uint64_t cpu_total = cpu1.second - cpu0.second;

    float cpu_usage = cpu_total == 0 ? 0 : (((double)(cpu1.first - cpu0.first)) / cpu_total);
    float mem_usage = monitor::get_memory_usage();
    unsigned char buf[MES_SIZE];
    buf[0] = 0;
    *reinterpret_cast<uint64_t*>((buf + 1)) = endian_convert::hton(current_time_uint64);
    *reinterpret_cast<float*>((buf + 1 + sizeof(uint64_t))) = endian_convert::hton(cpu_usage);
    *reinterpret_cast<float*>((buf + 1 + sizeof(uint64_t) + sizeof(float))) = endian_convert::hton(mem_usage);

    if (send(broadcast_socket, buf, MES_SIZE, 0) != MES_SIZE) {
      perror(nullptr);
      std::cerr << "failed to sendto" << std::endl;
    }
    cpu0 = cpu1;
    std::cout << current_time_uint64 << "\t" << cpu_usage << "\t" << mem_usage << std::endl;
    std::this_thread::sleep_until(current_time + std::chrono::seconds(1));
  }
  return 0;
}
