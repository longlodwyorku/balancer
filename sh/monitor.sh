#! /bin/sh

read -a args -d EOF < /etc/balancer/monitor.conf
$(dirname $(realpath $0))/monitor_server ${args[@]}
