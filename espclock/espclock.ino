////////////////////////////////////////////////////////////////////////
// Clock Project
// Baud: 115200
// ---------------------
// Instructions:
// Connect to WLAN: AutoConnectAP
// Access http://wifi-clock.local/
////////////////////////////////////////////////////////////////////////

#include <ESP8266WiFi.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h> // mDNS lib
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

/* Useful variables */
const int led = 2;               // ESP8266 pin to which onboard LED is connected
int port[4] = {1, 3, 5, 4};      // ports used to control the stepper motor
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
boolean initClockDone = false;
String output4State = "off";

// Current time
/* millis() reference: Returns the number of milliseconds passed since the Arduino board began running the current program.
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

// update stepper motor phase helper
void _updateStepperPhase(int &phase, const bool clockwise)
{
  constexpr int kDeltaClockwise = 1;
  constexpr int kDeltaCounterClockwise = 7;
  constexpr int kMaxPhases = 8;

  phase = (clockwise) ? (phase + kDeltaClockwise) % kMaxPhases : (phase + kDeltaCounterClockwise) % kMaxPhases;
}

// turn off the stepper motor helper
void _powerCut(int port[], const size_t numPorts)
{
  for (size_t i = 0; i < numPorts; ++i)
  {
    digitalWrite(port[i], LOW);
  }
}

/**
 * Rotates the stepper motor by the specified amount of steps
 * Positive numbers indicate clockwise rotation, negative numbers counter-clockwise
 */
void rotate(int step)
{
  static int currentPhase = 0;
  const int directionFactor = (step > 0) ? 1 : -1;
  const int targetSteps = abs(step);
  const int initialDelay = 20;
  int delayDuration = initialDelay;

  // loop through the required number of steps
  for (int i = 0; i < targetSteps; ++i)
  {
    _updateStepperPhase(currentPhase, step > 0);

    for (size_t portIndex = 0; portIndex < 4; ++portIndex) // apply voltage
    {
      digitalWrite(port[portIndex], seq[currentPhase][portIndex]);
    }

    delay(delayDuration); // wait

    if (delayDuration > 1) // gradually decrease the wait period until reaching minimum allowed delay
    {
      --delayDuration;
    }
  }

  _powerCut(port, 4); // turn off the stepper motor once the rotation is complete
}

void approach()
{
  rotate(-20); // for approach run
  rotate(20);  // approach run without heavy load
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
  wifiManager.autoConnect("AutoConnectAP");

  ArduinoOTA.onStart([&]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }

    Serial.println("Start updating " + type); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     { ota_error(error); });

  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin("wifi-clock"))
  {
    Serial.println("Error setting up MDNS responder!");
  }

  timeClient.begin();
  timeClient.setTimeOffset(19800);
  server.begin();

  approach();
  // rotate(STEPS_PER_ROTATION / 60);
}

void increment_time(int val)
{
  long pos;
  pos = (STEPS_PER_ROTATION * val) / 60;
  approach();
  rotate(pos);
}

void decrement_time(int val)
{
  long pos;
  pos = (STEPS_PER_ROTATION * val) / 60;
  approach();
  rotate(-pos);
}

void update_ntp_time()
{
  long calculatedMinutes = 0;
  timeClient.update();

  int hours = timeClient.getHours();
  Serial.printf("Hour: %,d\n", hours);

  int minutes = timeClient.getMinutes();
  Serial.printf("Min: %,d\n", minutes);

  hours = (hours >= 12) ? (hours - 12) : hours;

  if (hours <= 6)
  {
    calculatedMinutes = (hours * 60) + minutes;
    increment_time(calculatedMinutes);
  }
  else if (hours > 6)
  {
    hours -= 6;
    calculatedMinutes = 360 - ((hours * 60) + minutes);
    decrement_time(calculatedMinutes);
  }
}
const char *ota_error_message(ota_error_t error)
{
  switch (error)
  {
  case OTA_AUTH_ERROR:
    return "Auth Failed";
  case OTA_BEGIN_ERROR:
    return "Begin Failed";
  case OTA_CONNECT_ERROR:
    return "Connect Failed";
  case OTA_RECEIVE_ERROR:
    return "Receive Failed";
  case OTA_END_ERROR:
    return "End Failed";
  default:
    return "";
  }
}

void ota_error(ota_error_t error)
{
  Serial.printf("Error[%u]: %s\n", error, ota_error_message(error));
}

/*
 * HTTP request handling to interact with the clock
 * TODO: make this more readable
 */
void handleHttpRequest(WifiClient &client)
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
            if (init_clock_done == "no")
            {
              set_time_flag = true;
            }
          }
          else if (header.indexOf("GET /adjust_1_min") >= 0)
          {
            increment_time(1);
          }
          else if (header.indexOf("GET /adjust_minus_1_min") >= 0)
          {
            increment_time(-1);
          }

          // Display the HTML web page
          client.println("<!DOCTYPE html><html>");
          client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
          client.println("<link rel=\"icon\" href=\"data:,\">");
          // CSS to style the on/off buttons
          // Feel free to change the background-color and font-size attributes to fit your preferences
          client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
          client.println(".button {background-color: #195B6A; border: none; color: white; padding: 16px 40px; width: 350px; border-radius: 8px;");
          client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
          client.println(".button2 {background-color: #195B6A; width: 350px; border-radius: 8px;}");
          client.println(".button3 {background-color: #195B6A; width: 350px; border-radius: 8px;}");
          client.println("</style></head>");

          // Web Page Heading
          client.println("<body><h1>keep clock at 12'O clock position and press SET TIME button to Auto calibrate</h1>");
          if (init_clock_done == "yes")
          {
            client.println("<p>Clock INIT - completed</p>");
          }
          client.println("<p><a href=\"/set_time\"><button class=\"button\">SET TIME</button></a></p>");
          client.println("<p><a href=\"/adjust_1_min\"><button class=\"button button2\">Increment 1 min</button></a></p>");
          client.println("<p><a href=\"/adjust_minus_1_min\"><button class=\"button button3\">Decrement 1 min</button></a></p>");
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
          break;
        }
        else
        { // if you got a newline, then clear currentLine
          currentLine = "";
        }
      }
      else if (c != '\r')
      {                   // if you got anything else but a carriage return character,
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

void blink()
{
  if (currentTime - previousMillis >= interval)
  {
    digitalWrite(led, ledState);
    ledState = !ledState; // toggle
    previousMillis = currentTime;
  }
}

void updateCurrentMinute()
{
  static long prev_min = 0, prev_pos = 0; // TODO: these variables seem unnecessary...?
  long min = updateCurrentMinuteHelper();
  long pos = calculatePosForMinute(min);
  approach();

  if (pos - prev_pos > 0)
  {
    rotate(pos - prev_pos);
  }

  prev_pos = pos;
}

long updateCurrentMinuteHelper()
{
  static long prev_min = 0;
  long min = millis() / MILLIS_PER_MIN;

  if (prev_min == min)
  {
    return prev_min;
  }

  prev_min = min;
  return min;
}

long calculatePosForMinute(long min)
{
  return (STEPS_PER_ROTATION * min) / 60;
}

/*
 * Main loop
 */
void loop()
{
  ArduinoOTA.handle();
  WiFiClient client = server.available(); // Listen for incoming clients

  if (set_time_flag && !init_clock_done)
  {
    update_ntp_time();
    init_clock_done = true;
    set_time_flag = false;
  }

  if (client)
  {
    handleHttpRequest(client);
  }

  blink();
  updateCurrentMinute();
}
