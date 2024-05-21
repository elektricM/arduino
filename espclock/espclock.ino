////////////////////////////////////////////////////////////////////////
// Clock Project
// Baud: 115200
////////////////////////////////////////////////////////////////////////

#include <ESP8266WiFi.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h> // mDNS lib
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

/* Useful variables */
WiFiClient client;
const int led = 2;               // ESP8266 pin to which onboard LED is connected
int port[4] = {16, 14, 12, 13};  // ports used to control the stepper motor
#define MILLIS_PER_MIN 60000     // milliseconds per minute (60000 theoretically)
#define STEPS_PER_ROTATION 30720 // steps for a full turn of minute rotor 4096 * 90 / 12 = 30720

bool set_time_flag = false;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "fr.pool.ntp.org"); // Use French pool

// Set HTTP webserver port
WiFiServer server(80);

// Store HTTP request
String header;

// Auxiliar variables to store the current output state
bool init_clock_done = false;
// String output4State = "off";

// Current time
/* millis() reference: Returns the number of milliseconds passed
since the Arduino board began running the current program.
This number will overflow (go back to zero), after approximately 50 days. */
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

// LED blinking with millis (led variable defined earlier)
unsigned long previousMillis = 0; // will store last time LED was updated
const long interval = 5000;       // interval at which to blink (milliseconds)
int ledState = LOW;               // ledState used to set the LED

// wait for a single step of stepper
int delaytime = 2;

// sequence of stepper motor control
int seq[8][4] = {
    {LOW, HIGH, HIGH, LOW},
    {LOW, LOW, HIGH, LOW},
    {LOW, LOW, HIGH, HIGH},
    {LOW, LOW, LOW, HIGH},
    {HIGH, LOW, LOW, HIGH},
    {HIGH, LOW, LOW, LOW},
    {HIGH, HIGH, LOW, LOW},
    {LOW, HIGH, LOW, LOW}};

void approach()
{
  rotate(-20); // for approach run
  rotate(20);  // approach run without heavy load
}

void rotate(int step)
{
  static int phase = 0;
  int i, j;
  int delta = (step > 0) ? 1 : 7;
  int dt = 20;

  step = (step > 0) ? step : -step;
  for (j = 0; j < step; j++)
  {
    phase = (phase + delta) % 8;
    for (i = 0; i < 4; i++)
    {
      digitalWrite(port[i], seq[phase][i]);
    }
    delay(dt);
    if (dt > delaytime)
      dt--;
  }
  // power cut
  for (i = 0; i < 4; i++)
  {
    digitalWrite(port[i], LOW);
  }
}

void setup()
{
  pinMode(led, OUTPUT);

  for (int i = 0; i < 4; i++)
  {
    pinMode(port[i], OUTPUT);
  }

  Serial.begin(115200);
  Serial.println("Booting");
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.autoConnect("WiFi_Profs", "urm816UK"); // WARN: hardcoded :)

  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type); });
  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed"); });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP()); // http://wifi-clock.local/
  /*   curl \
    -d "SSH login: WiFi.localIP()" \
    ntfy.sh/wificlockprojetincroyable */

  int n = WiFi.scanNetworks(false, true);

  String ssid;
  uint8_t encryptionType;
  int32_t RSSI;
  uint8_t *BSSID;
  int32_t channel;
  bool isHidden;

  for (int i = 0; i < n; i++)
  {
    WiFi.getNetworkInfo(i, ssid, encryptionType, RSSI, BSSID, channel, isHidden);
    Serial.printf("%d: %s, Ch:%d (%ddBm) %s %s\n", i + 1, ssid.c_str(), channel, RSSI, encryptionType == ENC_TYPE_NONE ? "open" : "", isHidden ? "hidden" : "");
  }

  // get timezone from http://worldtimeapi.org/api/ip
  // raw_offset from json
  /*
    http.begin(client, "http://worldtimeapi.org/api/ip");
    http.addHeader("Content-Type", "text/plain");
    int httpCode = http.GET();
    if (httpCode == 200)
    {
      String jsonString = http.getString();
      DynamicJsonDocument jsonDoc(2048);
      deserializeJson(jsonDoc, jsonString);
      int raw_offset = jsonDoc["raw_offset"]; // TODO: check if this works
    }
  String payload = http.getString();
  Serial.println(httpCode);
  Serial.println(payload);
  http.end();
*/

  if (!MDNS.begin("wifi-clock"))
  { // Start the mDNS responder for esp8266.local
    Serial.println("Error setting up MDNS responder!");
  }

  // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +2 = 7200
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(7200); // GMT+2 (France)
  server.begin();

  HTTPClient http;
  http.begin(client, "http://ntfy.sh/wificlockprojetwow");
  http.addHeader("Content-Type", "text/plain");
  String message = "UP http://" + WiFi.localIP().toString(); // TODO: check if this works
  Serial.println(message);
  int httpCode = http.POST(message);
  String payload = http.getString();
  Serial.println(httpCode);
  Serial.println(payload);
  http.end();

  approach();
  // update_ntp_time();
}

/* Adjusts time in minutes */
void adjust_time(int val, bool increment = true)
{
  long pos;
  pos = (STEPS_PER_ROTATION * val) / 60;
  approach();
  if (increment)
  {
    rotate(pos);
  }
  else
  {
    rotate(-pos);
  }
}

