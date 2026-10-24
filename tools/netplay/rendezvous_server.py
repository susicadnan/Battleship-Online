#!/usr/bin/env python3
"""BattleShip rendezvous server: connect codes -> UDP hole punching.

Players register their connect code (NAME#1234) and keep the registration
alive; when two registered players name each other, both are told the
other's public endpoint and punch through their NATs directly. The server
never relays game traffic. One process, one UDP port, no dependencies.

    python3 rendezvous_server.py --port 7777

Protocol (UTF-8 text datagrams, fields separated by single spaces, every
line starts with the magic BSR1):

  client -> server
    BSR1 HELLO <code> <fkind>             register / keepalive (send ~1/s)
    BSR1 JOIN <code> <friend_code>        I want to play <friend_code>
    BSR1 LEAVE <code>
    BSR1 RELAY <match_id>                 punching failed; please relay
  server -> client
    BSR1 WELCOME <public_ip:port>         answer to HELLO
    BSR1 WAITING <friend_code>            JOIN seen, friend not (yet) mutual
    BSR1 PEER <role> <ip:port> <match_id> <peer_fkind> <peer_code>
                                          both named each other; role is
                                          host or client (host = lower code)
    BSR1 RELAYED <match_id>               sent to both: from now on send the
                                          game packets to the server, it
                                          forwards them to the other side
    BSR1 ERROR <text>

Relay fallback: when either side asks (symmetric NAT, CGNAT), the server
forwards every non-BSR1 datagram between the two addresses of that match for
as long as both keep sending (idle 20 s ends it). Costs the server the
match's bandwidth (~10 KB/s per player), so it is only used when direct
fails.

A registration expires 10 s after its last HELLO. Codes are case-insensitive
and must match [A-Z0-9]{1,12}#[0-9]{4}.
"""
import argparse
import asyncio
import re
import secrets
import time

MAGIC = "BSR1"
CODE_RE = re.compile(r"^[A-Z0-9]{1,12}#[0-9]{4}$")
EXPIRE_SECONDS = 10.0
SWEEP_SECONDS = 2.0
RELAY_IDLE_SECONDS = 20.0
RELAY_PENDING_SECONDS = 60.0


class Relay:
    """A match being forwarded: addr_a <-> addr_b."""
    __slots__ = ("match_id", "addrs", "last_seen")

    def __init__(self, match_id, addrs):
        self.match_id = match_id
        self.addrs = addrs  # [addr_a, addr_b]
        self.last_seen = time.monotonic()


class Registration:
    __slots__ = ("code", "addr", "fkind", "last_seen", "wants")

    def __init__(self, code, addr, fkind):
        self.code = code
        self.addr = addr
        self.fkind = fkind
        self.last_seen = time.monotonic()
        self.wants = None  # friend code this player asked to join


