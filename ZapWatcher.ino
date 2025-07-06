#include <time.h>
#include <Arduino.h>
#include <Preferences.h>

#include <WiFi.h>
#include <HTTPClient.h>

#include <NostrRelayManager.h>
#include <NostrEvent.h>

#include <vector>  // Add this for std::vector
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager

#define ZAPWATCHER_VERSION (1)

#define MIN_RELAYS (2)
#define INVALID_PIN_NUMBER (0xFFFF)
#define MAX_HTTP_RETRIES (600)

// Define custom parameters
WiFiManagerParameter wm_nostr_relays("nostr_relays", "Relays (Separate by space)", "", 200);
WiFiManagerParameter wm_recipient_npub("recipient_npub", "Recipient npub", "", 64);
WiFiManagerParameter wm_nostr_min_zap("nostr_min_zap", "Min Zap (milli sats)", "", 19);
WiFiManagerParameter wm_sender_npub("sender_npub", "Sender npub (optional)", "", 64);
WiFiManagerParameter wm_niot_trigger_id("niot_trigger_id", "nIoT Trigger ID (optional)", "", 26);
WiFiManagerParameter wm_niot_price("niot_price", "nIoT Price (optional, for fiat pricing)", "", 20);
WiFiManagerParameter wm_niot_unit("niot_unit", "nIoT Price Unit (optional, for fiat pricing)", "", 10);
WiFiManagerParameter wm_pin_number("pin_number", "PIN Number", "", 2);
WiFiManagerParameter wm_run_time("run_time", "Runtime (milliseconds)", "", 6);

// Preferences for storing configuration
Preferences preferences;

NostrEvent nostr;
NostrRelayManager nostrRelayManager;

String nostrRecipientPubkey = "";
String nostrWalletPubkey = "";
long nostrMinZap = 0;
String nostrSenderPubkey = "";
String niotTriggerId = "";
String niotPrice = "";
String niotUnit = "";
int pinNumber = INVALID_PIN_NUMBER;
int runtimeMs = 0;

int kind0CreatedAt = 0;
int kind9735CreatedAt = 0;

bool savedNewParams = false;

void onSaveParams() {
  Serial.println(F("Saving params"));
  String nostrRecipientNpub = String(wm_recipient_npub.getValue());
  nostrRecipientNpub.toLowerCase();
  String nostrRelaysStr = String(wm_nostr_relays.getValue());
  String nostrMinZapStr = String(wm_nostr_min_zap.getValue());
  nostrMinZap = nostrMinZapStr.toInt();
  String nostrSenderNpub = String(wm_sender_npub.getValue());
  nostrSenderNpub.toLowerCase();
  niotTriggerId = String(wm_niot_trigger_id.getValue());
  niotPrice = String(wm_niot_price.getValue());
  if (niotPrice.indexOf('.') >= 0) {
    while (niotPrice[niotPrice.length() - 1] == '0') {
      niotPrice = niotPrice.substring(0, niotPrice.length() - 1);
    }
  }
  if (niotPrice[niotPrice.length() - 1] == '.') {
    niotPrice = niotPrice.substring(0, niotPrice.length() - 1);
  }
  niotUnit = String(wm_niot_unit.getValue());
  String pinNumberStr = String(wm_pin_number.getValue());
  pinNumber = pinNumberStr == "" ? INVALID_PIN_NUMBER : pinNumberStr.toInt();
  String runtimeMsStr = String(wm_run_time.getValue());
  runtimeMs = runtimeMsStr.toInt();

  preferences.begin("config", false);
  Serial.print(F("Saving nostr_relays: "));
  Serial.println(nostrRelaysStr);
  preferences.putString("nostr_relays", nostrRelaysStr);
  Serial.print(F("Saving recipient_npub: "));
  Serial.println(nostrRecipientNpub);
  preferences.putString("recipient_npub", nostrRecipientNpub);
  Serial.print(F("Saving nostr_min_zap: "));
  Serial.println(nostrMinZapStr);
  preferences.putULong("nostr_min_zap", nostrMinZapStr.toInt());
  Serial.print(F("Saving sender_npub: "));
  Serial.println(nostrSenderNpub);
  preferences.putString("sender_npub", nostrSenderNpub);
  Serial.print(F("Saving niot_trigger_id: "));
  Serial.println(niotTriggerId);
  preferences.putString("niot_trigger_id", niotTriggerId);
  Serial.print(F("Saving niot_price: "));
  Serial.println(niotPrice);
  preferences.putString("niot_price", niotPrice);
  Serial.print(F("Saving niot_unit: "));
  Serial.println(niotUnit);
  preferences.putString("niot_unit", niotUnit);
  Serial.print(F("Saving pin_number: "));
  Serial.println(pinNumberStr);
  preferences.putUShort("pin_number", pinNumberStr.toInt());
  Serial.print(F("Saving run_time: "));
  Serial.println(runtimeMsStr);
  preferences.putUInt("run_time", runtimeMsStr.toInt());
  preferences.end();
  delay(1000);

  savedNewParams = true;
}


