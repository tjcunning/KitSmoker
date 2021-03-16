/* Trevor Cunningham
 * Ian Mackie
 * Kevin Basham
 * CECS 490A/B - Fall 2020/Spring 2021
 * 
 * KIT Smoker IoT with ESP32, ATMega4809 and Android application.
 * This program is designed for use with the ESP32 (Hiletgo Node32 equivalent)
 * Use NodeMCU-32S board within Arduino IDE
 * It uses MQTT protocol to communicate with other publishers/subscribers of
 * the same topic
 * It communicates with the ATMega4809 via UART (Serial2)
 * It communicates with the serial terminal monitor via SPI (Serial)
 * 
 * MQTT Broker address: broker.hivemq.com
 * Port: 1883
 * 
 * 12.12.2020 - limited functionality and MQTT testing
 * 
 * When the ESP32 recieves the message 'turn led on' - it turns on the on-board blue LED
 * and publishes a confirmation message to the from_esp32 topic
 * 
 * When the ESP32 recieves the message 'turn led off' - it turns off the on-board blue LED
 * and publishes a confirmation message to the from_esp32 topic
 * 
 * Checking for new messages occurs every 0.5 seconds for quick response, but can easily be reduced if desired.
 * 
 * All messages received are printed to serial terminal for debugging purposes
 * 
 * 3.3.2021 - Adding MQTT topics and temperature storage arrays
 *  -LED function removed
 *  -Random values "test section" at bottom added for debugging MQTT and Android app
 *  -MQTT topics added for webpage simulation of ATM4809 for debugging
 *  -MQTT topics added for pump control, and data request for graph population
 *  -Existing MQTT topics changed to reflect new date and to be more consistent
 *  -Size of array that contains temp data has been made a global variable "windowSize" and can be set as desired
 * 
 * 
 * 
 * 
 * 
 * Required libraries:
 * Wifi.h
 * PubSubClient.h
 */
#include <PubSubClient.h>
#include <WiFi.h>
#include <SPI.h>
#include <HardwareSerial.h>
#include <stdio.h>
#include <stdlib.h>
#include "driver/uart.h"
#include "soc/uart_struct.h"
PubSubClient MQTT_CLIENT;
WiFiClient WIFI_CLIENT;

#define LED 2  //On-board LED

// MQTT Topics
const char* NEED_GRAPH_DATA = "490B2021/iot/phone/need_data";
const char* ESP32_MEAT_TEMP = "490B2021/iot/esp32/meat";
const char* ESP32_INTERNAL_TEMP = "490B2021/iot/esp32/internal";
const char* APP_PUMP = "490B2021/iot/app/pump";
const char* APP_SET_INTERNAL = "490B2021/iot/app/internal";
const char* APP_SET_MEAT = "490B2021/iot/app/meat";
const char* WEB_INTERNAL_TEMP = "490B2021/iot/web/internal";
const char* WEB_MEAT_TEMP = "490B2021/iot/web/meat";

// WiFi network info.
const char* ssid = "net_rpi";
const char* password = "2smartpigs";

// Misc. variables
const int windowSize = 50;
bool mqttConnected = false;
bool validData = false;
char temp[5];
char controlChar = 0;
char inter;
String temp_smoker[50];
String temp_meat[50];
unsigned int smoker_index = 0;
unsigned int meat_index = 0;
WiFiServer server(80);
//HardwareSerial MySerial(1);

/*
 * Setup function connects to Wi-Fi.
 * SSID and local IP address are printed to serial monitor
 */
void setup() {
  temp[4] = '\n';
  
  // Initialize the serial port
  Serial.begin(115200);
  
  //MySerial.begin(115200, SERIAL_8N1, 16, 17);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);


   // For removing buffer - let's tinker with this later.
  /*
  uart_intr_config_t uart_intr;
  uart_intr.intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M
                             | UART_RXFIFO_TOUT_INT_ENA_M
                             | UART_FRM_ERR_INT_ENA_M
                             | UART_RXFIFO_OVF_INT_ENA_M
                             | UART_BRK_DET_INT_ENA_M
                             | UART_PARITY_ERR_INT_ENA_M;
  uart_intr.rxfifo_full_thresh = 1; //UART_FULL_THRESH_DEFAULT,  //120 default!! aghh! need receive 120 chars before we see them
  //uart_intr.rx_timeout_thresh = 10; //UART_TOUT_THRESH_DEFAULT,  //10 works well for my short messages I need send/receive
  uart_intr.txfifo_empty_intr_thresh = 10; //UART_EMPTY_THRESH_DEFAULT
  uart_intr_config((uart_port_t)1, &uart_intr);
*/

  // Print activity...
  delay(10);
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  // Attempt to connect to a specific access point
  WiFi.begin(ssid, password);

  // Keep checking the connection status until it is connected
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
  }
  
  // Print the IP address once connected
  Serial.print("Connected to: ");
  Serial.print(ssid);
  Serial.print(" with IP address: ");
  Serial.println(WiFi.localIP());
}


