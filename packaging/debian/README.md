# Debian and Ubuntu Packaging

Ubuntu distribution should start with a Launchpad PPA. Direct inclusion in
Debian or Ubuntu archives is a separate, slower process and should wait until
the project has a stable release cadence.

## Recommended path

1. Keep the upstream project installable with:

   ```sh
   make DESTDIR="$pkgdir" PREFIX=/usr install
   ```

2. Create Debian packaging metadata from a release tarball:

   - `debian/control`
   - `debian/rules`
   - `debian/changelog`
   - `debian/copyright`
   - `debian/install`
   - optional `debian/tnt.service`

3. Build locally with `debuild` or `dpkg-buildpackage`.
4. Upload the signed source package to a Launchpad PPA.
5. Only after repeated stable releases, consider Debian mentors or Ubuntu
   archive sponsorship.

## Package shape

- Binary package name: `tnt-chat`
- Installed command: `/usr/bin/tnt`
- Runtime dependency: `libssh`
- Optional systemd unit: `/usr/lib/systemd/system/tnt.service`
