/* watering 1 and 2 (one after the other) for respective given time
//watering is started when ESP is powered on
//ESP goes to sleep after watering is finished --> reboot or repower ESP to start again
//ESP wakes up after defined time (e.g. 12 hours) when not powered down
//ESP sends mail if the barrol is empty
*/

/* Disclaimer
  Rui Santos
  Complete project details at:
   - ESP32: https://RandomNerdTutorials.com/esp32-send-email-smtp-server-arduino-ide/
   - ESP8266: https://RandomNerdTutorials.com/esp8266-nodemcu-send-email-smtp-server-arduino/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
  Example adapted from: https://github.com/mobizt/ESP-Mail-Client
*/

#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include <EEPROM.h>
#include <credentials.h> //in this file credentials for wifi and E-mail are stored
//you can either define them below directly in the code or create an own credentials.h file

// define pins
#define pump 21
//watering circuit #1
#define valve1 23
#define sensor1 34
//watering circuit #2
#define valve2 32

//sensor to check if the barrol is empty
#define fillsensor 15//4

//manually start the watering (if ESP is not a sleep, so far only used for debugging)
#define Taster1 12//12
int Taster1Pushed = 0; 


//declare some variable
int debug = 1; //set debug level; 1== print additional serial messages
//moisture sensor (optional, not yet realized)
int minMoisture = 2343; // value for a completely try sensor (in air)
int maxMoisture = 321; // sensor in water
int moisture1 = 0; // raw measurement value of sensor 1

//define watering times for automatic mode
int autoTime1 = 20;//120; //default values, later on read from EEPROM
int autoTime2 = 25;//75; // time in seconds

//define watering times for manual mode
int manualTime1 = 0;//120; //default values, later on read from EEPROM
int manualTime2 = 0;//75; // time in seconds

int wateringTime1 = autoTime1;//use either auto values or values from manual mode
int wateringTime2 = autoTime2;

int wateringActive = 0; //indicator if watering is currently running or stopped
double wateringStartTime = 0; //store time when watering started

double wakeUpTimeHour = 0.005; // wakeup for next watering after XX hours
double wakeUpTimeMin = wakeUpTimeHour*60; //time in min between two waterings
int uSecToSec = 1000000;

int wifiConnected = 0; //indicator if connection to WIFI was succesful(1) or not(0)
int wsConnected = 0;

int barrolEmpty =0;

int eeprom_size = 3;

int TimeoutWifi = 30; //time in s which is waited until shutdown in case of inactivity (web inputs are than still possible)
int shutdownTimer = 0;

//define Wifi and E-Mail credentials
/* are currently read from credentials.h
#define ssid "XXX"
#define wifi_password "XXX"
#define author_email "XXX@gmail.com"
#define author_password "XXX"
#define recipient_email "XXX@gmail.com"
#define recipient_email2 "XXX@gmail.com"
*/

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

/* The SMTP Session object used for Email sending */
SMTPSession smtp;
/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status);

//initialize websocket connection
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

//define fixed IP adress of ESP
IPAddress local_IP(192, 168, 0, 88);
//it wil set the gateway static IP address 
IPAddress gateway(192, 168, 0, 1);

// Following three settings are optional
IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(8, 8, 8, 8); 
IPAddress secondaryDNS(8, 8, 4, 4);

AsyncWebSocketClient * globalClient = NULL;

 
 /*--------------------------------------------------------------
   setup block
   --------------------------------------------------------------*/

