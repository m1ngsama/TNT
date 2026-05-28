# Debian and Ubuntu Packaging

Ubuntu distribution should start with a Launchpad PPA. Direct inclusion in
Debian or Ubuntu archives is a separate, slower process and should wait until
the project has a stable release cadence.

## Draft metadata

The `debian/` directory in this folder is a packaging draft. To assemble it
against a clean source tree:

```sh
make debian-source-package
```

For PPA uploads, build a source package on Debian/Ubuntu:

```sh
scripts/package_debian_source.sh --build
```

## Recommended path

1. Keep the upstream project installable with:

   ```sh
   make DESTDIR="$pkgdir" PREFIX=/usr install
   ```

2. Review Debian packaging metadata from a release tarball:

   - `debian/control`
   - `debian/rules`
   - `debian/changelog`
   - `debian/copyright`
   - `debian/source/format`

3. Build locally with `debuild` or `dpkg-buildpackage`.
4. Upload the signed source package to a Launchpad PPA.
5. Only after repeated stable releases, consider Debian mentors or Ubuntu
   archive sponsorship.

## Package shape

- Binary package name: `tnt-chat`
- Installed commands: `/usr/bin/tnt`, `/usr/bin/tntctl`
- Runtime dependency: `libssh`
- Optional systemd unit: `/usr/lib/systemd/system/tnt.service`
- System user: package maintainer scripts create `tnt:tnt`; the systemd unit
  owns `/var/lib/tnt` through `StateDirectory=tnt`
