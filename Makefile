FLAGS = -std=c++20 -Wall -Wextra -Werror -pedantic $(NIX_CFLAGS_COMPILE)
CC = g++
OBJ = obj
BIN = bin

$(OBJ)/balancer-monitor.o: src/balancer-monitor.cpp headers/endian_convert.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o obj/monitor.o src/monitor.cpp

$(BIN)/balancer-monitor: $(OBJ)/balancer-monitor.o
	mkdir -p bin
	$(CC) $(FLAGS) -lmonitor -o bin/monitor $^

$(BIN)/balancer-proxy: $(OBJ)/worker.o $(OBJ)/balancer-proxy.o $(OBJ)/connection.o
	mkdir -p bin
	$(CC) $(FLAGS) -lpthread -o bin/proxy $^

$(OBJ)/balancer-proxy.o: src/balancer-proxy.cpp headers/endian_convert.hpp headers/worker.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o obj/proxy.o src/proxy.cpp

$(OBJ)/connection.o: src/connection.cpp headers/connection.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o obj/connection.o src/connection.cpp

$(OBJ)/worker.o: src/worker.cpp headers/worker.hpp headers/sync_queue.hpp headers/connection.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o obj/worker.o src/worker.cpp

install_monitor: $(BIN)/monitor sh/balancer-monitor.sh configs/monitor.conf serv/balancer-monitor.service
	mkdir -p $(DESTDIR)/usr/bin/ $(DESTDIR)/etc/balancer $(DESTDIR)/usr/lib/systemd/system
	install $< $(DESTDIR)/usr/bin/
	install sh/balancer-monitor.sh $(DESTDIR)/usr/bin/
	install configs/monitor.conf $(DESTDIR)/etc/balancer
	install serv/balancer-monitor.service $(DESTDIR)/usr/lib/systemd/system

install_proxy: $(BIN)/proxy sh/balancer-proxy.sh configs/proxy.conf serv/balancer-proxy.service
	mkdir -p $(DESTDIR)/usr/bin/ $(DESTDIR)/etc/balancer $(DESTDIR)/usr/lib/systemd/system
	install $< $(DESTDIR)/usr/bin/
	install sh/balancer-proxy.sh $(DESTDIR)/usr/bin/
	install configs/proxy.conf $(DESTDIR)/etc/balancer
	install serv/balancer-proxy.service $(DESTDIR)/usr/lib/systemd/system

install: install_proxy install_monitor

clean:
	rm -rf obj/* bin/* shared/*