String npubToHex(const String& npub) {
  // Remove "npub" prefix if present
  if (!npub.startsWith("npub")) {
    return "";
  }

  // bech32 decode
  const char* bech32Chars = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
  int bech32Len = strlen(bech32Chars);

  // Convert bech32 to 5-bit values
  uint8_t data[100] = {0};  // Buffer for 5-bit values
  int dataLen = 0;

  for (int i = 4; i < npub.length(); i++) {
    char c = npub[i];
    for (int j = 0; j < bech32Len; j++) {
      if (bech32Chars[j] == c) {
        data[dataLen++] = j;
        break;
      }
    }
  }

  // Convert 5-bit values to 8-bit bytes
  uint8_t bytes[32] = {0};  // Buffer for 8-bit bytes
  int byteLen = 0;
  int bitBuffer = 0;
  int bitsInBuffer = 0;

  for (int i = 0; (i < dataLen) && (byteLen < 32); i++) {
    bitBuffer = (bitBuffer << 5) | data[i];
    bitsInBuffer += 5;
    while ((bitsInBuffer >= 8) && (byteLen < 32)) {
      bytes[byteLen++] = (bitBuffer >> (bitsInBuffer - 8)) & 0xFF;
      bitsInBuffer -= 8;
    }
  }
  // Does not verify checksum

  // Convert bytes to hex
  String hex = "";
  for (int i = 0; i < byteLen; i++) {
    char hexChars[3];
    sprintf(hexChars, "%02x", bytes[i]);
    hex += hexChars;
  }

  return hex;
}


void okEvent(const std::string& key, const char* payload) {
  Serial.println(F("OK event"));
  Serial.println(F("payload is: "));
  Serial.println(payload);
}

String getNostrWalletPubkey(const String& domain, const String& username) {
  nostrRelayManager.disconnect();
  String url = "https://" + domain + "/.well-known/lnurlp/" + username;

  Serial.print(F("Sending GET request to: "));
  Serial.println(url);

  StaticJsonDocument<4098> responseDoc;
  int httpAttempt = 0;
  for (; httpAttempt < MAX_HTTP_RETRIES; httpAttempt++) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(url);
    int httpCode = http.GET();
    Serial.print(F("httpCode: "));
    Serial.println(httpCode);
    if (httpCode < 200 || httpCode >= 300) {
      Serial.print(F("httpCode is not 200-299: "));
      Serial.println(http.errorToString(httpCode));
      http.end();
      delay(1000);
      continue;
    }

    DeserializationError error = deserializeJson(responseDoc, http.getStream());
    http.end();
    if (error) {
      Serial.print(F("http response deserializeJson() failed: "));
      Serial.println(error.c_str());
      delay(1000);
      continue;
    }
    break;
  }
  nostrRelayManager.connect();
  if (httpAttempt >= MAX_HTTP_RETRIES) {
    Serial.println(F("Failed to get nostr pubkey"));
    ESP.restart();
    delay(5000);
    return "";
  }
  Serial.println(F("Response JSON:"));
  serializeJsonPretty(responseDoc, Serial);
  Serial.println();

  JsonVariantConst newNostrPubkey = responseDoc["nostrPubkey"];
  if (!newNostrPubkey.is<const char*>()) {
    Serial.println(F("No nostrPubkey"));
    return "";
  }
  return String(newNostrPubkey);
}