void update_ntp_time()
{
  long calc_minutes = 0;
  timeClient.update();
  int currentHour = timeClient.getHours();
  Serial.print("Hour: ");
  Serial.println(currentHour);
  int currentMinute = timeClient.getMinutes();
  Serial.print("Minutes: ");
  Serial.println(currentMinute);
  if (currentHour >= 12)
  {
    currentHour = currentHour - 12;
  }
  if (currentHour <= 6)
  {
    calc_minutes = (currentHour * 60) + currentMinute;
    // calc_minutes = calc_minutes + (calc_minutes/60);
    adjust_time(calc_minutes, true);
  }
  if (currentHour > 6)
  {
    currentHour = currentHour - 6;
    calc_minutes = (currentHour * 60) + currentMinute;
    calc_minutes = 360 - calc_minutes;
    // calc_minutes = calc_minutes - (calc_minutes/60);
    adjust_time(calc_minutes, false);
  }
}

void loop()
{
  ArduinoOTA.handle();
  WiFiClient client = server.available(); // Listen for incoming clients

  if ((set_time_flag) && (init_clock_done == false))
  {
    update_ntp_time();
    init_clock_done = true;
    set_time_flag = false;
  }

  if (client)
  {                                // If a new client connects,
    Serial.println("New Client."); // print a message out in the serial port
    String currentLine = "";       // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime)
    { // loop while the client's connected
      currentTime = millis();
      if (client.available())
      {                         // if there's bytes to read from the client,
        char c = client.read(); // read a byte, then
        Serial.write(c);        // print it out the serial monitor
        header += c;
        if (c == '\n')
        { // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0)
          {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // turns the GPIOs on and off
            if (header.indexOf("GET /set_time") >= 0)
            {
              if (init_clock_done == false)
              {
                set_time_flag = true;
              }
            }
            else if (header.indexOf("GET /adjust_1_min") >= 0)
            {
              adjust_time(1, true);
            }
            else if (header.indexOf("GET /adjust_minus_1_min") >= 0)
            {
              adjust_time(1, false);
            }
            else if (header.indexOf("GET /adjust_1_hour") >= 0)
            {
              adjust_time(60, true);
            }
            else if (header.indexOf("GET /adjust_minus_1_hour") >= 0)
            {
              adjust_time(60, false);
            }
            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html {font-family: Verdana; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println("body {background-color: #303030}");
            client.println("h1 {color: white}");
            client.println("h3 {color: white}");
            client.println(".button {background-color: #3241A3; border: none; color: white; padding: 16px 40px; width: 350px; border-radius: 8px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #33CEFF; width: 350px; border-radius: 8px;}");
            client.println(".button3 {background-color: #723D9E; width: 350px; border-radius: 8px;}");
            client.println("</style></head>");

            // Web Page Heading
            client.println("<body><h1>Clock - Martin & JG</h1><br/>");
            client.println("<body><h3>Put the clock at 12'O clock position and press the SET TIME button to auto calibrate</h3>");
            if (init_clock_done == false)
            {
              client.println("<h3>Clock init completed succesfully!</h3>");
            }
            client.println("<p><a href=\"/set_time\"><button class=\"button\">Set time</button></a></p>");
            client.println("<p><a href=\"/adjust_1_min\"><button class=\"button button2\">Increment 1 min</button></a></p>");
            client.println("<p><a href=\"/adjust_minus_1_min\"><button class=\"button button3\">Decrement 1 min</button></a></p>");
            client.println("<p><a href=\"/adjust_1_hour\"><button class=\"button button2\">Increment 1 hour</button></a></p>");
            client.println("<p><a href=\"/adjust_minus_1_hour\"><button class=\"button button3\">Decrement 1 hour</button></a></p>");
            // Display current state, and ON/OFF buttons for GPIO 5
            // client.println("<p>GPIO 5 - State " + output5State + "</p>");
            // If the output5State is off, it displays the ON button
            // if (output5State=="off") {
            //  client.println("<p><a href=\"/5/on\"><button class=\"button\">ON</button></a></p>");
            //} else {
            //  client.println("<p><a href=\"/5/off\"><button class=\"button button2\">OFF</button></a></p>");
            //}

            // Display current state, and ON/OFF buttons for GPIO 4
            //            client.println("<p>GPIO 4 - State " + output4State + "</p>");
            //            // If the output4State is off, it displays the ON button
            //            if (output4State=="off") {
            //              client.println("<p><a href=\"/4/on\"><button class=\"button\">ON</button></a></p>");
            //            } else {
            //              client.println("<p><a href=\"/4/off\"><button class=\"button button2\">OFF</button></a></p>");
            //            }
            client.println("</body></html>");

            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          }
          else
          { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        }
        else if (c != '\r') // anything else but CR
        {
          currentLine += c; // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }

  // loop to blink without delay
  unsigned long currentMillis = millis();
  //  if (currentMillis - previousMillis >= interval) {
  //    digitalWrite(led,  ledState);
  //  }

  static long prev_min = 0, prev_pos = 0;
  long min;
  static long pos;

  min = millis() / MILLIS_PER_MIN;
  if (prev_min == min)
  {
    return;
  }
  prev_min = min;
  pos = (STEPS_PER_ROTATION * min) / 60;
  approach();
  if (pos - prev_pos > 0)
  {
    rotate(pos - prev_pos);
  }
  prev_pos = pos;
}
