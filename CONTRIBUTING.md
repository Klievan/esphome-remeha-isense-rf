# Contributing

Bug reports, test results on other hardware, and protocol observations are welcome.

- **Protocol claims need evidence.** In `docs/protocol.md`, tag new statements as confirmed by captures, experimentally
  verified, inferred, or unknown, and say how you checked.
- **Don't publish your kit ID.** Captures and logs contain it (data bits 0–123, and the `display_template`). Mask it
  before posting, e.g. replace those bits with zeros and say that you did.
- **Run the tests** before opening a PR:
  ```sh
  c++ -std=c++17 -Wall -Wextra -o /tmp/t tests/test_remeha_frame.cpp && /tmp/t
  python3 tools/sniffer/decode.py
  ```
- Keep changes to radio timing and register settings minimal, and test them on real hardware. Say in the PR what you
  tested.
- No Remeha firmware, extracted code or confidential documents, please.