void kind0Event(const std::string& key, const char* payload) {
  Serial.println(F("Kind 0 event"));
  Serial.println(F("payload is: "));
  Serial.println(payload);

  StaticJsonDocument<4098> kind0Doc;
  DeserializationError error = deserializeJson(kind0Doc, payload);

  if (error) {
    Serial.print(F("event deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  JsonVariantConst eventType = kind0Doc[0];

  if (!eventType.is<const char*>() || strcmp(eventType, "EVENT") != 0) {
    Serial.println(F("Not an event"));
    return;
  }

  JsonVariantConst createdAt = kind0Doc[2]["created_at"];
  if (!createdAt.is<int>()) {
    Serial.println(F("No created_at"));
    return;
  }
  if (createdAt <= kind0CreatedAt) {
    Serial.println(F("Event is not newer than previous kind0 event"));
    return;
  }
  kind0CreatedAt = createdAt;

  JsonVariantConst content = kind0Doc[2]["content"];
  if (!content.is<const char*>()) {
    Serial.println(F("No content"));
    return;
  }

  Serial.println(F("parsing content"));

  StaticJsonDocument<4098> contentDoc;

  error = deserializeJson(contentDoc, content.as<const char*>());

  if (error) {
    Serial.print(F("content deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  JsonVariantConst lud16 = contentDoc["lud16"];
  if (!lud16.is<const char*>()) {
    Serial.println(F("No lud16"));
    return;
  }

  String lud16Str = String(lud16);

  Serial.print(F("lud16Str: "));
  Serial.println(lud16Str);

  int at_index = lud16Str.indexOf('@');
  if (at_index < 0) {
    Serial.println(F("No @ found in lud16"));
    return;
  }

  String username = lud16Str.substring(0, at_index);
  String domain = lud16Str.substring(at_index + 1);

  String newNostrWalletPubkey = getNostrWalletPubkey(domain, username);
  if (newNostrWalletPubkey.length() == 0) {
    Serial.println(F("No new wallet nostr pubkey, skipping"));
    return;
  }
  if (newNostrWalletPubkey == nostrWalletPubkey) {
    Serial.println(F("Nostr pubkey is the same as the previous wallet nostr pubkey"));
    return;
  }
  nostrWalletPubkey = newNostrWalletPubkey;

  Serial.print(F("New wallet nostr pubkey: "));
  Serial.println(nostrWalletPubkey);

  NostrRequestOptions* eventRequestOptions = new NostrRequestOptions();

  String authors[1];
  authors[0] = nostrWalletPubkey;
  eventRequestOptions->authors = authors;
  eventRequestOptions->authors_count = 1;

  int kinds[] = {9735};
  eventRequestOptions->kinds = kinds;
  eventRequestOptions->kinds_count = 1;

  String ps[] = {nostrRecipientPubkey};
  eventRequestOptions->p = ps;
  eventRequestOptions->p_count = 1;

  time_t now;
  time(&now);
  eventRequestOptions->since = now;

  Serial.print(F("Requesting zap receipt events since: "));
  Serial.println(now);
  nostrRelayManager.requestEvents(eventRequestOptions);
  delete eventRequestOptions;
}

void kind9735Event(const std::string& key, const char* payload) {
  Serial.print(F("Kind 9735 event. payload is: "));
  Serial.println(payload);
  StaticJsonDocument<4098> kind9735Doc;
  DeserializationError error = deserializeJson(kind9735Doc, payload);

  if (error) {
    Serial.print(F("kind9735Event: event deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  JsonVariantConst eventType = kind9735Doc[0];

  if (!eventType.is<const char*>() || strcmp(eventType, "EVENT") != 0) {
    Serial.println(F("kind9735Event: Not an event"));
    return;
  }

  JsonVariantConst authorPubkey = kind9735Doc[2]["pubkey"];
  if (!authorPubkey.is<const char*>()) {
    Serial.println(F("kind9735Event: No author pubkey"));
    return;
  }
  if (strcmp(authorPubkey, nostrWalletPubkey.c_str()) != 0) {
    Serial.println(F("kind9735Event: Author pubkey is not the wallet pubkey, skipping"));
    return;
  }

  JsonVariantConst tags = kind9735Doc[2]["tags"];
  if (!tags.is<JsonArrayConst>()) {
    Serial.println(F("kind9735Event: No tags"));
    return;
  }

  String bolt11Str = "";
  bool foundRecipient = false;
  bool foundSender = nostrSenderPubkey.length() == 0; // if empty, behave as if already found sender.
  bool foundTriggerId = niotTriggerId.length() == 0; // if empty, behave as if already found trigger id.
  bool foundNiotPrice = niotPrice.length() == 0; // if empty, behave as if already found price.
  bool foundNiotUnit = niotUnit.length() == 0; // if empty, behave as if already found unit.
  for (JsonVariantConst tag : tags.as<JsonArrayConst>()) {
    if (!tag[0].is<const char*>()) {
      continue;
    }
    if (strcmp(tag[0], "bolt11") == 0) {
      if (tag[1].is<const char*>()) {
        bolt11Str = String(tag[1]);
        bolt11Str.toLowerCase();
      }
    } else if (strcmp(tag[0], "p") == 0) {
      if (tag[1].is<const char*>() && (strcmp(tag[1], nostrRecipientPubkey.c_str()) == 0)) {
        foundRecipient = true;
      }
    } else if ((!foundTriggerId || !foundNiotPrice || !foundNiotUnit || !foundSender) && strcmp(tag[0], "description") == 0) {
      // Uppercase P should be the sender pubkey - but it's not always implemented.
      // The sender pubkey is always in the zap-request.
      if (tag[1].is<const char*>()) {
        StaticJsonDocument<4098> zapRequest;
        error = deserializeJson(zapRequest, tag[1].as<const char*>());
        // We don't verify the entire format & signature of the zap request.
        if (!error) {
          if (!foundSender && zapRequest["pubkey"].is<const char*>()) {
            Serial.print(F("kind9735Event: found sender pubkey: "));
            Serial.println(zapRequest["pubkey"].as<const char*>());
            foundSender = (strcmp(zapRequest["pubkey"], nostrSenderPubkey.c_str()) == 0);
          }
          if ((!foundTriggerId || !foundNiotPrice || !foundNiotUnit) && zapRequest["content"].is<const char*>()) {
            Serial.print(F("kind9735Event: zap content: "));
            Serial.println(zapRequest["content"].as<const char*>());
            StaticJsonDocument<200> zapRequestContent;
            error = deserializeJson(zapRequestContent, zapRequest["content"].as<const char*>());
            if (!error) {
              if (zapRequestContent["triggerId"].is<const char*>() && (strcmp(zapRequestContent["triggerId"], niotTriggerId.c_str()) == 0)) {
                foundTriggerId = true;
              }
              if (zapRequestContent["price"].is<const char*>() && (strcmp(zapRequestContent["price"], niotPrice.c_str()) == 0)) {
                foundNiotPrice = true;
              }
              if (zapRequestContent["unit"].is<const char*>() && (strcmp(zapRequestContent["unit"], niotUnit.c_str()) == 0)) {
                foundNiotUnit = true;
              }
            }
          }
        }
      }
    }
  }
  if (!foundRecipient) {
    Serial.println(F("kind9735Event: No recipient tag"));
    return;
  }
  if (!foundSender) {
    Serial.println(F("kind9735Event: No sender tag"));
    return;
  }
  if (!foundTriggerId) {
    Serial.println(F("kind9735Event: No nIoT trigger-id"));
    return;
  }
  if (!foundNiotPrice) {
    Serial.println(F("kind9735Event: No nIoT price"));
    return;
  }
  if (!foundNiotUnit) {
    Serial.println(F("kind9735Event: No nIoT price-unit"));
    return;
  }
  if (bolt11Str.length() == 0) {
    Serial.println(F("kind9735Event: No bolt11 tag"));
    return;
  }
  Serial.print(F("kind9735Event: bolt11 is: "));
  Serial.println(bolt11Str);
  if (!bolt11Str.startsWith("lnbc")) {
    Serial.println(F("kind9735Event: bolt11 does not start with lnbc"));
    return;
  }
  long amountMsats = 0;
  int bolt11Offset = 4;
  for (int bolt11Offset = 4; bolt11Offset < bolt11Str.length(); bolt11Offset++) {
    char c = bolt11Str[bolt11Offset];
    if (c >= '0' && c <= '9') {
      amountMsats = amountMsats * 10 + (c - '0');
      continue;
    }
    switch (c) {
      case 'm':
        amountMsats *= 100000000;
        break;
      case 'u':
        amountMsats *= 100000;
        break;
      case 'n':
        amountMsats *= 100;
        break;
      case 'p':
        amountMsats /= 10;
        break;
      default:
        Serial.print(F("kind9735Event: unknown character: "));
        Serial.println(c);
        amountMsats = 0;
        break;
    }
    break;
  }
  if (bolt11Offset == bolt11Str.length()) {
    Serial.println(F("kind9735Event: no amount unit found"));
    amountMsats = 0;
  }
  Serial.print(F("kind9735Event: amount is: "));
  Serial.println(amountMsats);
  if (amountMsats < nostrMinZap) {
    Serial.print(F("kind9735Event: amount is less than min zap: "));
    Serial.println(nostrMinZap);
    return;
  }

  JsonVariantConst createdAt = kind9735Doc[2]["created_at"];
  if (!createdAt.is<int>()) {
    Serial.println(F("kind9735Event: No created_at"));
    return;
  }
  if (createdAt <= kind9735CreatedAt) {
    Serial.println(F("kind9735Event: Event is not newer than previous kind9735 event"));
    return;
  }
  kind9735CreatedAt = createdAt;

  digitalWrite(pinNumber, HIGH);
  if (runtimeMs > 0) {
    delay(runtimeMs);
  }
  digitalWrite(pinNumber, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("setup"));

  preferences.begin("config", true);
  String nostrRelaysStr = preferences.getString("nostr_relays", "");
  Serial.print(F("Loaded nostr_relays: "));
  Serial.println(nostrRelaysStr);
  String nostrRecipientNpub = preferences.getString("recipient_npub", "");
  Serial.print(F("Loaded recipient_npub: "));
  Serial.println(nostrRecipientNpub);
  nostrMinZap = preferences.getULong("nostr_min_zap", 0);
  Serial.print(F("Loaded nostr_min_zap: "));
  Serial.println(nostrMinZap);
  String nostrSenderNpub = preferences.getString("sender_npub", "");
  Serial.print(F("Loaded sender_npub: "));
  Serial.println(nostrSenderNpub);
  niotTriggerId = preferences.getString("niot_trigger_id", "");
  Serial.print(F("Loaded niot_trigger_id: "));
  Serial.println(niotTriggerId);
  niotPrice = preferences.getString("niot_price", "");
  Serial.print(F("Loaded niot_price: "));
  Serial.println(niotPrice);
  niotUnit = preferences.getString("niot_unit", "");
  Serial.print(F("Loaded niot_inut: "));
  Serial.println(niotUnit);
  pinNumber = preferences.getUShort("pin_number", 13);
  Serial.print(F("Loaded pin_number: "));
  Serial.println(pinNumber);
  runtimeMs = preferences.getUInt("run_time", 5000);
  Serial.print(F("Loaded run_time: "));
  Serial.println(runtimeMs);
  preferences.end();

  wm_nostr_relays.setValue(nostrRelaysStr.c_str(), 200);
  wm_recipient_npub.setValue(nostrRecipientNpub.c_str(), 64);
  String nostrMinZapStr = String(nostrMinZap);
  wm_nostr_min_zap.setValue(nostrMinZapStr.c_str(), 19);
  wm_sender_npub.setValue(nostrSenderNpub.c_str(), 64);
  wm_niot_trigger_id.setValue(niotTriggerId.c_str(), 26);
  wm_niot_price.setValue(niotPrice.c_str(), 20);
  wm_niot_unit.setValue(niotUnit.c_str(), 10);
  String pinNumberStr = (pinNumber == INVALID_PIN_NUMBER) ? "" : String(pinNumber);
  wm_pin_number.setValue(pinNumberStr.c_str(), 2);
  String runtimeMsStr = String(runtimeMs);
  wm_run_time.setValue(runtimeMsStr.c_str(), 6);

  // Initialize WiFiManager
  WiFiManager wm;

  // Add custom parameters
  wm.addParameter(&wm_nostr_relays);
  wm.addParameter(&wm_recipient_npub);
  wm.addParameter(&wm_nostr_min_zap);
  wm.addParameter(&wm_sender_npub);
  wm.addParameter(&wm_niot_trigger_id);
  wm.addParameter(&wm_niot_price);
  wm.addParameter(&wm_niot_unit);
  wm.addParameter(&wm_pin_number);
  wm.addParameter(&wm_run_time);

  // Set timeout for configuration portal
  wm.setConfigPortalTimeout(180); // 3 minutes timeout
  wm.setConnectRetries(3);
  wm.setSaveParamsCallback(onSaveParams);

  // Attempt to connect; if it fails, start configuration portal
  char ssid[32];
  uint64_t chipid = ESP.getEfuseMac();
  snprintf(ssid, sizeof(ssid), "ZapWatcher %08X v%d", (uint32_t)(chipid), ZAPWATCHER_VERSION);
  if (!wm.autoConnect(ssid)) {
    Serial.println(F("Failed to connect or hit timeout. Restarting..."));
    delay(3000);
    ESP.restart();
    delay(5000);
    return;
  }
  if (savedNewParams) {
    Serial.println(F("Restarting to reload new params..."));
    delay(1000);
    ESP.restart();
    delay(5000);
    return;
  }
  delay(1000);
  configTime(0, 0, "pool.ntp.org");

  if (pinNumber == INVALID_PIN_NUMBER) {
    Serial.println(F("No pin number found, restarting..."));
    delay(1000);
    ESP.restart();
    return;
  }
  if (runtimeMs == 0) {
    Serial.println(F("No run time found, restarting..."));
    delay(1000);
    ESP.restart();
    return;
  }
  // Parsing values that are always saved as strings:
  nostrRecipientPubkey = npubToHex(nostrRecipientNpub);
  if (nostrRecipientPubkey.length() == 0) {
    Serial.println(F("No recipient pubkey hex found, restarting..."));
    delay(1000);
    ESP.restart();
    return;
  }
  Serial.print(F("nostrRecipientPubkey: "));
  Serial.println(nostrRecipientPubkey);

  if (nostrSenderNpub.length() > 0) {
    nostrSenderPubkey = npubToHex(nostrSenderNpub);
    Serial.print(F("nostrSenderPubkey: "));
    Serial.println(nostrSenderPubkey);
    if (nostrSenderPubkey.length() == 0) {
      Serial.println(F("No sender pubkey hex found, restarting..."));
      delay(1000);
      ESP.restart();
      delay(5000);
      return;
    }
  } else {
    nostrSenderPubkey = "";
  }
  Serial.print(F("nostrSenderPubkey: "));
  Serial.println(nostrSenderPubkey);

  Serial.print(F("niotTriggerId: "));
  Serial.println(niotTriggerId);
  Serial.print(F("niotPrice: "));
  Serial.println(niotPrice);
  Serial.print(F("niotUnit: "));
  Serial.println(niotUnit);

  // Split the string into a vector
  std::vector<String> nostrRelaysVector;
  int startIndex = 0;

  for (int i = 0; i <= nostrRelaysStr.length(); i++) {
    if (i == nostrRelaysStr.length() || nostrRelaysStr[i] == ' ') {
      if (startIndex < i) {
        nostrRelaysVector.push_back(nostrRelaysStr.substring(startIndex, i));
      }
      startIndex = i + 1;
    }
  }

  if (nostrRelaysVector.size() == 0) {
    Serial.println(F("No relays found, restarting..."));
    delay(1000);
    ESP.restart();
    delay(5000);
    return;
  }

  nostr.setLogging(true);
  nostrRelayManager.setRelays(nostrRelaysVector);
  int nostrRelaysVectorSize = nostrRelaysVector.size();
  nostrRelayManager.setMinRelaysAndTimeout(min(nostrRelaysVectorSize, MIN_RELAYS), 10000);

  Serial.println(F("Connecting to relays"));
  nostrRelayManager.setEventCallback("ok", okEvent);
  nostrRelayManager.setEventCallback(0, kind0Event);
  nostrRelayManager.setEventCallback(9735, kind9735Event);
  nostrRelayManager.connect();

  NostrRequestOptions* eventRequestOptions = new NostrRequestOptions();

  String authors[1];
  authors[0] = nostrRecipientPubkey;
  eventRequestOptions->authors = authors;
  eventRequestOptions->authors_count = 1;

  int kinds[] = {0};
  eventRequestOptions->kinds = kinds;
  eventRequestOptions->kinds_count = 1;

  Serial.println(F("Requesting events"));
  nostrRelayManager.requestEvents(eventRequestOptions);

  Serial.print(F("My IP address is: "));
  Serial.println(WiFi.localIP());

  delete eventRequestOptions;
  pinMode(pinNumber, OUTPUT);
  digitalWrite(pinNumber, LOW);
}

void loop() {
  // Retrieve stored parameters
  nostrRelayManager.loop();
  nostrRelayManager.broadcastEvents();
}
