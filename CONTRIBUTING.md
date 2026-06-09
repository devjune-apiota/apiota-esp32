# Contributing to APIOTA ESP32 Library

Thanks for your interest! This repo is the open-source ESP32/Arduino client library
for the [APIOTA](https://apiota.net) OTA platform.

## Reporting issues

Open a GitHub issue and include:

- Board (e.g. ESP32-WROOM, LILYGO T-Display-S3, T-SIM A7670G)
- Arduino core / library versions
- A minimal sketch that reproduces the problem
- Serial log output (the library logs each OTA step over Serial)

**Never paste real secrets** — redact your API key (`ak_...`), Wi-Fi password, and
device secret before posting.

## Pull requests

1. Fork and create a feature branch.
2. Keep the core (`src/`) header-only and dependency-free beyond the ESP32 Arduino core.
3. Make sure every example still compiles for its target board.
4. Don't commit credentials, `.bin` build artifacts, or editor/OS junk (see `.gitignore`).
5. Describe what you changed and why.

## Code style

- C++ for ESP32 Arduino; match the existing formatting in `src/`.
- Prefer small, focused changes.

By contributing you agree your contributions are licensed under the [MIT License](LICENSE).
