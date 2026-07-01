# url-Blocker

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
cd url-Blocker

# 2. Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build -- -j$(nproc)
```

The binary is created at `build/url-block`.

---

## Run interactively (with TUI)

```bash
sudo ./build/url-block
```

Optional flags:

```bash
sudo ./build/url-block -b blocklist.txt -u 8.8.8.8:53 -p 15353
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

This installs url-block as a systemd service so it runs automatically on every boot.

### Step 1 — Copy files to /opt/url-block

```bash
sudo mkdir -p /opt/url-block
sudo cp build/url-block /opt/url-block/
sudo cp blocklist.txt /opt/url-block/
```

### Step 2 — Install the service

```bash
sudo cp url-block.service /etc/systemd/system/
sudo systemctl daemon-reload
```

### Step 3 — Enable and start

```bash
sudo systemctl enable url-block   # start on boot
sudo systemctl start url-block    # start now
```

### Step 4 — Verify it's running

```bash
sudo systemctl status url-block
```

You should see `Active: active (running)`.

---

## Managing the service

```bash
sudo systemctl stop url-block      # stop
sudo systemctl restart url-block   # restart
sudo systemctl disable url-block   # don't start on boot
sudo journalctl -u url-block -f    # view live logs
```

## View the live TUI while the service is running

The TUI is only available when running interactively. To use it:

```bash
sudo systemctl stop url-block       # stop the background service
sudo /opt/url-block/url-block       # run with TUI
# Press q to exit, then restart the service:
sudo systemctl start url-block
```

---

## Updating the blocklist

Edit `/opt/url-block/blocklist.txt` and either:
- Restart the service: `sudo systemctl restart url-block`
- Or if running interactively, press `r` to reload without restarting

---

## Running on Windows

The same source builds on Windows too, using `netsh` for DNS redirection instead of `iptables`.

### Requirements

- Visual Studio Build Tools (or Visual Studio) with the "Desktop development with C++" workload
- `cmake`

### Build

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The binary is created at `build\Release\url-block.exe`.

### Run interactively (with TUI)

Right-click `url-block.exe` → **Run as administrator**, or from an elevated PowerShell:

```powershell
.\build\Release\url-block.exe
```

Same flags as Linux (`-b`, `-u`, `-p`, `-h`), except the default port is `53` — Windows points DNS straight at the proxy via `netsh` rather than redirecting with `iptables`. Quit with `q`; this is what triggers DNS cleanup, so always prefer it over closing the window.

### Run as a background service

Windows services must speak a specific control protocol that a plain console app doesn't implement, so this uses [NSSM](https://nssm.cc/) to wrap `url-block.exe` as a real service. All commands below need an elevated (Administrator) PowerShell.

#### Step 1 — Install NSSM

```powershell
choco install nssm -y
```

#### Step 2 — Copy files to a permanent location

```powershell
mkdir "C:\Program Files\url-block"
copy build\Release\url-block.exe "C:\Program Files\url-block\"
copy blocklist.txt "C:\Program Files\url-block\"
```

#### Step 3 — Register the service

```powershell
nssm install url-block "C:\Program Files\url-block\url-block.exe"
nssm set url-block AppDirectory "C:\Program Files\url-block"
nssm set url-block AppParameters "-d"
nssm set url-block DisplayName "url-block DNS Firewall"
nssm set url-block AppStdout "C:\Program Files\url-block\logs\stdout.log"
nssm set url-block AppStderr "C:\Program Files\url-block\logs\stderr.log"
nssm set url-block AppStopMethodConsole 5000
```

The `-d` flag is required — it skips the TUI, which would otherwise fill the log files with raw ANSI redraw output since a service has no visible desktop to draw on (services run in a separate, non-interactive session).

#### Step 4 — Start it

```powershell
Start-Service url-block
```

#### Step 5 — Verify it's running

```powershell
Get-Service url-block
```

You should see `Status: Running`.

### Managing the service

```powershell
Stop-Service url-block                            # stop
Start-Service url-block                           # start
Set-Service url-block -StartupType Automatic       # start on boot
Set-Service url-block -StartupType Manual          # don't start on boot
nssm edit url-block                                # GUI editor for settings
```

### Viewing activity while the service is running

Like the Linux TUI, the live view only renders when run interactively. To check on it:

```powershell
Stop-Service url-block
& "C:\Program Files\url-block\url-block.exe"   # run elevated, with TUI
# Press q to exit, then:
Start-Service url-block
```

### Updating the blocklist

Edit `C:\Program Files\url-block\blocklist.txt`, then either restart the service or, if running interactively, press `r` to reload without restarting.

> **Note:** after rebuilding, re-copy `url-block.exe` to `C:\Program Files\url-block\`. The service runs from that copy, not from `build\Release\` directly, so a rebuild alone won't update it.
