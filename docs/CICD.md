# CI/CD and Release Governance

TNT is a C SSH terminal chat server. The CI/CD system is designed for a public
open-source project: fast feedback on pull requests, broader scheduled
validation across target environments, reproducible release artifacts, and a
manual production deployment boundary.

Production deployment is intentionally manual. Workflows must not SSH into
production, restart services, upload to OSS buckets, publish package-manager
recipes, or mutate running servers.

## Pipeline Layers

### PR Fast Gate

Workflow: `.github/workflows/ci.yml`

Runs on pull requests targeting `main` or `release/**`, and pushes to `main`
or `release/**`:

- Ubuntu 24.04 and macOS latest builds.
- Normal build with `make`.
- AddressSanitizer build with `make asan`.
- Integration/security gate with `make ci-test`.
- Local release/package preflight with `make release-check`.

Purpose:

- Keep contributor feedback fast enough for normal review.
- Catch build, integration, packaging metadata, and release-preflight regressions
  before merge.
- Avoid slow soak, valgrind, and container matrix jobs on every PR.

### Extended and Nightly Validation

Workflow: `.github/workflows/ci.yml`

Runs on `main` or `release/**` pushes, manual dispatch, and the nightly
schedule:

- `extended-linux-runtime`
  - Runs `RUN_INTEGRATION=1 RUN_SOAK=1 RUN_SLOW_CLIENT=1 make release-check`.
  - Runs a valgrind smoke test against a temporary server.
- `portable-container-builds`
  - Builds in Debian stable glibc.
  - Builds in Ubuntu 24.04 glibc.
  - Builds in Alpine musl.
- `package-recipe-gate`
  - Syntax-checks shell scripts.
  - Syntax-checks the Arch `PKGBUILD`.
  - Syntax-checks the Homebrew formula.
  - Assembles the Debian source tree.

Purpose:

- Broaden platform confidence without making every PR wait for the full matrix.
- Detect musl/glibc portability issues early.
- Keep package metadata reviewable before public registry submission.

### Release Artifact Gates

Workflow: `.github/workflows/release.yml`

Runs only for SemVer tags matching `vMAJOR.MINOR.PATCH`:

- Verifies the tag matches `TNT_VERSION` through `scripts/check_release_ref.sh`.
- Builds Linux glibc AMD64 and ARM64 binaries.
- Builds macOS Intel and Apple Silicon binaries.
- Verifies binary architecture labels.
- Builds an explicit source archive: `tnt-chat-vX.Y.Z-source.tar.gz`.
- Runs `scripts/package_release_assets.sh` to collect release assets, verify
  expected asset names, verify binary architecture labels again after artifact
  download, verify source archive contents, generate `checksums.txt`, and verify
  the checksum file.
- Creates a GitHub draft release only. Publishing stays manual.

The release workflow does not publish package-manager recipes or deploy
production servers.

## Platform Policy

Current release assets:

- Linux glibc AMD64: `tnt-linux-amd64`, `tntctl-linux-amd64`
- Linux glibc ARM64: `tnt-linux-arm64`, `tntctl-linux-arm64`
- macOS Intel: `tnt-darwin-amd64`, `tntctl-darwin-amd64`
- macOS Apple Silicon: `tnt-darwin-arm64`, `tntctl-darwin-arm64`
- Source archive: `tnt-chat-vX.Y.Z-source.tar.gz`

Current CI validation:

- Ubuntu 24.04
- macOS latest
- Debian stable glibc container build
- Ubuntu 24.04 glibc container build
- Alpine musl container build

Package-manager routes:

- Debian/Ubuntu: maintain draft Debian metadata and start with a Launchpad PPA.
- Arch/AUR: maintain `packaging/arch/PKGBUILD` and `.SRCINFO`; submit manually.
- Homebrew/macOS: maintain a tap formula first; Homebrew core can wait for a
  stable release cadence and broader adoption.
- Source archive: every public package recipe must pin the final GitHub release
  source archive checksum.
- Containers: first stage is Docker-based build validation in CI. Publishing
  images should wait until image labels, SBOM, provenance, CVE scanning, and
  registry ownership are defined.

## Release Policy

- Use SemVer-style tags: `vMAJOR.MINOR.PATCH`.
- Bump PATCH for compatible bug fixes and release hardening.
- Bump MINOR for new commands, new documented flags, JSON field additions, or
  visible user-interface behavior changes.