class Rendezvous(asyncio.DatagramProtocol):
    def __init__(self, verbose):
        self.transport = None
        self.players = {}  # code -> Registration
        self.pending = {}  # match_id -> (addr_host, addr_client, created) until a RELAY arrives
        self.relays = {}   # addr -> Relay (both addresses of a relayed match)
        self.verbose = verbose

    def connection_made(self, transport):
        self.transport = transport

    # -- helpers ---------------------------------------------------------
    def send(self, addr, *fields):
        line = " ".join([MAGIC, *map(str, fields)])
        self.transport.sendto(line.encode("utf-8"), addr)
        if self.verbose:
            print(f"-> {addr[0]}:{addr[1]}  {line}", flush=True)

    @staticmethod
    def fmt(addr):
        return f"{addr[0]}:{addr[1]}"

    def sweep(self):
        now = time.monotonic()
        dead = [c for c, r in self.players.items() if now - r.last_seen > EXPIRE_SECONDS]
        for c in dead:
            if self.verbose:
                print(f"expire {c}", flush=True)
            del self.players[c]
        for mid in [m for m, (_, _, t) in self.pending.items() if now - t > RELAY_PENDING_SECONDS]:
            del self.pending[mid]
        for addr in [a for a, r in self.relays.items() if now - r.last_seen > RELAY_IDLE_SECONDS]:
            relay = self.relays.pop(addr, None)
            if relay is not None and self.verbose:
                print(f"relay {relay.match_id} idle, dropped", flush=True)

    def try_match(self, a):
        """If a and the player a wants both want each other, pair them."""
        if a.wants is None:
            return
        b = self.players.get(a.wants)
        if b is None or b.wants != a.code:
            return
        host, client = (a, b) if a.code < b.code else (b, a)
        match_id = secrets.token_hex(4)
        self.send(host.addr, "PEER", "host", self.fmt(client.addr), match_id, client.fkind, client.code)
        self.send(client.addr, "PEER", "client", self.fmt(host.addr), match_id, host.fkind, host.code)
        if self.verbose:
            print(f"match {host.code} (host) <-> {client.code} (client) id={match_id}", flush=True)
        self.pending[match_id] = (host.addr, client.addr, time.monotonic())
        # Both leave the lobby: a rematch registers again.
        del self.players[a.code]
        del self.players[b.code]

    def start_relay(self, match_id):
        entry = self.pending.pop(match_id, None)
        if entry is None:
            return False
        addr_a, addr_b, _ = entry
        relay = Relay(match_id, [addr_a, addr_b])
        self.relays[addr_a] = relay
        self.relays[addr_b] = relay
        self.send(addr_a, "RELAYED", match_id)
        self.send(addr_b, "RELAYED", match_id)
        if self.verbose:
            print(f"relay {match_id}: {self.fmt(addr_a)} <-> {self.fmt(addr_b)}", flush=True)
        return True

    # -- protocol --------------------------------------------------------
    def datagram_received(self, data, addr):
        if not data.startswith(MAGIC.encode()):
            # Game traffic of a relayed match: forward to the other side.
            relay = self.relays.get(addr)
            if relay is not None:
                relay.last_seen = time.monotonic()
                other = relay.addrs[1] if relay.addrs[0] == addr else relay.addrs[0]
                self.transport.sendto(data, other)
            return
        try:
            text = data.decode("utf-8", "strict").strip()
        except UnicodeDecodeError:
            return
        fields = text.split(" ")
        if len(fields) < 2 or fields[0] != MAGIC:
            return
        if self.verbose:
            print(f"<- {addr[0]}:{addr[1]}  {text}", flush=True)
        cmd = fields[1]
        args = fields[2:]

        if cmd == "HELLO" and len(args) >= 1:
            code = args[0].upper()
            fkind = args[1] if len(args) > 1 else "0"
            if not CODE_RE.match(code):
                self.send(addr, "ERROR", "bad code")
                return
            reg = self.players.get(code)
            if reg is not None and reg.addr != addr and time.monotonic() - reg.last_seen < EXPIRE_SECONDS:
                self.send(addr, "ERROR", "code in use")
                return
            if reg is None:
                reg = Registration(code, addr, fkind)
                self.players[code] = reg
            reg.addr = addr
            reg.fkind = fkind
            reg.last_seen = time.monotonic()
            self.send(addr, "WELCOME", self.fmt(addr))
            self.try_match(reg)

        elif cmd == "JOIN" and len(args) >= 2:
            code = args[0].upper()
            friend = args[1].upper()
            reg = self.players.get(code)
            if reg is None or reg.addr != addr:
                self.send(addr, "ERROR", "not registered")
                return
            if not CODE_RE.match(friend) or friend == code:
                self.send(addr, "ERROR", "bad friend code")
                return
            reg.last_seen = time.monotonic()
            reg.wants = friend
            self.send(addr, "WAITING", friend)
            self.try_match(reg)

        elif cmd == "LEAVE" and len(args) >= 1:
            code = args[0].upper()
            reg = self.players.get(code)
            if reg is not None and reg.addr == addr:
                del self.players[code]

        elif cmd == "RELAY" and len(args) >= 1:
            match_id = args[0]
            if addr in self.relays and self.relays[addr].match_id == match_id:
                self.send(addr, "RELAYED", match_id)  # already relaying; repeat the answer
            elif not self.start_relay(match_id):
                self.send(addr, "ERROR", "unknown match")


async def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    loop = asyncio.get_running_loop()
    transport, protocol = await loop.create_datagram_endpoint(
        lambda: Rendezvous(args.verbose), local_addr=(args.bind, args.port)
    )
    print(f"rendezvous listening on udp {args.bind}:{args.port}", flush=True)
    try:
        while True:
            await asyncio.sleep(SWEEP_SECONDS)
            protocol.sweep()
    finally:
        transport.close()


if __name__ == "__main__":
    asyncio.run(main())
