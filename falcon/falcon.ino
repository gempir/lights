#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include "settings.h"

const char *client_id = "falcon"; // Must be unique on the MQTT network

// Topics
const char *light_state_topic = "home/falcon";
const char *light_set_topic = "home/falcon/set";

const char *on_cmd = "ON";
const char *off_cmd = "OFF";

const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);

// Maintained state for reporting to HA
byte red = 255;
byte green = 255;
byte blue = 255;
byte brightness = 255;

//Your amount of LEDs to be wrote, and their configuration for FastLED
#define NUM_LEDS 5
#define DATA_PIN 5
//#define CLOCK_PIN 5
#define CHIPSET WS2812B
#define COLOR_ORDER GRB

// Real values to write to the LEDs (ex. including brightness and state)
byte realRed = 0;
byte realGreen = 0;
byte realBlue = 0;

bool stateOn = false;

// Globals for fade/transitions
bool startFade = false;
unsigned long lastLoop = 0;
int transitionTime = 0;
bool inFade = false;
int loopCount = 0;
int stepR, stepG, stepB;
int redVal, grnVal, bluVal;

// Globals for flash
bool flash = false;
bool startFlash = false;
int flashLength = 0;
unsigned long flashStartTime = 0;
byte flashRed = red;
byte flashGreen = green;
byte flashBlue = blue;
byte flashBrightness = brightness;

WiFiClient espClient;
PubSubClient client(espClient);
CRGB leds[NUM_LEDS];

void setup()
{
    //  pinMode(LED_BUILTIN, OUTPUT);
    //  digitalWrite(LED_BUILTIN, LOW);

    FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    Serial.begin(115200);
    setup_wifi();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

void setup_wifi()
{

    delay(10);
    // We start by connecting to a WiFi network
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

/*
  SAMPLE PAYLOAD:
  {
    "brightness": 120,
    "color": {
      "r": 255,
      "g": 100,
      "b": 100
    },
    "flash": 2,
    "transition": 5,
    "state": "ON"
  }
*/
void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    char message[length + 1];
    for (int i = 0; i < length; i++)
    {
        message[i] = (char)payload[i];
    }
    message[length] = '\0';
    Serial.println(message);

    if (!processJson(message))
    {
        return;
    }

    if (stateOn)
    {
        // Update lights
        realRed = map(red, 0, 255, 0, brightness);
        realGreen = map(green, 0, 255, 0, brightness);
        realBlue = map(blue, 0, 255, 0, brightness);
    }
    else
    {
        realRed = 0;
        realGreen = 0;
        realBlue = 0;
    }

    startFade = true;
    inFade = false; // Kill the current fade

    sendState();
}

bool processJson(char *message)
{
    StaticJsonDocument<BUFFER_SIZE> json;

    auto error = deserializeJson(json, message);

    if (error)
    {
        Serial.println("deserializeJson() failed");
        return false;
    }

    if (json.containsKey("state"))
    {
        if (strcmp(json["state"], on_cmd) == 0)
        {
            stateOn = true;
        }
        else if (strcmp(json["state"], off_cmd) == 0)
        {
            stateOn = false;
        }
    }

    // If "flash" is included, treat RGB and brightness differently
    if (json.containsKey("flash"))
    {
        flashLength = (int)json["flash"] * 1000;

        if (json.containsKey("brightness"))
        {
            flashBrightness = json["brightness"];
        }
        else
        {
            flashBrightness = brightness;
        }

        if (json.containsKey("color"))
        {
            flashRed = json["color"]["r"];
            flashGreen = json["color"]["g"];
            flashBlue = json["color"]["b"];
        }
        else
        {
            flashRed = red;
            flashGreen = green;
            flashBlue = blue;
        }

        flashRed = map(flashRed, 0, 255, 0, flashBrightness);
        flashGreen = map(flashGreen, 0, 255, 0, flashBrightness);
        flashBlue = map(flashBlue, 0, 255, 0, flashBrightness);

        flash = true;
        startFlash = true;
    }
    else
    { // Not flashing
        flash = false;

        if (json.containsKey("color"))
        {
            red = json["color"]["r"];
            green = json["color"]["g"];
            blue = json["color"]["b"];
        }

        if (json.containsKey("brightness"))
        {
            brightness = json["brightness"];
        }

        if (json.containsKey("transition"))
        {
            transitionTime = json["transition"];
        }
        else
        {
            transitionTime = 0;
        }
    }

    return true;
}

void sendState()
{
    DynamicJsonDocument json(BUFFER_SIZE);

    json["state"] = (stateOn) ? on_cmd : off_cmd;
    JsonObject color = json.createNestedObject("color");
    color["r"] = red;
    color["g"] = green;
    color["b"] = blue;

    json["brightness"] = brightness;

    char buffer[BUFFER_SIZE];
    serializeJson(json, buffer);

    client.publish(light_state_topic, buffer, true);
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect(client_id, mqtt_username, mqtt_password))
        {
            Serial.println("connected");
            client.subscribe(light_set_topic);
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void setColor(int inR, int inG, int inB)
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i].red = inR;
        leds[i].green = inG;
        leds[i].blue = inB;
    }
    FastLED.show();

    Serial.println("Setting LEDs:");
    Serial.print("r: ");
    Serial.print(inR);
    Serial.print(", g: ");
    Serial.print(inG);
    Serial.print(", b: ");
    Serial.println(inB);
}

