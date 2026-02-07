# Security Quick Reference

## Quick Start

### Default (Open Access)
```bash
./tnt
# No password required
# Anyone can connect
```

### Protected Mode
```bash
TNT_ACCESS_TOKEN="YourSecretPassword" ./tnt
```
Connect: `sshpass -p "YourSecretPassword" ssh -p 2222 localhost`

---

## Environment Variables

| Variable | Default | Description | Example |
|----------|---------|-------------|---------|
| `TNT_ACCESS_TOKEN` | (none) | Require password | `TNT_ACCESS_TOKEN="secret"` |
| `TNT_BIND_ADDR` | `0.0.0.0` | Bind address | `TNT_BIND_ADDR="127.0.0.1"` |
| `TNT_SSH_LOG_LEVEL` | `1` | SSH logging (0-4) | `TNT_SSH_LOG_LEVEL=3` |
| `TNT_RATE_LIMIT` | `1` | Rate limiting on/off | `TNT_RATE_LIMIT=0` |
| `TNT_MAX_CONNECTIONS` | `64` | Total connection limit | `TNT_MAX_CONNECTIONS=100` |
| `TNT_MAX_CONN_PER_IP` | `5` | Per-IP limit | `TNT_MAX_CONN_PER_IP=3` |

---

## Security Levels

### Level 1: Default (Open)
```bash
./tnt
```
- No authentication
- Rate limiting enabled
- 64 max connections
- 5 per IP

### Level 2: Local Only
```bash
TNT_BIND_ADDR=127.0.0.1 ./tnt
```
- Localhost access only
- Good for development
- No external access

### Level 3: Password Protected
```bash
TNT_ACCESS_TOKEN="MyPassword123" ./tnt
```
- Requires password
- Rate limiting blocks brute force (5 failures → 5 min block)
- 3 auth attempts per session

### Level 4: Maximum Security
```bash
TNT_ACCESS_TOKEN="StrongPass123" \
TNT_BIND_ADDR=127.0.0.1 \
TNT_MAX_CONNECTIONS=10 \
TNT_MAX_CONN_PER_IP=2 \
./tnt
```
- Password required
- Localhost only
- Strict limits
- Rate limiting enabled

---

## Rate Limiting

### Defaults
- **Connection Rate:** 10 connections per IP per 60 seconds
- **Auth Failures:** 5 failures → 5 minute IP block
- **Window:** 60 second rolling window

### Block Behavior
After 5 failed auth attempts:
```
IP 192.168.1.100 blocked due to 5 auth failures
Blocked IP 192.168.1.100 (blocked until 1234567890)
```

Auto-unblock after 5 minutes.

### Disable (Testing Only)
```bash
TNT_RATE_LIMIT=0 ./tnt
```
⚠️ **Warning:** No protection against brute force!

---

## Connection Limits

### Global Limit
```bash
TNT_MAX_CONNECTIONS=50 ./tnt
```
Rejects new connections when 50 total clients connected.

### Per-IP Limit
```bash
TNT_MAX_CONN_PER_IP=3 ./tnt
```
Each IP can have max 3 concurrent connections.

### Combined Example
```bash
TNT_MAX_CONNECTIONS=100 TNT_MAX_CONN_PER_IP=10 ./tnt
```
- Total: 100 connections
- Each IP: max 10 connections

---

## Key Management

### Key Generation
First run automatically generates 4096-bit RSA key:
```
Generating new RSA 4096-bit host key...
```

### Key File
- **Location:** `./host_key`
- **Permissions:** `0600` (owner read/write only)
- **Size:** 4096 bits RSA

### Regenerate Key
```bash
rm host_key
./tnt  # Generates new key
```

### Verify Key
```bash
ssh-keygen -l -f host_key
# Output: 4096 SHA256:... (RSA)
```

---

## Testing

### Run Security Tests
```bash
./test_security_features.sh
```
Expected output: `✓ All security features verified!`

### Manual Tests

**Test 1: Check Key Size**
```bash
./tnt &
sleep 8
ssh-keygen -l -f host_key
# Should show: 4096
kill %1
```

