#! /bin/sh

read -a args -d EOF < /etc/balancer/proxy.conf
$(dirname $(realpath $0))/balancer-proxy ${args[@]}
