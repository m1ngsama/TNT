CI / RELEASE GUIDE
==================

AUTOMATIC TESTING
-----------------
Every push or PR automatically runs:
  - Build on Ubuntu
  - AddressSanitizer build
  - `make ci-test` (strict integration, anonymous access, connection limits,
    and security feature checks)
  - Release/package preflight (`make release-check`)

Check status:
  https://github.com/m1ngsama/TNT/actions

Production deployment is intentionally manual. The CI workflow must not SSH
into production or restart services on push.


CREATING RELEASES
-----------------
Release policy:
  - Use SemVer-style tags: vMAJOR.MINOR.PATCH.
  - Bump PATCH for compatible bug fixes and release hardening.
  - Bump MINOR for new commands, new documented flags, JSON field additions,
    or visible user-interface behavior changes.
  - Bump MAJOR for incompatible command, config, storage, or package behavior.
  - Keep GitHub draft release review manual. Do not auto-publish releases.
  - Keep production deployment manual. Do not SSH into production from CI.

1. Update version metadata:
   - include/common.h
   - tnt.1
   - docs/CHANGELOG.md
   - packaging/arch/PKGBUILD
   - packaging/homebrew/tnt-chat.rb
   - packaging/debian/debian/changelog
   - package checksums and maintainer metadata, when preparing public package
     recipes

2. Run the local preflight:
   make release-check

   For a longer local runtime gate before publishing or production rollout:
   RUN_SOAK=1 RUN_SLOW_CLIENT=1 make release-check

3. Commit the release changes and create a local tag.  Do not push the tag
   until strict checks pass:
   git tag v1.0.1

4. Run strict release checks:
   make release-check-strict

   Strict mode requires the local `vX.Y.Z` tag to point at HEAD.  It also
   builds from the tagged source archive, so it catches files that were left
   untracked and would be missing from GitHub's source archive.

5. Push the tag:
   git push origin v1.0.1

6. GitHub Actions automatically:
   - Builds `tnt` and `tntctl` binaries (Linux/macOS, AMD64/ARM64)
   - Creates a draft release
   - Uploads binaries
   - Generates one `checksums.txt` file
   - Verifies that artifact architecture matches the asset name

7. Review the draft release, smoke-test downloaded assets, then publish it
   manually from GitHub.

8. Release appears at:
   https://github.com/m1ngsama/TNT/releases


RELEASE REVIEW CHECKLIST
------------------------
Before publishing a draft release:
  - Confirm `git tag` points at the intended commit.
  - Download every release asset from GitHub, not from the local workspace.
  - Verify `checksums.txt` with `sha256sum -c checksums.txt`.
  - Run downloaded `tnt --version` and `tntctl --version`.
  - Start a temporary server and check:
      ssh -p 2222 server health
      ssh -p 2222 server stats --json
      ssh -p 2222 server users --json
      ssh -p 2222 operator@server post "release smoke"
      ssh -p 2222 server "tail -n 1"
  - Check runtime dynamic links (`ldd` on Linux, `otool -L` on macOS) and make
    sure `libssh` is documented for the target install path.
  - Confirm `make release-check-strict` passed after package checksums were
    replaced.


ROLLBACK
--------
Production rollback stays manual:
  1. Keep the previous binary before replacing it.
  2. Stop or restart only the intended `tnt` service.
  3. Restore the previous binary if smoke checks fail.
  4. Re-run `health`, `stats --json`, and one post/tail smoke test.

Do not overwrite `TNT_STATE_DIR` during rollback.  If a future release changes
the message log format, its release notes must include the downgrade behavior.


DEPLOYING TO SERVERS
--------------------
Deployments are operator-driven:
  1. Build and test locally or in a temporary server directory.
  2. Back up the installed binary.
  3. Install the new binary.
  4. Restart the service.
  5. Run black-box checks (`health`, `stats --json`, `users --json`,
     and a post/tail smoke test).

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