void loop()
{

    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    if (flash)
    {
        if (startFlash)
        {
            startFlash = false;
            flashStartTime = millis();
        }

        if ((millis() - flashStartTime) <= flashLength)
        {
            if ((millis() - flashStartTime) % 1000 <= 500)
            {
                setColor(flashRed, flashGreen, flashBlue);
            }
            else
            {
                setColor(0, 0, 0);
                // If you'd prefer the flashing to happen "on top of"
                // the current color, uncomment the next line.
                // setColor(realRed, realGreen, realBlue);
            }
        }
        else
        {
            flash = false;
            setColor(realRed, realGreen, realBlue);
        }
    }

    if (startFade)
    {
        // If we don't want to fade, skip it.
        if (transitionTime == 0)
        {
            setColor(realRed, realGreen, realBlue);

            redVal = realRed;
            grnVal = realGreen;
            bluVal = realBlue;

            startFade = false;
        }
        else
        {
            loopCount = 0;
            stepR = calculateStep(redVal, realRed);
            stepG = calculateStep(grnVal, realGreen);
            stepB = calculateStep(bluVal, realBlue);

            inFade = true;
        }
    }

    if (inFade)
    {
        startFade = false;
        unsigned long now = millis();
        if (now - lastLoop > transitionTime)
        {
            if (loopCount <= 1020)
            {
                lastLoop = now;

                redVal = calculateVal(stepR, redVal, loopCount);
                grnVal = calculateVal(stepG, grnVal, loopCount);
                bluVal = calculateVal(stepB, bluVal, loopCount);

                setColor(redVal, grnVal, bluVal); // Write current values to LED pins

                Serial.print("Loop count: ");
                Serial.println(loopCount);
                loopCount++;
            }
            else
            {
                inFade = false;
            }
        }
    }
}

// From https://www.arduino.cc/en/Tutorial/ColorCrossfader
/* BELOW THIS LINE IS THE MATH -- YOU SHOULDN'T NEED TO CHANGE THIS FOR THE BASICS

  The program works like this:
  Imagine a crossfade that moves the red LED from 0-10,
    the green from 0-5, and the blue from 10 to 7, in
    ten steps.
    We'd want to count the 10 steps and increase or
    decrease color values in evenly stepped increments.
    Imagine a + indicates raising a value by 1, and a -
    equals lowering it. Our 10 step fade would look like:

    1 2 3 4 5 6 7 8 9 10
  R + + + + + + + + + +
  G   +   +   +   +   +
  B     -     -     -

  The red rises from 0 to 10 in ten steps, the green from
  0-5 in 5 steps, and the blue falls from 10 to 7 in three steps.

  In the real program, the color percentages are converted to
  0-255 values, and there are 1020 steps (255*4).

  To figure out how big a step there should be between one up- or
  down-tick of one of the LED values, we call calculateStep(),
  which calculates the absolute gap between the start and end values,
  and then divides that gap by 1020 to determine the size of the step
  between adjustments in the value.
*/
int calculateStep(int prevValue, int endValue)
{
    int step = endValue - prevValue; // What's the overall gap?
    if (step)
    {                       // If its non-zero,
        step = 1020 / step; //   divide by 1020
    }

    return step;
}

/* The next function is calculateVal. When the loop value, i,
   reaches the step size appropriate for one of the
   colors, it increases or decreases the value of that color by 1.
   (R, G, and B are each calculated separately.)
*/
int calculateVal(int step, int val, int i)
{
    if ((step) && i % step == 0)
    { // If step is non-zero and its time to change a value,
        if (step > 0)
        { //   increment the value if step is positive...
            val += 1;
        }
        else if (step < 0)
        { //   ...or decrement it if step is negative
            val -= 1;
        }
    }

    // Defensive driving: make sure val stays in the range 0-255
    if (val > 255)
    {
        val = 255;
    }
    else if (val < 0)
    {
        val = 0;
    }

    return val;
}
