/*
 Anemometer using an ESP-01 and hall effect sensor without using interrupts

 To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"

*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Update these with values suitable for your network.
const char* ssid = "Winterfell";
const char* password = "ThereIsNoKnowledgeThatIsNotPower";
const char* mqtt_server = "192.168.1.10";

// anemometer stuff
const int RecordTime = 30; // Define Measuring Time (Seconds)
const int SensorPin = 14;  // Define Interrupt Pin (GPIO14, hacked pin on ESP8266-01)
//const float fudge = 2.72;  // multiply wind result by this for calibration
const float fudge = 5.5; // just guessing, previous was too low compared to weather service reports
                        // even tho 2.72 is from 25mph calibration, I think the wifi being on eats
                        // cycles and makes it too low (which wasn't on during calibration)
                        // - could also be from just not being up high enough
int readCounter = 0;
int new_read = 0;
int prev_read = 0;
int avgCount = 0;
int lastAvg = 0;
float cur_windspeed;
float current_readings[10];
float hourly_readings[12];
float daily_readings[24];
float hourly_peak = 0.0;
float prev_hourly_peak = 0.0;
bool new_hourly_hit = false;
bool new_daily_hit = false;
float hour_avg = 0.0;
//float prev_hour_avg = 0.0;
float daily_avg = 0.0;
float daily_peak = 0.0;
int half_min_counter = 0;
int hour_counter = 0;

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
char msg[50];

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  //Serial.println();
  //Serial.print("Connecting to ");
  //Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    //Serial.print(".");
  }

  randomSeed(micros());

  //Serial.println("");
  //Serial.println("WiFi connected");
  //Serial.println("IP address: ");
  //Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {

  // not using to receive and act upon messages currently - possible future functionality
  /* 
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }
  */
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    //Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      //Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("sensor/wind/status", "Sensor START");
      // ... and resubscribe
      // NOT USING CURRENTLY
      //client.subscribe("inTopic");
    } else {
      //Serial.print("failed, rc=");
      //Serial.print(client.state());
      //Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  //pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  pinMode(SensorPin, INPUT_PULLUP);
  //Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void measure() {
  
  new_read = digitalRead(SensorPin);

  if (prev_read != new_read) {
    if(new_read) {
      readCounter++;
    }
    prev_read = new_read;
  }
}

float windspeed(int count) {
  int rpm = count * (60 / RecordTime);
  // wind speed (m/s) = (2 * pi * radius) * (RPM / 60)
  // radius of cups is 12.45 cm
  // mutiply by 2.237 to go from meters/sec to mph
  // fudge is a multiplier to calibrate this, since it's way off
  return (2.0 * 3.14158 * 0.1245) * (rpm / 60.0) * 2.237 * fudge;
}

void loop() {

  measure();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if ((now - lastMsg) > (RecordTime * 1000)) { // 30 sex

    lastMsg = now;
    cur_windspeed = windspeed(readCounter);
    readCounter = 0;
    // just for testing
    //cur_windspeed = random(10, 60) / 3.0;    
    if (cur_windspeed > hourly_peak) {
      hourly_peak = cur_windspeed;
      new_hourly_hit = true;
    }
    if (cur_windspeed > daily_peak) {
      daily_peak = cur_windspeed;
      new_daily_hit = true;
    }

    // send MQTT message every min instead of 30 secs
    if (half_min_counter % 2 == 0) {
      snprintf (msg, 50, "%.1f", cur_windspeed, true);
      //Serial.print("Wind Speed: ");
      //Serial.println(msg);
      client.publish("sensor/wind/speed", msg, true);
    }
    
    current_readings[half_min_counter] = cur_windspeed;

    half_min_counter++;
    if (half_min_counter > 9) { // 5 mins
      half_min_counter = 0;

      // add to hourly readings
      //hourly_readings[avgCount] = cur_windspeed;

      // get average of current readings and add to hourly
      float current_avg = 0.0;
      for (int i = 0; i < 10; i++) {
        current_avg += current_readings[i];
      }
      current_avg /= 10;
      hourly_readings[avgCount] = current_avg;

      // get and announce current hourly avg
      hour_avg = 0;
      for (int i = 0; i < (avgCount + 1); i++) {
        hour_avg += hourly_readings[i];
      }
      hour_avg /= (avgCount + 1);
      snprintf (msg, 50, "%.1f", hour_avg);
      //Serial.print("Hour avg: ");
      //Serial.println(msg);
      client.publish("sensor/wind/hour", msg, true);

      // do hourly high announcement if record broken
      if (new_hourly_hit) {
        new_hourly_hit = false;
        snprintf (msg, 50, "%.1f", hourly_peak);
        //Serial.print("Hourly high: ");
        //Serial.println(msg);
        client.publish("sensor/wind/hourhigh", msg, true);
      }
      
      // do daily high announcement if record broken
      if (new_daily_hit) {
        new_daily_hit = false;
        snprintf (msg, 50, "%.1f", daily_peak);
        //Serial.print("Daily high: ");
        //Serial.println(msg);
        client.publish("sensor/wind/dayhigh", msg, true);
      }
    
      avgCount++;
      if (avgCount > 11) { // hourly
        avgCount = 0;
        
        /* 
        // moved to 5 mins interval
        // add to hourly avg
        prev_hour_avg = hour_avg;
        hour_avg = 0;
        for (int i = 0; i < 12; i++) {
          hour_avg += hourly_readings[i];
        }
        hour_avg /= 12;

        // send MQTT messages
        snprintf (msg, 50, "%.1f", hour_avg);
        //Serial.print("Hour avg: ");
        //Serial.println(msg);
        client.publish("sensor/wind/hour", msg, true);
        snprintf (msg, 50, "%.1f", prev_hour_avg);
        */
        snprintf (msg, 50, "%.1f", hour_avg); // use last calculated hour avg for prev hour
        //Serial.print("Prev hour avg: ");
        //Serial.println(msg);
        client.publish("sensor/wind/prevhour", msg, true);
        snprintf (msg, 50, "%.1f", hourly_peak);
        //Serial.print("Hourly high: ");
        //Serial.println(msg);
        client.publish("sensor/wind/hourhigh", msg, true);
        snprintf (msg, 50, "%.1f", prev_hourly_peak);
        //Serial.print("Prev hour high: ");
        //Serial.println(msg);
        client.publish("sensor/wind/prevhourhigh", msg, true);
        snprintf (msg, 50, "%.1f", daily_peak);
        //Serial.print("Daily high: ");
        //Serial.println(msg);
        client.publish("sensor/wind/dayhigh", msg, true);

        // add to daily readings, daily avg and report
        daily_readings[hour_counter] = hour_avg;
        daily_avg = 0;
        for (int i = 0; i < (hour_counter + 1); i++) {
          daily_avg += daily_readings[i];
        }
        daily_avg /= (hour_counter + 1);
        snprintf (msg, 50, "%.1f", daily_avg);
        client.publish("sensor/wind/daily", msg, true);

        hour_counter++;
        if (hour_counter > 23) { // daily - record ave and peak to prev daily
          hour_counter = 0;

          // send MQTT message
          snprintf (msg, 50, "%.1f", daily_avg);
          //Serial.print("Prev daily: ");
          //Serial.println(msg);
          client.publish("sensor/wind/prevdaily", msg, true);
          snprintf (msg, 50, "%.1f", daily_peak);
          //Serial.print("Prev high: ");
          //Serial.println(msg);
          client.publish("sensor/wind/prevhigh", msg, true);
          daily_peak = 0.0;
        }
        prev_hourly_peak = hourly_peak;
        hourly_peak = 0.0;
      }   
    }
  }
}