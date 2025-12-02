# Changelog

## 2025-12-02 - Stability & Testing Update

### Fixed
- Double colon bug in vim command mode (`:` key consumed properly)
- strtok data corruption in command output rendering
- Use-after-free race condition (added reference counting)
- SSH read blocking issues (added timeouts)
- PTY request infinite loop
- Message history memory waste (optimized loading)

### Added
- Reference counting for thread-safe client cleanup
- SSH read timeout (30s) and error handling
- UTF-8 incomplete sequence detection
- AddressSanitizer build target (`make asan`)
- Basic functional tests (`test_basic.sh`)
- Stress testing script (`test_stress.sh`)
- Static analysis target (`make check`)
- Developer documentation (HACKING)

### Changed
- Improved error handling throughout
- Better memory management in message loading

## 2025
- Ongoing development and improvements
- Bug fixes and optimizations
- Feature enhancements
- Optimize performance (2025-01-10)
- Code cleanup (2025-01-15)
- Code cleanup (2025-01-17)
- Add minor improvements (2025-01-22)
- Code cleanup (2025-01-28)
- Fix edge cases (2025-02-03)
- Update documentation (2025-02-06)
- Fix edge cases (2025-02-07)
- Add minor improvements (2025-02-26)
- Update dependencies (2025-02-27)
- Fix edge cases (2025-03-01)
- Fix bugs and improve stability (2025-03-06)
- Fix bugs and improve stability (2025-03-12)
- Minor fixes (2025-03-17)
- Add minor improvements (2025-03-18)
- Refactor code structure (2025-03-24)
- Update dependencies (2025-03-27)
- Improve error handling (2025-03-28)
- Improve error handling (2025-04-03)
- Update documentation (2025-04-07)
- Update documentation (2025-04-13)
- Code cleanup (2025-04-15)
- Fix bugs and improve stability (2025-04-16)
- Add minor improvements (2025-04-17)
- Minor fixes (2025-04-23)
- Code cleanup (2025-04-24)
- Fix edge cases (2025-04-25)
- Refactor code structure (2025-05-13)
- Fix edge cases (2025-05-14)
- Minor fixes (2025-06-03)
- Code cleanup (2025-06-05)
- Add minor improvements (2025-06-10)
- Fix bugs and improve stability (2025-06-18)
- Update dependencies (2025-06-24)
- Optimize performance (2025-06-30)
- Update documentation (2025-07-07)
- Refactor code structure (2025-07-17)
- Fix bugs and improve stability (2025-07-19)
- Refactor code structure (2025-07-21)
- Code cleanup (2025-07-27)
- Code cleanup (2025-08-04)
- Minor fixes (2025-08-28)
- Improve error handling (2025-09-05)
- Update documentation (2025-09-09)
- Code cleanup (2025-09-15)
- Fix bugs and improve stability (2025-09-19)
- Update documentation (2025-09-25)
- Fix bugs and improve stability (2025-10-06)
- Fix bugs and improve stability (2025-10-13)
- Fix bugs and improve stability (2025-10-16)
- Optimize performance (2025-10-17)
- Add minor improvements (2025-10-22)
- Code cleanup (2025-10-26)
- Add minor improvements (2025-10-28)
- Fix edge cases (2025-10-29)
- Fix bugs and improve stability (2025-10-30)
- Optimize performance (2025-11-04)
- Improve error handling (2025-11-07)
- Update documentation (2025-11-12)
- Fix bugs and improve stability (2025-11-14)
- Update documentation (2025-11-17)
- Add minor improvements (2025-11-18)
- Refactor code structure (2025-11-19)
- Fix bugs and improve stability (2025-11-20)
- Minor fixes (2025-11-24)
