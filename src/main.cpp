#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

// Access Point credentials (default values)
String ap_ssid = "ESP32_AP";
String ap_password = "12345678";

// File to store Wi-Fi credentials
const char* WIFI_CREDENTIALS_FILE = "/wifi_credentials.json";

ESP8266WebServer server(80);  // Create a web server on port 80

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Global variable to store the selected timezone
String currentTimezone = "Asia/Tehran"; // Default timezone

// Clock variables
int hours = 0;
int minutes = 0;
int seconds = 0;
unsigned long lastMillis = 0; // Tracks the last time update
unsigned long lastSync = 0;   // Tracks the last API sync

// Function to load saved credentials from LittleFS
bool loadCredentials(JsonDocument &doc) {
    File file = LittleFS.open(WIFI_CREDENTIALS_FILE, "r");
    if (file) {
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (!error) {
            return true;
        } else {
            Serial.println("Failed to parse Wi-Fi credentials file");
        }
    } else {
        Serial.println("No Wi-Fi credentials file found");
    }
    return false;
}

// Function to save credentials to LittleFS
bool saveCredentials(const JsonDocument &doc) {
    File file = LittleFS.open(WIFI_CREDENTIALS_FILE, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        return true;
    } else {
        Serial.println("Failed to save Wi-Fi credentials");
        return false;
    }
}

// Function to attempt auto-connect to known Wi-Fi networks
bool autoConnectToWiFi() {
    JsonDocument doc;
    if (!loadCredentials(doc)) {
        return false;  // No saved credentials
    }

    // Scan for available networks
    int numNetworks = WiFi.scanNetworks();
    if (numNetworks == 0) {
        Serial.println("No networks found");
        return false;
    }

    // Iterate through saved credentials and try to connect
    for (JsonPair kv : doc.as<JsonObject>()) {
        String ssid = kv.key().c_str();
        String password = kv.value().as<String>();

        for (int i = 0; i < numNetworks; i++) {
            if (WiFi.SSID(i) == ssid) {
                Serial.print("Attempting to connect to known network: ");
                Serial.println(ssid);

                WiFi.begin(ssid.c_str(), password.c_str());
                int attempts = 0;
                while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                    delay(500);
                    Serial.print(".");
                    attempts++;
                }

                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("\nConnected to " + ssid);
                    return true;
                } else {
                    Serial.println("\nFailed to connect to " + ssid);
                    break;
                }
            }
        }
    }

    Serial.println("No known networks found");
    return false;
}

// Handle Wi-Fi scan request
void handleScan() {
    int numNetworks = WiFi.scanNetworks();
    Serial.println("Scan complete");

    String json = "[";
    for (int i = 0; i < numNetworks; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
    }
    json += "]";

    server.send(200, "application/json", json);
}

// Handle connection request
void handleConnect() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("Connecting to ");
    Serial.println(ssid);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected to " + ssid);
        server.send(200, "text/plain", "Connected to " + ssid);

        // Save the new credentials
        JsonDocument doc;
        loadCredentials(doc);  // Pass the JsonDocument object
        doc[ssid] = password;  // Add new credentials
        saveCredentials(doc);   // Pass the JsonDocument object

        // Update the display
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 10);
        display.println("Connected to " + ssid);
        display.display();

        // Reload the HTML page to update all values
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
    } else {
        Serial.println("\nFailed to connect to " + ssid);
        server.send(500, "text/plain", "Failed to connect to " + ssid);
    }
}

// Handle update credentials request
void handleUpdate() {
    String new_ssid = server.arg("ssid");
    String new_password = server.arg("password");

    if (new_ssid.length() > 0 && new_password.length() > 0) {
        JsonDocument doc;
        loadCredentials(doc);       // Pass the JsonDocument object
        doc[new_ssid] = new_password;  // Add new credentials
        saveCredentials(doc);       // Pass the JsonDocument object

        server.send(200, "text/plain", "Credentials updated.");
    } else {
        server.send(400, "text/plain", "Invalid SSID or password");
    }
}