void setup() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_21);
  gpio_hold_dis(GPIO_NUM_23);
  gpio_hold_dis(GPIO_NUM_32);
  
  Serial.begin(115200);

  //define pin modes
  pinMode(pump, OUTPUT);
  pinMode(valve1, OUTPUT);
  pinMode(sensor1, INPUT);

  pinMode(valve2, OUTPUT);
  
  digitalWrite(pump, LOW);
  digitalWrite(valve1, LOW);
  digitalWrite(valve2, LOW);

  //pinMode(Taster1, INPUT_PULLUP);
  //attachInterrupt(Taster1, Taster1Function, FALLING);

  pinMode(fillsensor, INPUT_PULLUP);

  
  /*--------------------------------------------------------------
  setup Wifi connection
   --------------------------------------------------------------*/
  pinMode(LED_BUILTIN,OUTPUT); // used as indicator for WIFI connection
   WiFi.begin(ssid, wifi_password);
  int i = 0; 
  //abort trying to connect to WIFI after 5 sec
  //if WIFI is not available (location on balkony;)), E-Mail service won't be used
  while ( WiFi.status() != WL_CONNECTED && i<5) {
    delay (500);
    Serial.print ( "." );
    i++;
  }
  Serial.println("");
  if(WiFi.status() == WL_CONNECTED){
    wifiConnected = 1;
    digitalWrite(LED_BUILTIN,HIGH);
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println();
  }else{
    Serial.println("WIFI connection failed; continuing without internet connection.");
  }

  /*--------------------------------------------------------------
   setup Mail server connection 
   --------------------------------------------------------------*/

    smtp.debug(0);
  
    /* Set the callback function to get the sending results */
    smtp.callback(smtpCallback);
  
    /* Declare the session config data */
    ESP_Mail_Session session;
  
    /* Set the session config */
    session.server.host_name = SMTP_HOST;
    session.server.port = SMTP_PORT;
    session.login.email = author_email;
    session.login.password = author_password;
    session.login.user_domain = "";
  
    /* Declare the message class */
    SMTP_Message message;
  
    /* Set the message headers */
    message.sender.name = "ESP";
    message.sender.email = author_email;
    message.subject = "Fass bitte auffüllen!!";
    message.addRecipient("ESP", recipient_email);
    message.addRecipient("ESP", recipient_email2);
  
    /*Send HTML message*/
    String htmlMsg = "<div style=\"color:#2f4468;\"><h1>Das Fass ist leer! ;)</h1><p>- bitte nachfüllen - Gießen ist deaktiviert</p></div>";
    message.html.content = htmlMsg.c_str();
    message.html.content = htmlMsg.c_str();
    message.text.charSet = "us-ascii";
    message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  
    /* Connect to server with the session config */
    if(wifiConnected == 1){
      if (!smtp.connect(&session))
        return;
    }

 /*--------------------------------------------------------------
   start websocket und server and file system
   --------------------------------------------------------------*/
  if(!SPIFFS.begin()){
     Serial.println("An Error has occurred while mounting SPIFFS");
     return;
  }

 
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
 
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/watering_system.html", "text/html");
  });


  server.begin();


  /*--------------------------------------------------------------
   start EEPROM and read values
   --------------------------------------------------------------*/
 EEPROM.begin(eeprom_size);
 
 autoTime1 = EEPROM.read(0);
 autoTime2 = EEPROM.read(1);
 wakeUpTimeHour = EEPROM.read(2);

 if(debug == 1){
 Serial.println("values:");
 Serial.println(autoTime1);
 Serial.println(autoTime2);
 Serial.println(wakeUpTimeHour);
 }
 wakeUpTimeMin = wakeUpTimeHour*60; //time in min between two waterings
 

 /*--------------------------------------------------------------
   measure if barrol is empty or not
   --------------------------------------------------------------*/

  barrolEmpty = digitalRead(fillsensor);
  //Serial.print("Barrol Empty: ");
  //Serial.println(barrolEmpty);
  
  //start watering when ESP is botted
  if(barrolEmpty==1){
    Serial.println("Barrol is empty, watering was not started");
    // send E-Mail notification that the barrol is empty
    if(wifiConnected == 1){
      /* Start sending Email and close the session */
      if (!MailClient.sendMail(&smtp, &message))
        Serial.println("Error sending Email, " + smtp.errorReason());
      //shutdown ESP abnd reboot after given interval (check again if barrel is filled)
    }
    delay(30000);//wait for 30s in case a ws connection should be established
    goToSleep();
  }else{
    //start automatic mode
    Serial.println("Barrol is filled, watering starts");
    wateringActive = 1;
    wateringStartTime = millis();
    wateringTime1 = autoTime1;
    wateringTime2 = autoTime2;
  }
}



 /*--------------------------------------------------------------
   loop 
   --------------------------------------------------------------*/

void loop() {
  if(wateringActive==1){
     int wateringPeriod = (millis() - wateringStartTime)/1000; //time since watering started in s
     if(debug == 1){
      Serial.print("watering active for " );
      Serial.println(wateringPeriod);
     }
     if(wateringPeriod>0 && wateringPeriod <= wateringTime1){
      digitalWrite(pump,HIGH);
      digitalWrite(valve1,HIGH);
      digitalWrite(valve2,LOW);
     }else if(wateringPeriod>wateringTime1 && wateringPeriod <= (wateringTime2+wateringTime1)){
      digitalWrite(pump,HIGH);
      digitalWrite(valve1,LOW);
      digitalWrite(valve2,HIGH);
     }else if(wateringPeriod>(wateringTime2+wateringTime1)){
      digitalWrite(pump,LOW);
      digitalWrite(valve1,LOW);
      digitalWrite(valve2,LOW);
      wateringActive = 0;
     }
  }
  //shutdown ESP if watering is not active and websocketr is not connected
  if(wateringActive == 0){
    if(wsConnected ==0){
      goToSleep();
    }
  }
  
  delay(1000);
}

 /*--------------------------------------------------------------
   define necesarry functions
   --------------------------------------------------------------*/

void wateringFunction(){
  Serial.println("Pump started");
    digitalWrite(pump, HIGH);
    digitalWrite(valve1, HIGH);
    Serial.println("valve 1 switched on");
    delay(autoTime1 * 1000);
    digitalWrite(valve1, LOW);
    digitalWrite(valve2, HIGH);
    Serial.println("valve 2 switched on");
    delay(autoTime2 * 1000);
    digitalWrite(pump,LOW);
    digitalWrite(valve1, LOW);
    digitalWrite(valve2, LOW);
    Serial.println("Pump stopped");
    Taster1Pushed = 0;
}

