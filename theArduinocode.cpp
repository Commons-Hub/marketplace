#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <SPI.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

// ---- Wi-Fi Configuration ----
const char* ssid = "Commons Hub Team";         // REPLACE WITH YOUR WIFI SSID
const char* password = "password"; // REPLACE WITH YOUR WIFI PASSWORD

// ---- Glitch Server Configuration ----
const char* glitchHost = "and1plus.glitch.me"; // REPLACE with your Glitch project name
const char* apiEndpoint = "/api/tap"; // The endpoint on your Glitch server

// ---- MFRC522 RFID Reader Pin Configuration ----
MFRC522DriverPinSimple ss_pin(D4); // Using D4 (GPIO2) as SS for MFRC522

// IMPORTANT: For this code to compile, CONNECT THE RST PIN OF YOUR MFRC522 TO 3.3V ON YOUR NODEMCU.
// We are no longer using a GPIO to control the RST pin programmatically.
MFRC522DriverSPI driver{ss_pin, SPI};
MFRC522 mfrc522{driver};

// Network clients
WiFiClientSecure client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000); // Adjust UTC offset as needed for your timezone

// Function to convert byte array UID to a hexadecimal string
String uidToHexString(byte *buffer, byte bufferSize) {
  String hexString = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) {
      hexString += "0"; // Pad with leading zero if byte is single digit hex
    }
    hexString += String(buffer[i], HEX);
  }
  hexString.toUpperCase(); // Convert to uppercase for consistency
  return hexString;
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println(F("\n--- NodeMCU RFID Counter Starting ---"));

  // Connect to Wi-Fi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int connectAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && connectAttempts < 40) {
    delay(500);
    Serial.print(".");
    connectAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // --- NTP Time Synchronization ---
    Serial.println("Synchronizing time with NTP...");
    timeClient.begin();
    unsigned long startMillis = millis();
    bool timeSynced = false;
    while(!timeClient.update() && (millis() - startMillis < 15000)) {
       Serial.print(".");
       delay(500);
    }
    if (timeClient.update()) {
      Serial.println("\nTime synchronized. Current time: " + timeClient.getFormattedTime());
      timeSynced = true;
    } else {
      Serial.println("\nFailed to synchronize time with NTP. HTTPS might be unreliable.");
    }

    // --- DNS Resolution Check ---
    IPAddress resolvedIp;
    Serial.print("Attempting to resolve Glitch host: ");
    Serial.println(glitchHost);
    if (WiFi.hostByName(glitchHost, resolvedIp)) {
      Serial.print("Resolved Glitch host IP: ");
      Serial.println(resolvedIp);
    } else {
      Serial.println("DNS resolution for Glitch host FAILED!");
      Serial.print("Resolved IP (should be 0.0.0.0 on failure): ");
      Serial.println(resolvedIp);
      Serial.println("Check your 'glitchHost' spelling and network DNS settings.");
      // If DNS fails, HTTP/HTTPS will not work. Consider an infinite loop here or a reset.
    }

  } else {
    Serial.println("\nFailed to connect to WiFi. Please check SSID/password and try again.");
    Serial.println("Restarting NodeMCU in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  client.setInsecure(); // This allows HTTPS connections to self-signed or non-verified certificates.
                        // In a real application, you might want to remove this and properly validate certificates.
  Serial.println("WiFiClientSecure set to insecure mode.");

  SPI.begin();
  Serial.println("SPI bus initialized.");

  mfrc522.PCD_Init();
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);
  Serial.println(F("MFRC522 PCD_Init completed."));
  Serial.println(F("Ready to scan PICC..."));
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  Serial.println("\n--- Card Detected! ---");
  Serial.println("PICC_IsNewCardPresent detected!");

  if (!mfrc522.PICC_ReadCardSerial()) {
    Serial.println("PICC_ReadCardSerial failed (card might have moved or be invalid).");
    return;
  }

  // Convert UID to Hex String
  String cardUid = uidToHexString(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.print("Card UID: ");
  Serial.println(cardUid);

  Serial.println("Card tapped! Sending UID to Glitch server...");

  HTTPClient http;
  String serverPath = "https://" + String(glitchHost) + String(apiEndpoint);

  Serial.print("Attempting HTTP POST to: ");
  Serial.println(serverPath);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Skipping HTTP POST.");
    return;
  }

  http.begin(client, serverPath);
  http.addHeader("Content-Type", "application/json");

  // Create JSON payload with the UID
  String httpRequestData = "{\"uid\":\"" + cardUid + "\"}";
  Serial.print("Sending JSON: ");
  Serial.println(httpRequestData);

  int httpResponseCode = http.POST(httpRequestData);

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String payload = http.getString();
    Serial.println("Server Response: " + payload);
  } else {
    Serial.print("HTTP Error code: ");
    Serial.println(httpResponseCode);
    // Use the generic error string provided by the library, which is generally sufficient
    Serial.println("HTTP Error message: " + http.errorToString(httpResponseCode));
  }

  http.end();

  mfrc522.PICC_HaltA(); // Halt PICC (effectively de-select the card)
  delay(2000); // Small delay to prevent rapid, multiple reads of the same card
}