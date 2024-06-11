#! /bin/sh

read -a args -d EOF < /etc/balancer/proxy.conf
$(dirname $(realpath $0))/proxy_server ${args[@]}
