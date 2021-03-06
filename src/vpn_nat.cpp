#include "vpn_nat.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

namespace vpn {

NAT::NAT() : _nat(-1), _in_use(-1) {
    init();
}

void NAT::init() {
    _nat.prev = _nat.next = &_nat;
    _in_use.prev = _in_use.next = &_in_use;

    FILE *fp = fopen("/proc/sys/net/ipv4/ip_local_port_range", "r");
    assert(fp);

    int s, e;
    assert(fscanf(fp, "%d%d", &s, &e) == 2);

    for (int i = s; i <= e; ++i) {
        append(&_nat, new NATNode(i));
    }
}

int NAT::snat(const std::string& addr, int port, struct sockaddr_in sock) {
    if (empty(&_nat)) {
        prune(75000);
    }
    assert(!empty(&_nat));

    NATNode *node = lookup(addr, port);
    if (node == nullptr) {
        node = _nat.next;
        node->addr = addr;
        node->port = port;

        remove(node);
        append(&_in_use, node);
    }
    node->use = time(nullptr);
    node->sock = sock;
    return node->new_port;
}

std::shared_ptr<OriginData> NAT::dnat(int port) {
    NATNode *node = lookup(port);
    if (node == nullptr) {
        return nullptr;
    }
    return std::make_shared<OriginData>(OriginData{node->sock, node->addr, node->port});
}

void NAT::snat(const std::string& saddr, const std::string& daddr, struct sockaddr_in sock) {
    _addrmap[daddr] = OriginData{sock, saddr, 0};
}

std::shared_ptr<OriginData> NAT::dnat(const std::string& daddr) {
    auto it = _addrmap.find(daddr);
    if (it == _addrmap.end()) {
        return nullptr;
    }
    return std::make_shared<OriginData>(it->second);
}

void NAT::remove(NATNode *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void NAT::append(NATNode *list, NATNode *node) {
    node->next = list;
    node->prev = list->prev;
    node->next->prev = node;
    node->prev->next = node;
}

NATNode* NAT::lookup(int port) {
    NATNode *node = _in_use.next;
    while (node != &_in_use) {
        if (node->new_port == port) {
            return node;
        }
        node = node->next;
    }
    return nullptr;
}

NATNode* NAT::lookup(const std::string& addr, int port) {
    NATNode *node = _in_use.next;
    while (node != &_in_use) {
        if (node->addr == addr && node->port == port) {
            return node;
        }
        node = node->next;
    }
    return nullptr;
}

NAT::~NAT() {
    prune(0);
    assert(empty(&_in_use));

    NATNode *node = _nat.next;
    while (node != &_nat) {
        NATNode *next = node->next;
        delete node;
        node = next;
    }
}

void NAT::prune(int timeout) {
    time_t now = time(nullptr);
    NATNode *node = _in_use.next;
    while (node != &_in_use) {
        NATNode *next = node->next;
        if (now - node->use >= timeout) {
            remove(node);
            append(&_nat, node);
        }
        node = next;
    }
}

bool NAT::empty(const NATNode *list) {
    return list->next == list->prev;
}

} /* namespace vpn */
