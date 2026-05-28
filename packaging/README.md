# Packaging

This directory contains package-manager drafts for TNT. They are intentionally
kept out of the root install path and should be reviewed before submission to
any public registry.

## Current targets

- `arch/` - AUR-ready draft for `tnt-chat`.
- `homebrew/` - Homebrew tap formula draft and maintainer notes.
- `debian/` - Ubuntu PPA / Debian packaging notes and draft metadata.

Package installs include both `tnt` and `tntctl`.  `tnt` is the server process;
`tntctl` is a thin wrapper around the documented SSH exec interface.

## CI governance

Package recipes are validated in stages:

- PR fast gate: `make release-check` verifies package metadata stays aligned
  with `TNT_VERSION`.
- Extended CI: package syntax and Debian source-tree assembly run on `main` and
  `release/**` pushes, nightly, and manual workflow dispatch.
- Release gate: the workflow builds an explicit release source archive, verifies
  it, and includes it in `checksums.txt`.
- Publishing gate: after final source checksums are pinned, run
  `SOURCE_TARBALL=... make package-publish-check`.

All package-manager submissions remain manual. CI must not push to AUR, open or
merge Homebrew tap updates, upload Debian/PPA packages, publish container
images, or deploy production servers.

## Release checklist

1. Confirm `TNT_VERSION` in `include/common.h` and the manpage version match.
   Also update package versions in Arch, Homebrew, and Debian drafts.
2. Create a GitHub release tag such as `vX.Y.Z`.
3. Let the release workflow build the explicit release source archive and draft
   release assets.
4. Replace placeholder checksums in package drafts.
5. Verify package contents in an isolated directory:

   ```sh
   make release-check
   ```

6. Assemble a Debian/PPA source tree when preparing Ubuntu packaging:

   ```sh
   make debian-source-package
   ```

   Use `scripts/package_debian_source.sh --build` on a Debian/Ubuntu system
   with `dpkg-buildpackage` installed to build the unsigned source package.

7. Before submitting package recipes, download the explicit release source archive,
   replace checksum placeholders, and run:

   ```sh
   SOURCE_TARBALL=dist/tnt-chat-vX.Y.Z-source.tar.gz make package-publish-check
   ```

8. Submit packages manually:
   - Arch: upload `PKGBUILD` and generated `.SRCINFO` to AUR.
   - Homebrew: open a PR to the project tap, or later Homebrew core if eligible.
   - Ubuntu: build Debian source packages and upload to a Launchpad PPA.

Do not connect these packaging drafts to automatic production deployment.
