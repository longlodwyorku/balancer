FLAGS = -std=c++20 -Wall -Wextra -Werror -pedantic $(NIX_CFLAGS_COMPILE)
CC = g++
OBJ = obj
BIN = bin

$(OBJ)/monitor_server.o: src/monitor_server.cpp headers/endian_convert.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o obj/monitor_server.o src/monitor_server.cpp

$(BIN)/monitor_server: $(OBJ)/monitor_server.o
	mkdir -p bin
	$(CC) $(FLAGS) -lmonitor -o bin/monitor_server $^

$(BIN)/proxy_server: $(OBJ)/worker.o $(OBJ)/proxy_server.o $(OBJ)/connection.o
	mkdir -p bin
	$(CC) $(FLAGS) -lpthread -o bin/proxy_server $^

$(OBJ)/proxy_server.o: src/proxy_server.cpp headers/endian_convert.hpp headers/worker.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o obj/proxy_server.o src/proxy_server.cpp

$(OBJ)/connection.o: src/connection.cpp headers/connection.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o obj/connection.o src/connection.cpp

$(OBJ)/worker.o: src/worker.cpp headers/worker.hpp headers/sync_queue.hpp headers/connection.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o obj/worker.o src/worker.cpp

install_monitor: $(BIN)/monitor_server sh/monitor.sh configs/monitor.conf serv/monitor.service
	mkdir -p $(DESTDIR)/usr/bin/balancer $(DESTDIR)/etc/balancer $(DESTDIR)/usr/lib/systemd/system
	install $< $(DESTDIR)/usr/bin/balancer
	install sh/monitor.sh $(DESTDIR)/usr/bin/balancer
	install configs/monitor.conf $(DESTDIR)/etc/balancer
	install serv/balancer-monitor.service $(DESTDIR)/usr/lib/systemd/system

install_proxy: $(BIN)/proxy_server sh/proxy.sh configs/proxy.conf serv/proxy.service
	mkdir -p $(DESTDIR)/usr/bin/balancer $(DESTDIR)/etc/balancer $(DESTDIR)/usr/lib/systemd/system
	install $< $(DESTDIR)/usr/bin/balancer
	install sh/proxy.sh $(DESTDIR)/usr/bin/balancer
	install configs/proxy.conf $(DESTDIR)/etc/balancer
	install serv/balancer-proxy.service $(DESTDIR)/usr/lib/systemd/system

clean:
	rm -rf obj/* bin/* shared/*
