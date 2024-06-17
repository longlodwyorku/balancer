FLAGS = -std=c++20 -Wall -Wextra -Werror -pedantic $(NIX_CFLAGS_COMPILE)
CC = g++
OBJ = obj
BIN = bin

$(OBJ)/balancer-monitor.o: src/balancer-monitor.cpp headers/endian_convert.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o $@ $<

$(BIN)/balancer-monitor: $(OBJ)/balancer-monitor.o
	mkdir -p bin
	$(CC) $(FLAGS) -lmonitor -o $@ $^

$(BIN)/balancer-proxy: $(OBJ)/worker.o $(OBJ)/balancer-proxy.o $(OBJ)/connection.o
	mkdir -p bin
	$(CC) $(FLAGS) -lpthread -o $@ $^

$(OBJ)/balancer-proxy.o: src/balancer-proxy.cpp headers/endian_convert.hpp headers/worker.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o $@ $<

$(OBJ)/connection.o: src/connection.cpp headers/connection.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o $@ $<

$(OBJ)/worker.o: src/worker.cpp headers/worker.hpp headers/connection.hpp
	mkdir -p obj
	$(CC) $(FLAGS) -c -o $@ $<

install_balancer-monitor.sh: sh/balancer-monitor.sh
	mkdir -p $(DESTDIR)/usr/bin
	install $< $(DESTDIR)/usr/bin/

install_monitor.conf: configs/monitor.conf
	mkdir -p $(DESTDIR)/etc/balancer
	install $< $(DESTDIR)/etc/balancer

install_balancer-monitor.service: serv/balancer-monitor.service 
	mkdir -p $(DESTDIR)/usr/lib/systemd/system
	install $< $(DESTDIR)/usr/lib/systemd/system

install_balancer-monitor: $(BIN)/balancer-monitor
	mkdir -p $(DESTDIR)/usr/bin/
	install $< $(DESTDIR)/usr/bin/

install_balancer-proxy.sh: sh/balancer-proxy.sh
	mkdir -p $(DESTDIR)/usr/bin
	install $< $(DESTDIR)/usr/bin/

install_proxy.conf: configs/proxy.conf
	mkdir -p $(DESTDIR)/etc/balancer
	install $< $(DESTDIR)/etc/balancer

install_balancer-proxy.service: serv/balancer-proxy.service 
	mkdir -p $(DESTDIR)/usr/lib/systemd/system
	install $< $(DESTDIR)/usr/lib/systemd/system

install_balancer-proxy: $(BIN)/balancer-proxy
	mkdir -p $(DESTDIR)/usr/bin/
	install $< $(DESTDIR)/usr/bin/

install: install_balancer-proxy install_balancer-proxy.service install_proxy.conf install_balancer-proxy.sh install_balancer-monitor install_balancer-monitor.service install_monitor.conf install_balancer-monitor.sh

clean:
	rm -rf obj/* bin/* shared/*