// Function to fetch time from API
bool fetchTime() {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure(); // Bypass SSL certificate validation (not recommended for production)

        HTTPClient http;
        String url = "https://timeapi.io/api/time/current/zone?timeZone=" + currentTimezone;

        if (http.begin(client, url)) {
            int httpCode = http.GET();

            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                http.end();

                // Parse the JSON response
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload);

                if (error) {
                    Serial.print("JSON parsing failed: ");
                    Serial.println(error.c_str());
                    return false;
                }

                // Extract the time
                hours = doc["hour"];
                minutes = doc["minute"];
                seconds = doc["seconds"];
                lastMillis = millis(); // Reset the millis counter
                lastSync = millis();   // Reset the sync timer
                return true;
            } else {
                http.end();
                Serial.print("HTTP request failed, error code: ");
                Serial.println(httpCode);
                return false;
            }
        } else {
            Serial.println("Failed to connect to server");
            return false;
        }
    } else {
        return false;
    }
}

// Function to update the OLED display with the current time and timezone
void updateDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Display the timezone
    display.setCursor(0, 0);
    display.println("Timezone: " + currentTimezone);

    // Display the current time
    display.setCursor(0, 20);
    display.println("Current Time:");

    char timeString[6];
    sprintf(timeString, "%02d:%02d", hours, minutes);
    display.setCursor(0, 30);
    display.println(timeString);

    display.display();
}

void setup() {
    Serial.begin(115200);

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("OLED not found"));
        while (true);
    }

    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return;
    }

    // Start Access Point (AP) mode
    WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
    Serial.println("Access Point Started");
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());

    // Attempt to auto-connect to known Wi-Fi networks
    JsonDocument doc;
    if (loadCredentials(doc)) {    // Pass the JsonDocument object
        if (autoConnectToWiFi()) {
            Serial.println("Connected to Wi-Fi");
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(0, 10);
            display.println("Connected to " + WiFi.SSID());
            display.display();

            // Sync time with API at startup
            if (fetchTime()) {
                Serial.println("Time synced with API");
            } else {
                Serial.println("Failed to sync time with API");
            }
        } else {
            Serial.println("No known networks found");
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(0, 10);
            display.println("No Internet");
            display.setCursor(0, 20);
            display.println("Connect to AP: " + ap_ssid);
            display.display();
        }
    } else {
        Serial.println("No saved credentials found");
    }

    // Serve the HTML file
    server.on("/", []() {
        File file = LittleFS.open("/dashboard.html", "r");
        if (!file) {
            server.send(500, "text/plain", "Failed to open file");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });

    // Handle Wi-Fi scan request
    server.on("/scan", handleScan);

    // Handle connection request
    server.on("/connect", handleConnect);

    // Handle update credentials request
    server.on("/update", HTTP_POST, handleUpdate);

    // New endpoint: Get connected SSID
    server.on("/get-connected-ssid", []() {
        if (WiFi.status() == WL_CONNECTED) {
            server.send(200, "text/plain", WiFi.SSID());
        } else {
            server.send(200, "text/plain", "");
        }
    });

    // New endpoint: Forget a network
    server.on("/forget", []() {
        String ssid = server.arg("ssid");
        JsonDocument doc;
        if (loadCredentials(doc)) {    // Pass the JsonDocument object
            doc.remove(ssid); // Remove the SSID from the credentials
            saveCredentials(doc);      // Pass the JsonDocument object
            server.send(200, "text/plain", "Network forgotten: " + ssid);
        } else {
            server.send(500, "text/plain", "Failed to forget network");
        }
    });

    // New endpoint: Set timezone
    server.on("/set-timezone", []() {
        String newTimezone = server.arg("timezone");
        if (newTimezone.length() > 0) {
            currentTimezone = newTimezone;

            // Immediately fetch and display the new time
            if (fetchTime()) {
                updateDisplay();
                server.send(200, "text/plain", "Timezone updated to " + currentTimezone);
            } else {
                server.send(500, "text/plain", "Failed to fetch time for " + currentTimezone);
            }
        } else {
            server.send(400, "text/plain", "Invalid timezone");
        }
    });

    // Start the server
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();  // Handle client requests

    // Update the clock using millis()
    unsigned long currentMillis = millis();
    unsigned long elapsedMillis = currentMillis - lastMillis;

    if (elapsedMillis >= 1000) { // Update every second
        seconds++;
        if (seconds >= 60) {
            seconds = 0;
            minutes++;
            if (minutes >= 60) {
                minutes = 0;
                hours++;
                if (hours >= 24) {
                    hours = 0;
                }
            }
        }
        lastMillis = currentMillis; // Reset the millis counter

        // Update the display
        updateDisplay();
    }

    // Re-sync with API every 1 minute
    if (currentMillis - lastSync >= 60000) { // 1 minute = 60,000 ms
        if (fetchTime()) {
            Serial.println("Re-synced time with API");
        } else {
            Serial.println("Failed to sync time with API");
        }
    }
}