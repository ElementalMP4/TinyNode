#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

String setup_page = "<!DOCTYPE html>\n\
<html>\n\
<head>\n\
    <title>TinyNode WiFi setup</title>\n\
</head>\n\
<body>\n\
    <h1>Connect to your WiFi network</h1>\n\
    <input type=\"text\" id=\"ssid-input\" placeholder=\"SSID\">\n\
    <input type=\"text\" id=\"password-input\" placeholder=\"Password\">\n\
    <button onclick=\"submitDetails()\">Submit</button>\n\
</body>\n\
<script>\n\
    function doHttpGet(url, callback) {\n\
        var xhr = new XMLHttpRequest();\n\
        xhr.onreadystatechange = function () {\n\
            if (xhr.readyState == 4) {\n\
                callback(xhr.status, xhr.responseText);\n\
            }\n\
        }\n\
        xhr.open(\"GET\", url, true);\n\
        xhr.send();\n\
    }\n\
    function submitDetails() {\n\
        let ssid = document.getElementById(\"ssid-input\").value;\n\
        let password = document.getElementById(\"password-input\").value;\n\
        doHttpGet(`/update-wifi?ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`, (code, response) => {\n\
            if (code !== 200) window.alert(`Error ${code} ${response}`);\n\
            else window.alert(\"Success! \" + response);\n\
        });\n\
    }\n\
</script>\n\
</html>";

int setupButton = 4;

LiquidCrystal_I2C lcd(0x27, 16, 2);

IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

const int outputs[] = { 5, 18, 19, 23 };
const int inputs[] = { 25, 26, 32, 33 };

int lastBounceTime = 0;
const int debouncePeriod = 200;

Preferences preferences;

AsyncWebServer server(80);

void setup() {
  lcd.init();
  lcd.backlight();

  preferences.begin("tiny-node", false);
  Serial.begin(115200);
  Serial.println("TinyNode - Booting up...");

  initialisePins();

  if (digitalRead(setupButton) == HIGH) {
    Serial.println("Setup button pressed");
    setupMode();
  } else {
    String ssid = preferences.getString("ssid");
    String password = preferences.getString("password");

    if (ssid == "" || password == "") {
      Serial.println("No credentials saved. Going into setup mode");
      setupMode();
    } else {
      businessAsUsual(ssid, password);
    }
  }
}

void initialisePins() {
  pinMode(setupButton, INPUT_PULLDOWN);

  for (int i = 0; i < 4; i++) {
    pinMode(outputs[i], OUTPUT);
    digitalWrite(outputs[i], HIGH);
  }

  for (int i = 0; i < 4; i++) {
    pinMode(inputs[i], INPUT_PULLDOWN);
  }

  Serial.println("Initialised IO pins");
}

void printLcdMessage(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void setupMode() {
  Serial.println("Welcome to setup mode!");
  printLcdMessage("Setup Mode", "-> TinyNode");
  WiFi.softAP("TinyNode", "123456789");
  WiFi.softAPConfig(local_ip, gateway, subnet);

  server.on("/", HTTP_GET, handle_setup_route);
  server.on("/update-wifi", HTTP_GET, handle_wifi_details_change_route);
  server.begin();
}

char *string2char(String input) {
  char *output = new char[input.length() + 1];
  memset(output, 0, input.length() + 1);

  for (int i = 0; i < input.length(); i++)
    output[i] = input.charAt(i);
  return output;
}

void businessAsUsual(String ssid, String password) {
  Serial.println("Connecting to WiFi");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);

  printLcdMessage("Connecting WiFi", ssid);
  WiFi.setHostname("TinyNode");
  WiFi.begin(string2char(ssid), string2char(password));

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
    Serial.print("Current status: ");
    Serial.println(WiFi.status());
  }

  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  printLcdMessage("WiFi Connected!", WiFi.localIP().toString());

  server.on("/detect", HTTP_GET, handle_detect_route);
  server.on("/toggle-switch", HTTP_GET, handle_switch_toggle_route);
  server.on("/set-switch", HTTP_GET, handle_switch_set_route);
  server.on("/status", HTTP_GET, handle_switch_state_route);
  server.on("/all-on", HTTP_GET, handle_all_on_route);
  server.on("/all-off", HTTP_GET, handle_all_off_route);

  server.begin();
  delay(3000);
  lcd.setCursor(0, 0);
  lcd.print("    TinyNode    ");
}

