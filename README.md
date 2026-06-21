# WebBlock

A lightweight DNS firewall for Linux. Intercepts DNS queries and blocks domains from a blocklist, returning NXDOMAIN for blocked sites. Includes a live terminal UI showing blocked/allowed queries in real time.

## How it works

```
Browser → DNS query (port 53)
       → iptables redirects to local proxy (port 15353)
       → Blocklist check
         ├─ blocked → NXDOMAIN (site fails to load)
         └─ allowed → forwarded to upstream DNS → response returned
```

Blocking works at the DNS layer — any app that uses standard DNS resolution is affected (browsers, apps, etc.). Apps using hardcoded IPs bypass it.

---

## Requirements

- Linux (tested on Ubuntu / Linux Mint)
- `g++` with C++17 support
- `cmake`
- `iptables`

Install dependencies:

```bash
sudo apt install build-essential cmake iptables
```

---

## Build

```bash
# 1. Clone or download the project
cd WebBlockDamon

# 2. Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build -- -j$(nproc)
```

The binary is created at `build/webblock`.

---

## Run interactively (with TUI)

```bash
sudo ./build/webblock
```

Optional flags:

```bash
sudo ./build/webblock -b blocklist.txt -u 8.8.8.8:53 -p 15353
```

| Flag | Default | Description |
|------|---------|-------------|
| `-b FILE` | `blocklist.txt` | Path to blocklist file |
| `-u ADDR` | `8.8.8.8:53` | Upstream DNS server |
| `-p PORT` | `15353` | Local proxy port |

**Keyboard shortcuts in the TUI:**

| Key | Action |
|-----|--------|
| `q` | Quit |
| `c` | Clear stats |
| `r` | Reload blocklist (without restarting) |

---

## Blocklist format

Plain text, one domain per line. Lines starting with `#` are comments. Blocking a parent domain automatically blocks all subdomains.

```
# Block all of Facebook
facebook.com

# Block Google ads
doubleclick.net
googleadservices.com

# Your own additions
example-tracker.com
```

A default `blocklist.txt` is included with ad networks, trackers, social media pixels, and Microsoft telemetry pre-populated.

---

## Run as a background service (start on boot)

This installs WebBlock as a systemd service so it runs automatically on every boot.

### Step 1 — Copy files to /opt/webblock

```bash
sudo mkdir -p /opt/webblock
sudo cp build/webblock /opt/webblock/
sudo cp blocklist.txt /opt/webblock/
```

### Step 2 — Install the service

```bash
sudo cp webblock.service /etc/systemd/system/
sudo systemctl daemon-reload
```

### Step 3 — Enable and start

```bash
sudo systemctl enable webblock   # start on boot
sudo systemctl start webblock    # start now
```

### Step 4 — Verify it's running

```bash
sudo systemctl status webblock
```

You should see `Active: active (running)`.

---

## Managing the service

```bash
sudo systemctl stop webblock      # stop
sudo systemctl restart webblock   # restart
sudo systemctl disable webblock   # don't start on boot
sudo journalctl -u webblock -f    # view live logs
```

## View the live TUI while the service is running

The TUI is only available when running interactively. To use it:

```bash
sudo systemctl stop webblock      # stop the background service
sudo /opt/webblock/webblock       # run with TUI
# Press q to exit, then restart the service:
sudo systemctl start webblock
```

---

## Updating the blocklist

Edit `/opt/webblock/blocklist.txt` and either:
- Restart the service: `sudo systemctl restart webblock`
- Or if running interactively, press `r` to reload without restarting
