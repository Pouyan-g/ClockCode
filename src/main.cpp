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
    DynamicJsonDocument doc(1024);
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
        DynamicJsonDocument doc(1024);
        loadCredentials(doc);  // Load existing credentials
        doc[ssid] = password;  // Add new credentials
        saveCredentials(doc);   // Save updated credentials

        // Update the display
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 10);
        display.println("Connected to " + ssid);
        display.display();
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
        DynamicJsonDocument doc(1024);
        loadCredentials(doc);       // Load existing credentials
        doc[new_ssid] = new_password;  // Add new credentials
        saveCredentials(doc);       // Save updated credentials

        server.send(200, "text/plain", "Credentials updated.");
    } else {
        server.send(400, "text/plain", "Invalid SSID or password");
    }
}

// Function to fetch time from API
bool fetchTime(int &hours, int &minutes, int &seconds) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        WiFiClientSecure client;  // Use WiFiClientSecure for HTTPS
        client.setInsecure();     // Bypass SSL certificate validation (not recommended for production)

        // Construct the API URL
        String url = "https://timeapi.io/api/time/current/zone?timeZone=Asia/Tehran";

        // Begin the HTTP request
        if (http.begin(client, url)) {
            int httpCode = http.GET();

            if (httpCode == HTTP_CODE_OK) {  // Check if the request was successful
                String payload = http.getString();
                http.end();

                // Parse the JSON response
                DynamicJsonDocument doc(512);  // Increase the size if needed
                DeserializationError error = deserializeJson(doc, payload);

                if (error) {
                    Serial.print("JSON parsing failed: ");
                    Serial.println(error.c_str());
                    return false;  // Return false on JSON error
                }

                // Extract the "hour", "minute", and "seconds" fields
                hours = doc["hour"];
                minutes = doc["minute"];
                seconds = doc["seconds"];
                return true;  // Return true on success
            } else {
                http.end();
                Serial.print("HTTP request failed, error code: ");
                Serial.println(httpCode);
                return false;  // Return false on HTTP error
            }
        } else {
            Serial.println("Failed to connect to server");
            return false;  // Return false on connection failure
        }
    } else {
        return false;  // Return false if not connected to Wi-Fi
    }
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
    if (autoConnectToWiFi()) {
        Serial.println("Connected to Wi-Fi");
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 10);
        display.println("Connected to " + WiFi.SSID());
        display.display();
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

    // Start the server
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();  // Handle client requests

    static unsigned long lastUpdate = 0;
    static int hours = 0, minutes = 0, seconds = 0;
    static unsigned long lastMillis = 0;
    static bool timeSynced = false;  // Track if time has been synced

    if (WiFi.status() == WL_CONNECTED) {
        if (!timeSynced || millis() - lastUpdate >= 60000) {  // Sync every 60 seconds
            if (fetchTime(hours, minutes, seconds)) {
                lastMillis = millis();  // Reset the millis counter
                timeSynced = true;      // Mark time as synced
                lastUpdate = millis();  // Reset the update timer
            }
        }

        // If time is not yet synced, show "Getting Time..."
        if (!timeSynced) {
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(0, 10);
            display.println("Getting Time...");
            display.display();
        } else {
            // Calculate the current time based on the last API update
            unsigned long elapsedMillis = millis() - lastMillis;
            int currentSeconds = seconds + (elapsedMillis / 1000);
            int currentMinutes = minutes + (currentSeconds / 60);
            int currentHours = hours + (currentMinutes / 60);

            // Normalize the time
            currentSeconds %= 60;
            currentMinutes %= 60;
            currentHours %= 24;

            // Format the updated time (only hours and minutes)
            char updatedTime[6];
            sprintf(updatedTime, "%02d:%02d", currentHours, currentMinutes);

            // Display the updated time
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(0, 10);
            display.println("Current Time:");
            display.setCursor(0, 20);
            display.println(updatedTime);
            display.display();
        }
    } else {
        // Display "No Internet" if not connected to Wi-Fi
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 10);
        display.println("No Internet");
        display.display();
    }
}