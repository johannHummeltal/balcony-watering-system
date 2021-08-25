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
int debug = 0; //set debug level; 1== print additional serial messages
//moisture sensor (optional, not yet realized)
int minMoisture = 2343; // value for a completely try sensor (in air)
int maxMoisture = 321; // sensor in water
int moisture1 = 0; // raw measurement value of sensor 1

//define watering times
int wateringTime1 = 20;//120; //time in seconds
int wateringTime2 = 25;//75; // time in seconds


double wakeUpTimeHour = 0.005; // wakeup for next watering after XX hours
double wakeUpTimeMin = wakeUpTimeHour*60; //time in min between two waterings
int uSecToSec = 1000000;

int wifiConnected = 0; //indicator if connection to WIFI was succesful(1) or not(0)

int barrolEmpty =0;

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

  pinMode(Taster1, INPUT_PULLUP);
  attachInterrupt(Taster1, Taster1Function, FALLING);

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
   start websocket und server
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
    }
  }else{
    Serial.println("Barrol is filled, watering starts");
    wateringFunction();
  }
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
  Serial.println("ESP is sleeping");
}

 /*--------------------------------------------------------------
   loop 
   --------------------------------------------------------------*/

void loop() {
  if(Taster1Pushed == 1){
     wateringFunction();
  }
}

 /*--------------------------------------------------------------
   define necesarry functions
   --------------------------------------------------------------*/

void Taster1Function() {
  Serial.println("Taster1 pushed");
  Taster1Pushed = 1;
}

void wateringFunction(){
  Serial.println("Pump started");
    digitalWrite(pump, HIGH);
    digitalWrite(valve1, HIGH);
    Serial.println("valve 1 switched on");
    delay(wateringTime1 * 1000);
    digitalWrite(valve1, LOW);
    digitalWrite(valve2, HIGH);
    Serial.println("valve 2 switched on");
    delay(wateringTime2 * 1000);
    digitalWrite(pump,LOW);
    digitalWrite(valve1, LOW);
    digitalWrite(valve2, LOW);
    Serial.println("Pump stopped");
    Taster1Pushed = 0;
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
    globalClient = client;
    if(debug==1){
    //ledcWrite(PWM_channel4,255);
    }
    //send initially set data to web client
    String value1 = String (wateringTime1);
    while(value1.length()<3){
        value1 = '0' + value1;
      }
    String value2 = String (wateringTime2);
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
   
   
     if (dummy_data.indexOf("Color") >= 0)
    {
      String buffer_data = dummy_data.substring(dummy_data.indexOf("_")+1);
      
//      for(int i = 0; i < NLED; i++)
//         {
//          
//          String buffer_data2=buffer_data.substring(buffer_data.indexOf("(")+1,buffer_data.indexOf("(")+4);
//          Serial.println((buffer_data2));
//          //ColorArray[i][0]=buffer_data2.toInt();
//          buffer_data=buffer_data.substring(dummy_data.indexOf("(")+5);
//          Serial.println(buffer_data2);
//          //ColorArray[i][1]=buffer_data2.toInt();
//          buffer_data2=buffer_data.substring(5,7);
//          Serial.println(buffer_data2);
//          //ColorArray[i][2]=buffer_data2.toInt();
//          Serial.println("---------");
//         }
 //
      long number = (long) strtol( &buffer_data[1], NULL, 16);
      int colour_r = number >> 16;
      int colour_g = number >> 8 & 0xFF;
      int colour_b = number & 0xFF;
      Serial.println(colour_r);
      Serial.println(colour_g);
      Serial.println(colour_b);
      //for(int i = 0; i < NLED; i++)
      //{
       // ColorArray[i][0]=colour_r;
       // ColorArray[i][1]=colour_g;
       // ColorArray[i][2]=colour_b;
      //}
      //ColorChange=1;
      //colour_hue = get_hue(colour_r,colour_g,colour_b);
      //colour_saturation = get_saturation(colour_r,colour_g,colour_b);
   }
   if (dummy_data.indexOf("Aus") >= 0)
    {
     // Taster1Function();
    }

//    if (dummy_data.indexOf("Taster_2") >= 0)
//    {
//      Taster2Function();
//    }
//    if (dummy_data.indexOf("Taster_3") >= 0)
//    {
//      Taster3Function();
//    }
//    if (dummy_data.indexOf("Taster_4") >= 0)
//    {
//      Taster4Function();
//    }

//    if (dummy_data.indexOf("Store") >= 0)
//    {
//      String buffer_data = dummy_data.substring(dummy_data.indexOf("_")+1);
//      int number = buffer_data.toInt();
//      EEPROM.write((number-2)*3,ColorArray[0][0]);
//      EEPROM.write((number-2)*3+1,ColorArray[0][1]);
//      EEPROM.write((number-2)*3+2,ColorArray[0][2]);
//      EEPROM.commit();
//   
//      Serial.println("EEprom written:");
//      int debug = 0;
//      if(debug==1){
//        Serial.println(EEPROM.read(0));
//        Serial.println(EEPROM.read(1));
//        Serial.println(EEPROM.read(2));
//        Serial.println(EEPROM.read(3));
//        Serial.println(EEPROM.read(4));
//        Serial.println(EEPROM.read(5));
//        Serial.println(EEPROM.read(6));
//        Serial.println(EEPROM.read(7));
//        Serial.println(EEPROM.read(8));
//      } 
//    }
        
     
  }
  
}
