# ZapWatcher

An ESP32 project to trigger whenever your Nostr npub receives a zap.

## Demo Video

https://files.niot.space/ZapWatcher-Demo.mp4

## Installation instructions

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

## Common Issues

### Sketch is too big

`Sketch uses 1317946 bytes (100%) of program storage space. Maximum is 1310720 bytes.`

Under Tools -> Partition Scheme, instead of "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
choose "No OTA (2MB APP/2MB SPIFFS)".
