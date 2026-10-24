/**
 * signal.h — find a friend by connect code with nothing of our own to host.
 *
 * Two pieces of free public infrastructure stand in for a rendezvous server:
 *
 *   STUN (stun.l.google.com and friends): a Binding Request sent from the
 *   game's UDP socket comes back with the public ip:port the router mapped
 *   that socket to. That endpoint, plus the LAN address, is what a friend
 *   needs to punch through to us.
 *
 *   Public MQTT brokers (broker.emqx.io, broker.hivemq.com,
 *   test.mosquitto.org): a mailbox per connect code. We subscribe to the
 *   topic for our own code; a friend publishes their endpoints to it, we
 *   publish ours back to theirs. Topics are hashes of the code and payloads
 *   are encrypted with a key derived from the recipient's code, so someone
 *   watching a broker sees neither codes nor addresses unless they already
 *   know the code. Every broker that is up gets every message; duplicates
 *   are dropped, so one broker being down changes nothing.
 *
 * After that the two games talk directly (port/netplay/lobby.cpp punches the
 * NATs); nothing here carries game traffic. See docs/netplay_connect_codes.md.
 *
 * Settings: gNetplay.Brokers and gNetplay.Stun, comma-separated host:port
 * lists (defaults below).
 */
#pragma once

#include "netsock.h"

#include <cstdint>
#include <string>
#include <vector>

/* Start (or keep) the broker connections and listen on my_code's mailbox.
 * Changing the code re-subscribes. Cheap to call every frame. */
void port_signal_open(const std::string &my_code);
void port_signal_close();
bool port_signal_is_open();

/* Once per frame while open: connects, reconnects, pings, reads. */
void port_signal_tick();

/* Brokers connected and subscribed right now / configured. */
int port_signal_brokers_up();
int port_signal_brokers_total();

/* Encrypt text for to_code's mailbox and publish it on every broker that is up. */
void port_signal_send(const std::string &to_code, const std::string &text);

/* Next message that arrived in our mailbox, decrypted and de-duplicated. */
bool port_signal_receive(std::string *text);

/* STUN. Servers resolve in the background from the first call on. */
void port_stun_send(intptr_t sock);
/* A Binding Success Response for one of our requests: *mapped gets our
 * public endpoint. Anything else returns false (pass it on). */
bool port_stun_parse(const uint8_t *buf, int len, PortNetAddr *mapped);

/* Addresses a peer can try for us besides the public one: the LAN address
 * of the default route, and loopback (two copies on one machine). */
std::vector<PortNetAddr> port_signal_local_addrs(uint16_t port);