- Bump MAJOR for incompatible command, config, storage, or package behavior.
- Keep GitHub release publishing manual by using draft releases.
- Keep production deployment manual.

Update version metadata before tagging:

- `include/common.h`
- `tnt.1`
- `tntctl.1`
- `docs/CHANGELOG.md`
- `packaging/arch/PKGBUILD`
- `packaging/arch/.SRCINFO`
- `packaging/homebrew/tnt-chat.rb`
- `packaging/debian/debian/changelog`

Local preflight:

```sh
make release-check
```

Longer local runtime gate:

```sh
RUN_INTEGRATION=1 RUN_SOAK=1 RUN_SLOW_CLIENT=1 make release-check
```

Strict local release gate before pushing a tag:

```sh
git tag vX.Y.Z
make release-check-strict
```

Strict mode requires the local `vX.Y.Z` tag to point at `HEAD` and builds from
the tagged source archive, so it catches files that were left untracked and
would be missing from the release source archive.

After strict checks pass:

```sh
git push origin vX.Y.Z
```

GitHub Actions then builds artifacts and opens a draft release. Review and
publish that draft manually.

## Release Review Checklist

Before publishing a draft release:

- Confirm the Git tag points at the intended commit.
- Confirm the release workflow passed.
- Download every release asset from GitHub, not from the local workspace.
- Verify downloaded assets against `checksums.txt`.
- Run downloaded `tnt --version` and `tntctl --version`.
- Start a temporary server and check:

  ```sh
  ssh -p 2222 server health
  ssh -p 2222 server stats --json
  ssh -p 2222 server users --json
  ssh -p 2222 operator@server post "release smoke"
  ssh -p 2222 server "tail -n 1"
  ```

- Check runtime dynamic links with `ldd` on Linux or `otool -L` on macOS.
- Confirm `libssh` runtime installation is documented for the target install
  path.
- Verify the explicit source archive checksum before updating Arch, Homebrew,
  Debian, Ubuntu, or container package metadata.
- Run package publication preflight after package recipes pin final source
  checksums:

  ```sh
  SOURCE_TARBALL=dist/tnt-chat-vX.Y.Z-source.tar.gz make package-publish-check
  ```

## Checksums

Release assets include `checksums.txt`.

Linux:

```sh
sha256sum -c checksums.txt --ignore-missing
```

macOS:

```sh
for f in tnt-* tntctl-* tnt-chat-*-source.tar.gz; do
  grep "  $f$" checksums.txt | shasum -a 256 -c -
done
```

## Supply Chain Roadmap

Stage 1, implemented now:

- Tag/version gate.
- Draft release, manual publish.
- Binary architecture validation.
- Source archive validation.
- SHA-256 checksums for every release asset.
- Package recipe checksum preflight.

Stage 2, next:

- Generate an SBOM for release artifacts, preferably CycloneDX or SPDX.
- Attach SBOM files to draft releases.
- Add package lint jobs for Debian source packages, Arch packages, Homebrew
  audit, and container image metadata.

Stage 3, later:

- Sign release checksums and/or artifacts with a documented maintainer key or
  Sigstore flow.
- Add SLSA provenance for GitHub Actions builds.
- Define container image ownership, tag policy, vulnerability scan policy, and
  rollback behavior before publishing images.

## Manual Production Deployment

Deployment remains operator-driven:

1. Build and test locally or in a temporary server directory.
2. Back up the installed binary.
3. Install the new binary.
4. Restart only the intended `tnt` service.
5. Run black-box checks: `health`, `stats --json`, `users --json`, and one
   post/tail smoke test.

Manual binary replacement pattern:

```sh
backup=/usr/local/bin/tnt.bak-$(date +%Y%m%d%H%M%S)
sudo cp -a /usr/local/bin/tnt "$backup"
sudo install -m 755 ./tnt /usr/local/bin/tnt
sudo systemctl restart tnt
```

## Rollback

Production rollback stays manual:

1. Keep the previous binary before replacing it.
2. Stop or restart only the intended `tnt` service.
3. Restore the previous binary if smoke checks fail.
4. Re-run `health`, `stats --json`, and one post/tail smoke test.

Do not overwrite `TNT_STATE_DIR` during rollback. If a future release changes
the message log format, its release notes must include downgrade behavior.
