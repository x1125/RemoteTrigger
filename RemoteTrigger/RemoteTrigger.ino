#include <cstring>
#include <WiFi.h>
#include "esp_http_server.h"
#include "config.h"

#define PRINT_BUFSIZE 128

#if defined(ARDUINO_XIAO_ESP32S3)
const int relayPin = D0;
const int buttonPin = D1;
#else
const int relayPin = 1;
const int buttonPin = 2;
#endif
bool has_serial = false;
int buttonState = 0;

httpd_handle_t httpd = NULL;

// Read and print current power state
static esp_err_t status_handler(httpd_req_t *req)
{
  buttonState = digitalRead(buttonPin);
  httpd_resp_set_type(req, "text/plain");
  char buf[2];
  sprintf(buf, "%i", buttonState);
  return httpd_resp_send(req, (const char *)buf, strlen(buf));
}

// Toggle power button (1 sec)
static esp_err_t power_handler(httpd_req_t *req)
{
  digitalWrite(relayPin, HIGH);
  delay(1000);
  digitalWrite(relayPin, LOW);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, NULL, 0);
}

// Force power off (5 sec)
static esp_err_t hold_handler(httpd_req_t *req)
{
  digitalWrite(relayPin, HIGH);
  delay(5000);
  digitalWrite(relayPin, LOW);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, NULL, 0);
}

// Try to attach serial console for 5 seconds
// Otherwise sets has_serial false
void try_serial() {
  Serial.begin(115200);
  int i = 0;
  while(!Serial) {
    i++;
    delay(1000);
    if (i > 5) {
      return;
    }
  }
  has_serial = true;
  Serial.setDebugOutput(true);
  print("");
}

// A wrapper around Serial.print that can to proper string formatting
// Respects has_serial state
void print(const char* format, ...) {
  if (!has_serial) {
    return;
  }

  va_list args; 
  va_start(args, format);
  
  char output[PRINT_BUFSIZE] = {0};
  vsnprintf(output, PRINT_BUFSIZE, format, args);

  Serial.print(output);

  if (strlen(output) == (PRINT_BUFSIZE - 1)) {
    Serial.print("[TRUNCATED]\n");
  }

  va_end(args);
}

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 4;

  httpd_uri_t status_uri = {
      .uri = "/status",
      .method = HTTP_GET,
      .handler = status_handler,
      .user_ctx = NULL
  };

  httpd_uri_t power_uri = {
      .uri = "/power",
      .method = HTTP_POST,
      .handler = power_handler,
      .user_ctx = NULL
  };

  httpd_uri_t hold_uri = {
      .uri = "/hold",
      .method = HTTP_POST,
      .handler = hold_handler,
      .user_ctx = NULL
  };

  print("Starting web server on port: %u\n", config.server_port);
  if (httpd_start(&httpd, &config) == ESP_OK)
  {
      httpd_register_uri_handler(httpd, &status_uri);
      httpd_register_uri_handler(httpd, &power_uri);
      httpd_register_uri_handler(httpd, &hold_uri);
  }
}

void ensure_wifi_connected() {
  // reconnecting is weird. it takes 35 seconds or so
  // see: https://github.com/espressif/arduino-esp32/issues/653#issuecomment-778561856
  if (WiFi.status() != WL_CONNECTED) {
    print("WiFi not connected. Connecting...\n");

    int i=0;
    while (WiFi.status() != WL_CONNECTED) {
      i++;
      delay(2000);
      print(".");
      if (i >= 10) {
        i = 1;
        print("\nConnection status: ");
        switch (WiFi.status()) {
          case WL_IDLE_STATUS:
            print("idle");
            break;

          case WL_NO_SSID_AVAIL:
            print("no ssid avail");
            break;

          case WL_SCAN_COMPLETED:
            print("scan completed");
            break;

          case WL_CONNECTED:
            print("connected");
            break;

          case WL_CONNECT_FAILED:
            print("connect failed");
            break;

          case WL_CONNECTION_LOST:
            print("connection lost");
            break;

          case WL_DISCONNECTED:
            print("disconnected");
            break;

          default:
            print("unknown");
            break;
        }
        print("\n");
        WiFi.reconnect();
      }
    }

    IPAddress ip = WiFi.localIP();
    print("\nWiFi connected (IP: %u.%u.%u.%u)\n", ip[0], ip[1], ip[2], ip[3]);
  }
}

void setup() {
  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  buttonState = digitalRead(buttonPin);

  try_serial();

#ifdef LOCAL_IP
  IPAddress local_IP(LOCAL_IP);
  IPAddress gateway(GATEWAY);
  IPAddress subnet(SUBNET);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    print("STA Failed to configure\n");
  }
#endif

  WiFi.begin(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    print(".");
  }
  // webserver wont start without initial wifi connection
  ensure_wifi_connected();
  startWebServer();
}

void loop() {
  ensure_wifi_connected();
  delay(10000);
}
