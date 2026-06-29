# DEPLOYMENT

## Quick Install

Pinned release install:
```bash
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/v1.2.0/install.sh | VERSION=v1.2.0 sh
```

Moving latest-release installer, convenient for test deployments:
```bash
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

## Manual Install

Download binary for your platform from [releases](https://github.com/m1ngsama/TNT/releases):

```bash
# Linux AMD64
curl -LO https://github.com/m1ngsama/TNT/releases/latest/download/tnt-linux-amd64
chmod +x tnt-linux-amd64
sudo mv tnt-linux-amd64 /usr/local/bin/tnt

# macOS ARM64 (Apple Silicon)
curl -LO https://github.com/m1ngsama/TNT/releases/latest/download/tnt-darwin-arm64
chmod +x tnt-darwin-arm64
sudo mv tnt-darwin-arm64 /usr/local/bin/tnt
```

## systemd Service

1. Create user and directory:
```bash
sudo useradd -r -s /bin/false tnt
```

2. Install service file:
```bash
sudo cp tnt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable tnt
sudo systemctl start tnt
```

3. Optional runtime overrides:
```bash
sudo tee /etc/default/tnt >/dev/null <<'EOF'
PORT=2222
TNT_BIND_ADDR=0.0.0.0
TNT_STATE_DIR=/var/lib/tnt
TNT_MAX_CONNECTIONS=200
TNT_MAX_CONN_PER_IP=30
TNT_MAX_CONN_RATE_PER_IP=60
TNT_RATE_LIMIT=1
TNT_SSH_LOG_LEVEL=0
TNT_PUBLIC_HOST=chat.example.com
EOF

sudo systemctl restart tnt
```

4. Check status:
```bash
sudo systemctl status tnt
sudo journalctl -u tnt -f
```

## Configuration

Environment variables:
```bash
# Change port
sudo systemctl edit tnt
# Add:
[Service]
Environment="PORT=3333"

sudo systemctl restart tnt
```

The service uses `StateDirectory=tnt`, so systemd creates `/var/lib/tnt` automatically.
Use `TNT_STATE_DIR` or `tnt -d DIR` when running outside systemd to avoid depending on the current working directory.

To generate a reviewable environment file with an interactive terminal setup
wizard:

```bash
scripts/install_wizard.sh --output tnt.env
sudo install -m 600 tnt.env /etc/default/tnt
sudo systemctl restart tnt
```

The wizard can choose a core-only deployment, scan a module root, select
individual modules, or validate a manually entered module path list. It never
downloads modules, edits systemd units, or restarts TNT by itself. See
`docs/INSTALL_LIFECYCLE.md` for the full operator lifecycle.

Recommended interpretation:

- `TNT_MAX_CONNECTIONS`: global connection ceiling
- `TNT_MAX_CONN_PER_IP`: concurrent sessions allowed from one IP
- `TNT_MAX_CONN_RATE_PER_IP`: new connection attempts allowed per IP per 60 seconds
- `TNT_RATE_LIMIT=0`: disables rate-based blocking and auth-failure IP blocking, but not the explicit capacity limits

## Edge Module Production Profile

Some deployments intentionally track the newest TNT builds and newest module
integrations to exercise the full product surface. Treat these as edge
production environments: user-facing, but optimized for fast integration and
fast rollback.

For that profile:

- Deploy TNT and modules as separate artifacts so a module can be disabled
  without replacing the core server.
- Keep module permissions explicit and minimal. Do not grant private-message
  access unless the module exists for that purpose.
- Keep a known-good TNT binary and module manifest set on disk for immediate
  rollback.
- Log module startup failures, invalid JSONL, protocol errors, and timeouts
  separately from chat history.
- Prefer plain-text fallbacks for every module-created message, even when the
  module also targets richer terminal renderers.
- Before promoting a module, test its manifest and JSONL handshake against the
  protocol in `docs/MODULE_PROTOCOL.md` with `scripts/module_check.sh`.
- Develop and publish community modules in the public companion repository:
  `https://github.com/m1ngsama/tnt-modules`.

Enable modules explicitly with `TNT_MODULE_PATHS`, using a colon-separated
list of module directories:

```bash
TNT_MODULE_PATHS=/opt/tnt-modules/echo-module:/opt/tnt-modules/other-module
```

Unset `TNT_MODULE_PATHS` and restart TNT to return to the plain core server.

## MOTD (Message of the Day)

Place a `motd.txt` file in the state directory. TNT displays it to each user on connect; they press any key to enter the chat.

```bash
# Systemd deployment (state dir is /var/lib/tnt)
sudo tee /var/lib/tnt/motd.txt <<'EOF'
Welcome! Be respectful. No spam.
Type :help for a concise manual, or ? for the full key reference.
EOF
sudo chown tnt:tnt /var/lib/tnt/motd.txt

# Remove to disable
sudo rm /var/lib/tnt/motd.txt
```

No restart required — TNT reads the file on each new connection.

## Manual Log Maintenance

TNT stores public chat history in `messages.log` under the state directory.
Use the maintenance script from a source checkout when the service is stopped
or during a quiet maintenance window:

```bash
sudo systemctl stop tnt
sudo scripts/logrotate.sh /var/lib/tnt/messages.log 100 10000
sudo systemctl start tnt
```

The arguments are `LOG_FILE MAX_SIZE_MB KEEP_LINES`.  The script archives the
full log, compacts the active log to the last `KEEP_LINES` records, compresses
the archive when `gzip` is available, and keeps the newest five archives by
default.  Use `--dry-run` to preview actions, or `--keep-archives N` to change
archive retention.

Before replacing a suspicious log, inspect and recover it offline:

```bash
tnt --log-check /var/lib/tnt/messages.log
tnt --log-recover /var/lib/tnt/messages.log > /var/lib/tnt/messages.recovered.log
```

`--log-recover` writes valid records to stdout and reports skipped records to
stderr.  Review the recovered file before replacing the active log.

## Firewall

```bash
# Ubuntu/Debian
sudo ufw allow 2222/tcp

# CentOS/RHEL
sudo firewall-cmd --permanent --add-port=2222/tcp
sudo firewall-cmd --reload
```

## Update

```bash
# Stop service
sudo systemctl stop tnt

# Re-run the pinned installer for the version you want
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/v1.2.0/install.sh | VERSION=v1.2.0 sh

# Start service
sudo systemctl start tnt
```

## Uninstall

```bash
sudo systemctl stop tnt
sudo systemctl disable tnt
sudo rm /etc/systemd/system/tnt.service
sudo systemctl daemon-reload
sudo rm /usr/local/bin/tnt
sudo userdel tnt
sudo rm -rf /var/lib/tnt
```

## Docker (Alternative)

```dockerfile
FROM alpine:latest
RUN apk add --no-cache libssh
COPY tnt /usr/local/bin/tnt
EXPOSE 2222
CMD ["/usr/local/bin/tnt"]
```

Build and run:
```bash
docker build -t tnt .
docker run -d -p 2222:2222 --name tnt tnt
```
