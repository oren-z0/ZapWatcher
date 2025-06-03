# ZapWatcher

An ESP32 project to trigger whenever your Nostr npub receives a zap.

Installation instructions:

In your Arudino IDE, install the following packages:
- ArduinoJson
- Nostr
- WifiManager
- uBitcoin

uBitcoin 0.2.0 has some collisions with the WifiManager project
(See PRs [#35](https://github.com/micro-bitcoin/uBitcoin/pull/35),
[#36](https://github.com/micro-bitcoin/uBitcoin/pull/36)).

Until those PRs are resolved, you can replace the library files
with [uBitcoin0.2.0-fixed.zip](uBitcoin0.2.0-fixed.zip).
