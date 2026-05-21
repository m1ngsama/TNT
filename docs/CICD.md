CI / RELEASE GUIDE
==================

AUTOMATIC TESTING
-----------------
Every push or PR automatically runs:
  - Build on Ubuntu
  - AddressSanitizer build
  - Unit and integration tests

Check status:
  https://github.com/m1ngsama/TNT/actions

Production deployment is intentionally manual. The CI workflow must not SSH
into production or restart services on push.


CREATING RELEASES
-----------------
1. Update version metadata:
   - include/common.h
   - tnt.1
   - docs/CHANGELOG.md
   - packaging/arch/PKGBUILD
   - packaging/homebrew/tnt-chat.rb

2. Run the local preflight:
   make release-check

3. Replace package checksum placeholders and run:
   make release-check-strict

4. Create and push tag:
   git tag v1.0.0
   git push origin v1.0.0

5. GitHub Actions automatically:
   - Builds binaries (Linux/macOS, AMD64/ARM64)
   - Creates release
   - Uploads binaries
   - Generates checksums

6. Release appears at:
   https://github.com/m1ngsama/TNT/releases


DEPLOYING TO SERVERS
--------------------
Deployments are operator-driven:
  1. Build and test locally or in a temporary server directory.
  2. Back up the installed binary.
  3. Install the new binary.
  4. Restart the service.
  5. Run black-box checks (`health`, `stats --json`, `users --json`,
     `support`, and a post/tail smoke test).

The installer can still be used manually on a server:
  curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh


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
Manual binary replacement pattern:
  backup=/usr/local/bin/tnt.bak-$(date +%Y%m%d%H%M%S)
  sudo cp -a /usr/local/bin/tnt "$backup"
  sudo install -m 755 ./tnt /usr/local/bin/tnt
  sudo systemctl restart tnt


PLATFORMS SUPPORTED
-------------------
✓ Linux AMD64 (x86_64)
✓ Linux ARM64 (aarch64)
✓ macOS Intel (x86_64)
✓ macOS Apple Silicon (arm64)


EXAMPLE WORKFLOW
----------------
# Local development
make && make asan && make release-check
./tnt

# Create release
git tag v1.0.1
git push origin v1.0.1
# Wait 5 minutes for builds

# Deploy to production manually after validation
ssh server "sudo install -m 755 /tmp/tnt-build/tnt /usr/local/bin/tnt"
ssh server "sudo systemctl restart tnt"
ssh -p 2222 server health