/**************************************************
 * Add a temperature value to the meat temp array
 *************************************************/
void addMeatValue(const String value)
{
  if(!(meat_index == (windowSize - 1)))
  {
      temp_meat[meat_index] = value;
      ++meat_index;
  }
  else
  {
    for(int i = 0; i < (windowSize - 1); ++i) 
      temp_meat[i] = temp_meat[i + 1];    // Shift array values to left if full
    temp_meat[windowSize - 1] = value;    // append new value to last position in array
  }
}


/**************************************************
 * Add a temperature value to the smoker temp array
 *************************************************/
void addSmokerValue(const String value)
{
  if(!(smoker_index == (windowSize - 1)))
  {
      temp_smoker[smoker_index] = value;
      ++smoker_index;
  }
  else
  {
    for(int i = 0; i < (windowSize - 1); ++i)
      temp_smoker[i] = temp_smoker[i + 1];
    temp_smoker[windowSize - 1] = value;
  }  
}


/*************************************************************************
 * Handles message arrival from MQTT.
 * Determines which topic the message pertains to and reacts accordingly.
 ************************************************************************/
void myMessageArrived(char* topic, byte* payload, unsigned int length) {
  // Convert the message payload from bytes to a string
  String message = "";

  // Convert message into char array (string).
  for (unsigned int i=0; i< length; i++) {
    message = message + (char)payload[i];
  }
  

  /**************************************************
  / Pump signal from app
  **************************************************/
  if((String)topic == APP_PUMP)
  {
    Serial.println("Turn the pump on briefly");
    Serial2.write("{P}");
    //MySerial.write("{P}");
  }

   /**************************************************
   * Set smoker temp from app
   **************************************************/
  else if((String)topic == APP_SET_INTERNAL)
  {
    Serial.println("Set internal temperature to: " + message);
    Serial2.print(message);
    //MySerial.write(message);
  }

   /**************************************************
   * Set desired meat temp from app
   **************************************************/
  else if((String)topic == APP_SET_MEAT)
  {
    Serial.println("Set desired meat temperature to: " + message);
    //Serial2.print(message);
  }

  /********************************************************************
   * If the app opens the graph, entire contents of arrays will be
   * published to MQTT. Note: smoker_index will be equivalent to 
   * meat_index.
   *******************************************************************/
  else if((String)topic == NEED_GRAPH_DATA)
  { 
    for(int i = 0; i <= smoker_index; ++i)
    {
      MQTT_CLIENT.publish(ESP32_MEAT_TEMP, (char*)temp_meat[i].c_str());
      MQTT_CLIENT.publish(ESP32_INTERNAL_TEMP, (char*)temp_smoker[i].c_str());
    }
  }
  
  /*******************************************************
   * Used for web simulation of temp values
   ******************************************************/
  else if((String)topic == WEB_MEAT_TEMP)
  {
    MQTT_CLIENT.publish(ESP32_MEAT_TEMP, (char*)message.c_str());
    addMeatValue(message);  
  }

  // Smoker temp value from website simulator
  else if((String)topic == WEB_INTERNAL_TEMP)
  {
    Serial.println("Got internal");
    MQTT_CLIENT.publish(ESP32_INTERNAL_TEMP, (char*)message.c_str());
    addSmokerValue(message);  
  }
  
  // Unknown topic
  else
    Serial.println("Unknown topic");
} //end myMessageArrived function

/***********************************************************************
 * reconnect() connects to the MQTT broker and subscribes to all topics.
 * Connection status is displayed in serial monitor.
 **********************************************************************/
