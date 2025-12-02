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
sudo mkdir -p /var/lib/tnt
sudo chown tnt:tnt /var/lib/tnt
```

2. Install service file:
```bash
sudo cp tnt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable tnt
sudo systemctl start tnt
```

3. Check status:
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