**Test 2: Test Access Token**
```bash
TNT_ACCESS_TOKEN="test123" ./tnt &
sleep 5
sshpass -p "test123" ssh -p 2222 localhost  # Success
sshpass -p "wrong" ssh -p 2222 localhost     # Fails
kill %1
```

**Test 3: Test Rate Limiting**
```bash
./tnt &
sleep 5
for i in {1..15}; do ssh -p 2222 localhost & done
# After 10 connections, should see rate limit blocks
kill %1
```

---

## Troubleshooting

### Server Won't Start
```bash
# Check if port is in use
lsof -i :2222

# Kill existing instance
pkill -f tnt

# Check logs
./tnt 2>&1 | tee debug.log
```

### Can't Connect
```bash
# Check server is listening
lsof -i :2222 | grep tnt

# Check bind address
# If TNT_BIND_ADDR=127.0.0.1, only localhost works
# Use 0.0.0.0 for all interfaces

# Test connection
ssh -v -p 2222 localhost
```

### Authentication Fails
```bash
# Check if token is set
env | grep TNT_ACCESS_TOKEN

# If token is set, password is required
# Use: sshpass -p "YourToken" ssh -p 2222 localhost

# If no token, any password works (or none)
```

### Rate Limited / Blocked
```bash
# Wait 5 minutes for auto-unblock
# Or restart server to clear blocks
pkill -f tnt
./tnt
```

---

## Migration Guide

### Upgrading from Previous Version

**Before:**
```bash
./tnt  # Open access
```

**After (Same Behavior):**
```bash
./tnt  # Still open access, backward compatible!
```

**New: Add Protection**
```bash
TNT_ACCESS_TOKEN="secret" ./tnt  # Now protected
```

### No Breaking Changes
- Default behavior unchanged
- All new features opt-in via environment variables
- Existing scripts/deployments work as-is

---

## Production Deployment

### Recommended Configuration
```bash
#!/bin/bash
# /usr/local/bin/tnt-secure.sh

export TNT_ACCESS_TOKEN="$(cat /etc/tnt/access_token)"
export TNT_BIND_ADDR="0.0.0.0"
export TNT_MAX_CONNECTIONS=200
export TNT_MAX_CONN_PER_IP=10
export TNT_SSH_LOG_LEVEL=1

cd /opt/tnt
exec ./tnt
```

### systemd Service
```ini
[Unit]
Description=TNT Chat Server
After=network.target

[Service]
Type=simple
User=tnt
WorkingDirectory=/opt/tnt
EnvironmentFile=/etc/tnt/config
ExecStart=/opt/tnt/tnt
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

### Config File (/etc/tnt/config)
```bash
TNT_ACCESS_TOKEN=YourProductionPassword
TNT_BIND_ADDR=0.0.0.0
TNT_MAX_CONNECTIONS=500
TNT_MAX_CONN_PER_IP=20
TNT_RATE_LIMIT=1
TNT_SSH_LOG_LEVEL=1
```

---

## Security Best Practices

✅ **DO:**
- Use `TNT_ACCESS_TOKEN` in production
- Set `TNT_BIND_ADDR=127.0.0.1` if local-only
- Keep rate limiting enabled (`TNT_RATE_LIMIT=1`)
- Monitor `messages.log` for suspicious activity
- Rotate access tokens periodically
- Use strong passwords (12+ chars, mixed case, numbers, symbols)

❌ **DON'T:**
- Disable rate limiting in production (`TNT_RATE_LIMIT=0`)
- Use weak passwords (e.g., "password", "123456")
- Expose to internet without access token
- Run as root (use dedicated user)
- Share access tokens in plain text

---

## Performance Impact

| Feature | Impact | Notes |
|---------|--------|-------|
| 4096-bit RSA | First startup: +3s | Cached after generation |
| Rate Limiting | Minimal | Hash table lookup |
| Access Token | Minimal | Simple string compare |
| UTF-8 Validation | Minimal | Per-character check |
| Message Snapshot | Low | Only during render |

Expected overhead: <5% in normal usage

---

## Support

- **Documentation:** `README.md`, `CHANGELOG.md`
- **Test Results:** `TEST_RESULTS.md`
- **Test Suite:** `./test_security_features.sh`
- **Issues:** https://github.com/m1ngsama/TNT/issues
