#!/bin/sh
# Set up a host bridge, VMEmu TAP, and IPv4 NAT for the RVM guest.
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo 'Run this script with sudo; set RVM_TAP_USER to the VMEmu user.' >&2
    exit 1
fi

bridge=${RVM_BRIDGE:-br-rvm}
tap=${RVM_TAP:-tap-rvm}
tap_user=${RVM_TAP_USER:-${SUDO_USER:-}}
host_cidr=${RVM_HOST_CIDR:-192.168.76.1/24}
guest_subnet=${RVM_GUEST_SUBNET:-192.168.76.0/24}
uplink=${RVM_UPLINK:-$(ip -4 route show default | awk 'NR == 1 { for (i = 1; i < NF; i++) if ($i == "dev") { print $(i + 1); exit } }')}

if [ -z "$tap_user" ] || [ -z "$uplink" ]; then
    echo 'Set RVM_TAP_USER and RVM_UPLINK before running this script.' >&2
    exit 1
fi
case "$bridge:$tap:$uplink" in
    *[!a-zA-Z0-9_.:-]*) echo 'Invalid network interface name.' >&2; exit 1;;
esac

modprobe tun
if ! ip link show dev "$bridge" >/dev/null 2>&1; then
    ip link add "$bridge" type bridge
fi
if ! ip -4 addr show dev "$bridge" | grep -Fq "inet $host_cidr "; then
    ip addr add "$host_cidr" dev "$bridge"
fi
ip link set "$bridge" up
if ! ip link show dev "$tap" >/dev/null 2>&1; then
    ip tuntap add dev "$tap" mode tap user "$tap_user"
fi
ip link set "$tap" master "$bridge"
ip link set "$tap" up

sysctl -qw net.ipv4.ip_forward=1
if ! iptables -t nat -C POSTROUTING -s "$guest_subnet" -o "$uplink" -j MASQUERADE >/dev/null 2>&1; then
    iptables -t nat -A POSTROUTING -s "$guest_subnet" -o "$uplink" -j MASQUERADE
fi
if ! iptables -C FORWARD -i "$bridge" -o "$uplink" -s "$guest_subnet" -j ACCEPT >/dev/null 2>&1; then
    iptables -A FORWARD -i "$bridge" -o "$uplink" -s "$guest_subnet" -j ACCEPT
fi
if ! iptables -C FORWARD -i "$uplink" -o "$bridge" -d "$guest_subnet" -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT >/dev/null 2>&1; then
    iptables -A FORWARD -i "$uplink" -o "$bridge" -d "$guest_subnet" -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
fi

echo "$tap ready for $tap_user on $bridge ($host_cidr); NAT via $uplink"
