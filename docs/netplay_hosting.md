# Hosting the rendezvous server

**Optional.** Connecting by code from the in-game Online panel needs no
server: it uses public STUN servers and public MQTT brokers (see
`docs/netplay_connect_codes.md`). The server below only adds a relay for
the few connections that cannot punch through (symmetric NAT on both
sides). Free options if that is ever needed: an Oracle Cloud "Always Free"
VM or a Google Cloud e2-micro free-tier VM both run it at no cost.

The rendezvous server (`tools/netplay/rendezvous_server.py`) is the only
piece of online play that needs a machine with a public address. It matches
connect codes, tells each player the other's endpoint, and relays a match
only when the two cannot reach each other directly. One Python 3 process,
no dependencies, a few MB of RAM; any small VPS works.

Players enter its `ip:port` once on Settings → Netplay. Giving it a DNS name
(`play.example.com:7777`) means the address can change later without every
player editing it.

## Plain Ubuntu / Debian machine

```bash
# 1. the file
sudo mkdir -p /opt/battleship
sudo curl -fsSL -o /opt/battleship/rendezvous_server.py \
  https://raw.githubusercontent.com/susicadnan/Battleship-Online/main/tools/netplay/rendezvous_server.py

# 2. a service that restarts on failure and at boot
sudo tee /etc/systemd/system/battleship-rendezvous.service >/dev/null <<'EOF'
[Unit]
Description=BattleShip rendezvous (connect-code matchmaking)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/bin/python3 /opt/battleship/rendezvous_server.py --port 7777
Restart=always
RestartSec=2
DynamicUser=yes
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes

[Install]
WantedBy=multi-user.target
EOF
sudo systemctl daemon-reload
sudo systemctl enable --now battleship-rendezvous

# 3. open the port (ufw shown; use the provider's firewall page if it has one)
sudo ufw allow 7777/udp
```

Check it: `sudo journalctl -u battleship-rendezvous -f` shows
`rendezvous listening on udp 0.0.0.0:7777`, then one line per match
(`match ADO#1111 (host) <-> BOB#2222 (client) id=…`) and one per relay. Add
`-v` to `ExecStart` to log every datagram while debugging.

From a player's machine, a quick reachability test without the game:

```bash
printf 'BSR1 HELLO TEST#0001 0' | nc -u -w1 play.example.com 7777
```

prints `BSR1 WELCOME <your public ip:port>`.

## Docker

```bash
docker run -d --name battleship-rendezvous --restart unless-stopped \
  -p 7777:7777/udp -v /opt/battleship:/srv:ro python:3-alpine \
  python3 /srv/rendezvous_server.py --port 7777
```

## Sizing and limits

- Matchmaking traffic is negligible (one small datagram per second per
  waiting player).
- A relayed match costs the server about 10 KB/s per player for its
  duration. There is no cap on concurrent relays yet; a 100 Mbit/s VPS
  carries hundreds before bandwidth matters.
- Everything is in memory; restarting the service drops waiting players
  (they re-register automatically within a second) and any relayed match in
  progress (those players see "connection lost" after 10 s).
- The protocol has no authentication. Codes are first come, first served
  while online; nothing persists.
