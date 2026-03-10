# DEPLOYMENT

## Quick Install

One-line install (latest release):
```bash
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

Specific version:
```bash
VERSION=v1.0.0 curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

## Manual Install

Download binary for your platform from [releases](https://github.com/m1ngsama/TNT/releases):

```bash
# Linux AMD64
wget https://github.com/m1ngsama/TNT/releases/latest/download/tnt-linux-amd64
chmod +x tnt-linux-amd64
sudo mv tnt-linux-amd64 /usr/local/bin/tnt

# macOS ARM64 (Apple Silicon)
wget https://github.com/m1ngsama/TNT/releases/latest/download/tnt-darwin-arm64
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
TNT_PUBLIC_HOST=chat.m1ng.space
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

Recommended interpretation:

- `TNT_MAX_CONNECTIONS`: global connection ceiling
- `TNT_MAX_CONN_PER_IP`: concurrent sessions allowed from one IP
- `TNT_MAX_CONN_RATE_PER_IP`: new connection attempts allowed per IP per 60 seconds
- `TNT_RATE_LIMIT=0`: disables rate-based blocking and auth-failure IP blocking, but not the explicit capacity limits

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

# Re-run install script
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh

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