void goToSleep(){
  digitalWrite(pump,LOW);
  digitalWrite(valve1,LOW);
  digitalWrite(valve2,LOW);
  Serial.print("Minutes until wake up: ");
  Serial.println(wakeUpTimeMin);
  uint64_t wakeUpTimeUS = wakeUpTimeMin*60*uSecToSec;
  esp_sleep_enable_timer_wakeup(1ULL * wakeUpTimeMin * 60*uSecToSec);
  //send ESP to deep sleep (configure ports to stay low before
  Serial.println("Go to sleep"); 
  //ensure pins stay low in deep sleep (both FET and both Hogwarts LEDs)
  gpio_hold_en(GPIO_NUM_21);
  gpio_hold_en(GPIO_NUM_23);
  gpio_hold_en(GPIO_NUM_32);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
  Serial.println("ESP is sleeping");//won't be printed if every worked out
}

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status){
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()){
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failled: %d\n", status.failedCount());
    Serial.println("----------------\n");
    struct tm dt;

    for (size_t i = 0; i < smtp.sendingResult.size(); i++){
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients);
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject);
    }
    Serial.println("----------------\n");
  }
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
 
  if(type == WS_EVT_CONNECT){
    
    Serial.println("Websocket client connection received");
    wsConnected = 1;
    globalClient = client;
    if(debug==1){
    //ledcWrite(PWM_channel4,255);
    }
    //send initially set data to web client
    String value1 = String (autoTime1);
    while(value1.length()<3){
        value1 = '0' + value1;
      }
    String value2 = String (autoTime2);
    while(value2.length()<3){
        value2 = '0' + value2;
      }

    String value3 = String (int(wakeUpTimeHour));
    while(value3.length()<3){
        value3 = '0' + value3;
      }
    String barrel = String(barrolEmpty);
     globalClient->text(value1 + '_' + value2 + '_' + value3 + '_' + barrel);//delay(1000);
  } else if(type == WS_EVT_DISCONNECT){
 
    Serial.println("Websocket client connection finished");
    wsConnected = 0;
    globalClient = NULL;
    if(debug==1){
   // ledcWrite(PWM_channel4,12);
    }
  }
   else if(type == WS_EVT_DATA){
   Serial.println("Data Received");
   String dummy_data = (char*) data;
   dummy_data = dummy_data.substring(0,len);
   Serial.println(dummy_data); 
   
     if (dummy_data.indexOf("stop") >= 0){
      wateringActive = 0;
      digitalWrite(pump,LOW);
      digitalWrite(valve1,LOW);
      digitalWrite(valve2,LOW);
     }
     if (dummy_data.indexOf("stdby") >= 0){
      goToSleep();
     }
   
     if (dummy_data.indexOf("auto") >= 0)
    {
      Serial.println("auto message detected");
      String buffer_data = dummy_data.substring(dummy_data.indexOf("_")+1,dummy_data.indexOf("*"));
      Serial.println(buffer_data);
      int value1 = buffer_data.toInt();
      buffer_data = dummy_data.substring(dummy_data.indexOf("*")+1,dummy_data.indexOf("/"));
      Serial.println(buffer_data);
      int value2 = buffer_data.toInt();
      buffer_data = dummy_data.substring(dummy_data.indexOf("/")+1);
      Serial.println(buffer_data);
      int value3 = buffer_data.toInt();
      if(debug == 1){
        Serial.print("Value1: ");
        Serial.println(value1);
        Serial.print("Value2: ");
        Serial.println(value2);
        Serial.print("Value3: ");
        Serial.println(value3);
      }
      //write values to EEPROM for future use
      EEPROM.write(0,value1);
      EEPROM.write(1,value2);
      EEPROM.write(2,value3);
      EEPROM.commit();
      if(debug==1){
        Serial.println("EEPROM written:");
        Serial.println(EEPROM.read(0));
        Serial.println(EEPROM.read(1));
        Serial.println(EEPROM.read(2));
      }
      autoTime1 = value1;
      autoTime2 = value2;
      wateringTime1 = autoTime1;
      wateringTime2 = autoTime2;
      wakeUpTimeHour = value3;
      wakeUpTimeMin = wakeUpTimeHour * 60;
      wateringActive = 1;
      wateringStartTime = millis();
   }
     if (dummy_data.indexOf("manual") >= 0)
    {
      Serial.println("manual message detected");
      String buffer_data = dummy_data.substring(dummy_data.indexOf("_")+1,dummy_data.indexOf("*"));
      Serial.println(buffer_data);
      int value1 = buffer_data.toInt();
      buffer_data = dummy_data.substring(dummy_data.indexOf("*")+1);
      Serial.println(buffer_data);
      int value2 = buffer_data.toInt();
      if(debug == 1){
        Serial.print("Value1: ");
        Serial.println(value1);
        Serial.print("Value2: ");
        Serial.println(value2);
      }
      manualTime1 = value1;
      manualTime2 = value2;
      wateringTime1 = manualTime1;
      wateringTime2 = manualTime2;
      wateringActive = 1;
      wateringStartTime = millis();
   }   
     
   }
  
}
