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

//moisture sensor (optional, not yet realized)
int minMoisture = 2343; // value for a completely try sensor (in air)
int maxMoisture = 321; // sensor in water
int moisture1 = 0; // raw measurement value of sensor 1

//define watering times
int wateringTime1 = 2;//120; //time in seconds
int wateringTime2 = 5;//75; // time in seconds


double wakeUpTimeHour = 0.005; // wakeup for next watering after XX hours
double wakeUpTimeMin = wakeUpTimeHour*60; //time in min between two waterings
int uSecToSec = 1000000;

int wifiConnected = 0; //indicator if connection to WIFI was succesful(1) or not(0)

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

 /*--------------------------------------------------------------
   setuop block
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
   measure if barrol is empty or not
   --------------------------------------------------------------*/

  int barrolEmpty = digitalRead(fillsensor);
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