// This function connects to the MQTT broker
void reconnect() {
  // Set our MQTT broker address and port
  MQTT_CLIENT.setServer("broker.hivemq.com", 1883);
  MQTT_CLIENT.setClient(WIFI_CLIENT);

  // Loop until we're reconnected
  while (!MQTT_CLIENT.connected()) {
    // Attempt to connect
    mqttConnected = false;
    Serial.println("Attempting to connect to MQTT broker");
    MQTT_CLIENT.connect("esp32_490B2021");

    // Wait some time to space out connection requests
    delay(3000);
  }

  // Only send successful connection message one time
  if(MQTT_CLIENT.connected() && !mqttConnected)
  {
    Serial.println("Established connection with MQTT Broker");
    mqttConnected = true;
  }

  // Subscribe to the topics where the smart phone is publishing information
  MQTT_CLIENT.subscribe(APP_SET_INTERNAL);
  MQTT_CLIENT.subscribe(APP_SET_MEAT);
  MQTT_CLIENT.subscribe(APP_PUMP);
  MQTT_CLIENT.subscribe(NEED_GRAPH_DATA);
  MQTT_CLIENT.subscribe(WEB_INTERNAL_TEMP);
  MQTT_CLIENT.subscribe(WEB_MEAT_TEMP);
  
  // Set the message received callback
  MQTT_CLIENT.setCallback(myMessageArrived);
}

/*
 * Main superloop
 */
void loop() {
  
  // Check if we're connected to the MQTT broker
  // reconnect if not.
  if (!MQTT_CLIENT.connected()) {
    // If we're not, attempt to reconnect
    Serial.println("MQTT has dropped connection");
    reconnect();
  }
  
  // Delay (in ms)
  // Needs to be implemented as an interrupt rather than delay at some point...
  //delay(1000);  // How long to wait to check for new temp data?

  /*
   * Collect data from ATMega4809
   * Current solution is brute forcing it.
   * Lots of garbage coming through on Serial2.
   * First we check for framing character '{'
   * If we get that, gather 4 next characters into MQTT message and publish it to Android APP
   * Otherwise, ignore the garbage data
   */
  if(Serial2.available() > 0)
  {
    //Serial.print((String)Serial2.available() + " byte(s) are available on Serial2: ");
    //temp[0] = Serial2.read();                                                   // Store current character in temp[0]
    controlChar = Serial2.read();
    //Serial.println(temp[0]);
    if(controlChar == '{')                                                        // Check for framing character
    {
      for(int i = 0; i < 3; ++i)                                                  // Collect data if framing character found - ONLY 4 DIGITS!
      {
        inter = Serial2.read();                                                   // Read data into intermediate variable 
        if(inter != '{')                                                          // Little extra check to ensure that we are not printing things we shoulding
        {
          temp[i] = inter;                                                        // If we havent wrapped around to a control char, add to buffer
        }  
        // Maybe add an else->break statement here to handle smaller payloads?
      }
      validData = true;                                                           // Set boolean value to true once data has been acquired                                                        
    }

    if(validData)                                                                 // If valid data has been received, publish to MQTT and append to array
    {
      MQTT_CLIENT.publish(ESP32_INTERNAL_TEMP, temp);
      addSmokerValue(String(temp));
      //Serial.println("Received message from ATMega...");
      //Serial.println("Printing " + String(temp) + " to MQTT");                  // Print to serial terminal for debugging
      //MySerial.write('A');                                                      // Send confirmation message back to Mega
      validData = false;                                                          // Reset boolean value to false for next valid data acquisition  
    }                                                                              
  }

   /*
    * Test section. Randomly generated numbers are published to
    * ESP32 MQTT topics to be viewed on the smart phone application.
    * For debugging puposes.
    // Generate a random number for internal and meat temp
    // Number should be to one decimal
    
   float randNumber1 = random(100, 225);
   float randNumber2 = random(80, 130);
   float dec = (float)(random(0, 10)) / 10;
   randNumber1 = randNumber1 + dec;
   randNumber2 = randNumber2 + dec;
   String randMessage1 = String(randNumber1);
   String randMessage2 = String(randNumber2);

   // Message must be formatted as a character array
   randMessage1.toCharArray(message, randMessage1.length());
   MQTT_CLIENT.publish(ESP32_INTERNAL_TEMP, message);
   addSmokerValue((String)message);
  randMessage2.toCharArray(message, randMessage2.length());
   MQTT_CLIENT.publish(ESP32_MEAT_TEMP, message);
   addMeatValue((String)message);
   /*
    * End test setion
    */


    /*******************************************************
     * This checks to see if there are new messages received
     * via MQTT
     *******************************************************/
    MQTT_CLIENT.loop();

}
