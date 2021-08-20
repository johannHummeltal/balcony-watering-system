// watering 1 and 2 (one after the other) for respective given time
//watering is started when ESP is powered on
//ESP goes to sleep after watering is finished --> reboot or repower ESP to start again
//ESP wakes up after defined time (e.g. 12 hours) when not powered down


//when TASTER is pushed pump is started for a given time


// define pins
//#define wakeup 4 //pin for remote wakeup of ESP. e.g. if ESP is in sleep and you want to turn it on manually or connect to WIFI
#define pump 21

//watering circuit #1
#define valve1 23
#define sensor1 34

#define valve2 32

//sensor to check if the barrol is empty
#define fillsensor 15//4

//backup for future manual functions
#define Taster1 12//12



//declare some variable

int minMoisture = 2343; // value for a completely try sensor (in air)
int maxMoisture = 321; // sensor in water
int moisture1 = 0; // raw measurement value of sensor 1

//pumpe schafft nominal 10l/min (da stränge nacheinander, bekommt ein strang die volle Menge ab)
//measured after ventil: 2l/min
int Taster1Pushed = 0; 
int wateringTime1 = 120;//180; //time in seconds
int wateringTime2 = 75;//120; // time in seconds

double wakeUpTimeHour = 8; // wakeup for next watering after XX hours
int wakeUpTimeMin = wakeUpTimeHour*60; //time in min between two waterings
int uSecToSec = 1000000;

void setup() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_21);
  gpio_hold_dis(GPIO_NUM_23);
  gpio_hold_dis(GPIO_NUM_32);
  
  Serial.begin(115200);
  
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

  //measure if barrol is empty or not
  int barrolEmpty = digitalRead(fillsensor);
  //Serial.print("Barrol Empty: ");
  //Serial.println(barrolEmpty);
  
  //start watering when ESP is botted
  if(barrolEmpty==1){
    Serial.println("Barrol is empty, watering was not started");
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

void loop() {
  if(Taster1Pushed == 1){
     wateringFunction();
  }
}

void Taster1Function() {
  Serial.println("Taster1 pushed");
  Taster1Pushed = 1;
}

void wateringFunction(){
  Serial.println("Pump started");
    digitalWrite(pump, HIGH);
    digitalWrite(valve1, HIGH);
    delay(wateringTime1 * 1000);
    digitalWrite(valve1, LOW);
    digitalWrite(valve2, HIGH);
    delay(wateringTime2 * 1000);
    digitalWrite(pump,LOW);
    digitalWrite(valve1, LOW);
    digitalWrite(valve2, LOW);
    Serial.println("Pump stopped");
    Taster1Pushed = 0;
}