void loop() {
  for (int i = 0; i < 4; i++) {
    if (digitalRead(inputs[i])) {
      if ((millis() - lastBounceTime) > debouncePeriod) {
        digitalWrite(outputs[i], !digitalRead(outputs[i]));
        lastBounceTime = millis();
      }
      while (digitalRead(inputs[i])) {}
    }
  }
}

void handle_all_on_route(AsyncWebServerRequest *request) {
  updateAll(LOW);
  request->send(200, "text/html", "Turned all outputs on");
}

void handle_all_off_route(AsyncWebServerRequest *request) {
  updateAll(HIGH);
  request->send(200, "text/html", "Turned all outputs off");
}

void updateAll(bool state) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(outputs[i], state);
  }
}

void handle_switch_toggle_route(AsyncWebServerRequest *request) {
  if (!request->hasParam("switch")) {
    request->send(400, "text/html", "You must specify a switch");
  } else {
    String choice = request->getParam("switch")->value();
    if (choice == "") {
      request->send(400, "text/html", "Switch cannot be blank");
    } else {
      int switchChoice = choiceToInt(choice);
      if (switchChoice == -1) {
        request->send(400, "text/html", "Switch is invalid");
      } else {
        toggleSwitch(switchChoice);
        request->send(200, "text/html", "Switch Toggled");
      }
    }
  }
}

void handle_switch_state_route(AsyncWebServerRequest *request) {
  String stateString = "";
  for (int i = 0; i < 4; i++) {
    bool state = digitalRead(outputs[i]);
    stateString.concat(i + 1);
    stateString.concat("-");
    stateString.concat(!digitalRead(outputs[i]));
    if (i < 3)
      stateString.concat("|");
  }
  request->send(200, "text/html", stateString);
}

void handle_switch_set_route(AsyncWebServerRequest *request) {
  if (!request->hasParam("switch") || !request->hasParam("state")) {
    request->send(400, "text/html", "You must specify a switch and a state");
  } else {
    String choice = request->getParam("switch")->value();
    String state = request->getParam("state")->value();
    if (choice == "" || state == "") {
      request->send(400, "text/html", "Switch and state cannot be blank");
    } else {
      int switchChoice = choiceToInt(choice);
      bool stateInBooleanForm = stateToBool(state);
      if (switchChoice == -1) {
        request->send(400, "text/html", "Switch is invalid");
      } else {
        changeSwitch(switchChoice, stateInBooleanForm);
        request->send(200, "text/html", "Switch Toggled");
      }
    }
  }
}

bool stateToBool(String state) {
  return state == "off";
}

int choiceToInt(String choice) {
  if (choice == "1")
    return 0;
  if (choice == "2")
    return 1;
  if (choice == "3")
    return 2;
  if (choice == "4")
    return 3;
  return -1;
}

void toggleSwitch(int choice) {
  if (choice != -1) {
    bool state = false;
    state = !digitalRead(outputs[choice]);
    changeSwitch(choice, state);
  }
}

void changeSwitch(int choice, bool state) {
  digitalWrite(outputs[choice], state);
}

void handle_detect_route(AsyncWebServerRequest *request) {
  request->send(200, "text/html", "TinyNode");
}

void handle_setup_route(AsyncWebServerRequest *request) {
  request->send(200, "text/html", setup_page);
}

void handle_wifi_details_change_route(AsyncWebServerRequest *request) {
  if (!request->hasParam("ssid") || !request->hasParam("password")) {
    request->send(400, "text/html", "You must specify both an SSID and password");
  } else {
    String ssid = request->getParam("ssid")->value();
    String password = request->getParam("password")->value();
    if (ssid == "" || password == "") {
      request->send(400, "text/html", "SSID and password cannot be empty");
    } else {
      preferences.putString("ssid", ssid);
      preferences.putString("password", password);
      request->send(200, "text/html", "Rebooting in 5 seconds to implement changes");
      delay(5000);
      esp_restart();
    }
  }
}