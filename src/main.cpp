#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// Access Point credentials (default values)
String ap_ssid = "ESP32_AP";
String ap_password = "12345678";

ESP8266WebServer server(80);  // Create a web server on port 80

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Function to load saved credentials from LittleFS
void loadCredentials() {
    File file = LittleFS.open("/credentials.txt", "r");
    if (file) {
        ap_ssid = file.readStringUntil('\n');
        ap_password = file.readStringUntil('\n');
        file.close();
        ap_ssid.trim();  // Remove any extra whitespace
        ap_password.trim();
    }
}

// Function to save credentials to LittleFS
void saveCredentials(String ssid, String password) {
    File file = LittleFS.open("/credentials.txt", "w");
    if (file) {
        file.println(ssid);
        file.println(password);
        file.close();
    }
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
        saveCredentials(new_ssid, new_password);
        ap_ssid = new_ssid;
        ap_password = new_password;

        // Restart the AP with the new credentials
        WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
        server.send(200, "text/plain", "Credentials updated. AP restarted with new SSID: " + ap_ssid);
    } else {
        server.send(400, "text/plain", "Invalid SSID or password");
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

    // Load saved credentials
    loadCredentials();

    // Set up Access Point
    WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
    Serial.println("Access Point Started");
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 10);
    display.println("FEED ME MOMMY");
    display.display();

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
}