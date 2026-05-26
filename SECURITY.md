# Security Policy

## Supported Versions

TNT currently supports security fixes for the latest published release and the
current `main` branch.

| Version | Supported |
|---|---|
| latest release | yes |
| `main` | best effort |
| older releases | no |

This policy will become stricter after TNT has a longer stable release history.

## Reporting a Vulnerability

Do not open a public issue for a security vulnerability.

Report privately through one of these paths:

- GitHub private vulnerability reporting, when available on the repository
- email: `contact@m1ng.space`

Include:

- affected version or commit
- operating system and deployment shape
- reproduction steps or proof of concept
- expected impact
- whether the issue is already public

## Response

The maintainer will try to acknowledge valid reports within 7 days.  Fixes may
land on `main` before a release is published.  For serious issues, the release
notes will mention the security impact after users have a reasonable upgrade
path.

## Scope

In scope:

- remote crashes or memory-safety bugs
- authentication or access-token bypass
- unintended file writes outside `TNT_STATE_DIR`
- privilege escalation in packaged service configuration
- release artifact tampering or installer verification bypass

Out of scope:

- denial of service from an operator intentionally disabling rate limits
- identity spoofing in the documented anonymous-access mode
- vulnerabilities requiring local administrator access to the host

## Release Integrity

Release binaries are published with `checksums.txt`.  The installer verifies
the selected binary against that file before installation.  Future releases
should add a detached signature for `checksums.txt` before package recipes are
submitted to public registries.
