CI/CD USAGE GUIDE
=================

AUTOMATIC TESTING
-----------------
Every push or PR automatically runs:
  - Build on Ubuntu and macOS
  - AddressSanitizer checks
  - Valgrind memory leak detection

Check status:
  https://github.com/m1ngsama/TNT/actions


CREATING RELEASES
-----------------
1. Update version in CHANGELOG.md

2. Create and push tag:
   git tag v1.0.0
   git push origin v1.0.0

3. GitHub Actions automatically:
   - Builds binaries (Linux/macOS, AMD64/ARM64)
   - Creates release
   - Uploads binaries
   - Generates checksums

4. Release appears at:
   https://github.com/m1ngsama/TNT/releases


DEPLOYING TO SERVERS
--------------------
Single command on any server:
  curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh

Or with specific version:
  VERSION=v1.0.0 curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh


PRODUCTION SETUP (systemd)
---------------------------
1. Install binary (see above)

2. Setup service:
   sudo useradd -r -s /bin/false tnt
   sudo mkdir -p /var/lib/tnt
   sudo chown tnt:tnt /var/lib/tnt
   sudo cp tnt.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable --now tnt

3. Check status:
   sudo systemctl status tnt
   sudo journalctl -u tnt -f


UPDATING SERVERS
----------------
Stop service:
  sudo systemctl stop tnt

Run installer again:
  curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh

Restart:
  sudo systemctl start tnt


PLATFORMS SUPPORTED
-------------------
✓ Linux AMD64 (x86_64)
✓ Linux ARM64 (aarch64)
✓ macOS Intel (x86_64)
✓ macOS Apple Silicon (arm64)


EXAMPLE WORKFLOW
----------------
# Local development
make && make asan
./tnt

# Create release
git tag v1.0.1
git push origin v1.0.1
# Wait 5 minutes for builds

# Deploy to production servers
for server in server1 server2 server3; do
  ssh $server "curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | VERSION=v1.0.1 sh"
  ssh $server "sudo systemctl restart tnt"
done
