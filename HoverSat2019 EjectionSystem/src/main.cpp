//------------------------------------------------------------------//
//Supported MCU:   ESP32 (M5Stack)
//File Contents:   HoverSat EjectionSystem
//Version number:  Ver.1.0
//Date:            2019.12.29
//------------------------------------------------------------------//
 
//This program supports the following boards:
//* M5Stack(Grey version)
 
//Include
//------------------------------------------------------------------//
#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <time.h>
#include <EEPROM.h>
#include <VL53L0X.h>

//Define
//------------------------------------------------------------------//
#define TIMER_INTERRUPT       1

#define LEDC_CHANNEL_0        0
#define LEDC_TIMER_BIT        16
#define LEDC_BASE_FREQ        1000
#define GPIO_PIN              2

#define LEDC2_CHANNEL_1       1
#define LEDC2_TIMER_BIT       16
#define LEDC2_BASE_FREQ       1000
#define GPIO2_PIN             15


#define NOOFPATTERNS 5
int parameters[NOOFPATTERNS][3] =
{
// PWM, EjctionTime, HoverTime
{ 20, 500, 5000 },
{ 40, 500, 5000 },
{ 60, 500, 5000 },
{ 80, 500, 5000 },
{ 100, 500, 5000 },
};
VL53L0X sensor;

//Global
//------------------------------------------------------------------//
const char* ssid = "HoverSat-2019"; 
const char* password = "root0123";
 
const char * to_udp_address = "192.168.4.2";
const int to_udp_port = 55556;
const int my_server_udp_port = 55555;

unsigned char udp_pattern = 0;
unsigned char udp_No = 0;
unsigned char udp_SH = 0;
unsigned char udp_SL = 0;
unsigned char udp_flag = 0;

unsigned char pattern = 0;
bool log_flag = false;
unsigned int pwm;
unsigned char core0_pattern = 0;

unsigned long time_ms;
unsigned long time_buff = 0;
unsigned long time_buff2 = 0;
unsigned long time_buff3 = 0;
volatile int interruptCounter;
int iTimer10;

// IO define
static const int LED_Pin = 17;
static const int MAGNET_Pin = 15;
static const int IR_Pin = 5;

// WiFi
WiFiUDP udp;
TaskHandle_t task_handl;

// Timer
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

//SD
File file;
String fname_buff;
const char* fname;

// Battery
unsigned int cnt_battery;
unsigned char battery_status;
unsigned char battery_persent;

// Parameters
unsigned char hover_val = 70;
unsigned int ex_pwm = 100;
unsigned int ex_time = 100;
unsigned char patternNo = 0;

unsigned int ex_distance;
unsigned int ex_interval;
unsigned char flag = 0;
bool cnt_flag = false;

unsigned int totalSeaquence = 0;
unsigned int interval_time;
unsigned int interval_time_buff;
unsigned char IR_flag = 0;

unsigned int mag_pwm = 90 

;
unsigned int mag_pwm_map = 0;


//Prototype
//------------------------------------------------------------------//
void receiveUDP(void);
void sendUDP(void);
void setupWiFiUDPserver(void);
void button_action(void);
void taskDisplay(void *pvParameters);
void IRAM_ATTR onTimer(void);
void Timer_Interrupt(void);
void LCD_Control(void);
uint8_t getBatteryGauge(void);
void taskInit(void);

//Setup #1
//------------------------------------------------------------------//
void setup() {
  M5.begin();
  delay(1000);
  setupWiFiUDPserver();
  
  xTaskCreatePinnedToCore(&taskDisplay, "taskDisplay", 6144, NULL, 10, &task_handl, 0);

  // Create Log File
  /*fname_buff  = "/log/Satellite_log.csv";
  fname = fname_buff.c_str();

  SD.begin(4, SPI, 24000000);
  // Create Log File
  file = SD.open(fname, FILE_APPEND);
  if( !file ) {
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(5, 160);
    M5.Lcd.println("Failed to open sd");
  }*/

  // Initialize IIC
  Wire.begin();
  Wire.setClock(400000);

  // Initialize Timer Interrupt
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, TIMER_INTERRUPT * 1000, true);
  timerAlarmEnable(timer); 

  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
  ledcAttachPin(GPIO_PIN, LEDC_CHANNEL_0);
  ledcSetup(LEDC2_CHANNEL_1, LEDC2_BASE_FREQ, LEDC2_TIMER_BIT);
  ledcAttachPin(GPIO2_PIN, LEDC2_CHANNEL_1);

  mag_pwm_map = map(65535, 0, 100, 0, 65535); 
  ledcWrite(LEDC2_CHANNEL_1, mag_pwm_map);
  
  pinMode(LED_Pin, OUTPUT);
  //pinMode(MAGNET_Pin, OUTPUT);
  pinMode(IR_Pin, INPUT);
  //digitalWrite( MAGNET_Pin, 1 );  

  delay(500);


}

//Main #1
//------------------------------------------------------------------//
void loop() {
  receiveUDP();
  Timer_Interrupt(); 

  IR_flag = digitalRead(IR_Pin);

  switch (pattern) {
    case 0:
      break;

    case 11:    
      ex_pwm = parameters[patternNo][0];
      ex_time = parameters[patternNo][1];
      pwm = map(ex_pwm, 0, 100, 0, 65535);
      ledcWrite(LEDC_CHANNEL_0, pwm);
      digitalWrite( LED_Pin, 1 );  
      time_buff = millis();
      pattern = 12;
      flag = 1;
      break;

    case 12:
      if( ex_distance >= 30 && flag == 1 ) {
        flag = 0;
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(96, 192);
        M5.Lcd.printf("Interval Time %4d", ex_interval);
        ex_interval = millis() - time_buff;
      }
      if( millis() - time_buff >= ex_time ) {
        ledcWrite(LEDC_CHANNEL_0, 0);
        digitalWrite( LED_Pin, 0 );
        pattern = 0;
      }
      break; 

    case 111:    
      ex_pwm = parameters[patternNo][0];
      ex_time = parameters[patternNo][1];
      time_buff = millis();
      time_buff2 = 0;
      time_buff3 = 0;
      M5.Lcd.fillRect(0, 20, 60, 60, TFT_LIGHTGREY);
      //digitalWrite( MAGNET_Pin, 0 ); 
      mag_pwm_map = map(mag_pwm, 0, 100, 0, 65535); 
      ledcWrite(LEDC2_CHANNEL_1, mag_pwm_map);
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(80, 100);
      M5.Lcd.printf("ID        %08d", totalSeaquence); 
      pattern = 112;
      break;
    
    case 112:
      if(cnt_flag) {
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(8, 36);
        M5.Lcd.setTextColor(TFT_LIGHTGREY);
        M5.Lcd.printf("%2d", time_buff3);
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(8, 36);
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.printf("%2d", time_buff2);
        cnt_flag = false;
      }
      time_buff3 = time_buff2;
      time_buff2 = (10000-(millis()-time_buff))/1000;
      if(time_buff2 < time_buff3) {
        cnt_flag = true;
      }
      if( millis() - time_buff >= 7000 ) {
        log_flag = true;
        pattern = 113;
      }
      break;
    
    case 113:
      if(cnt_flag) {
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(8, 36);
        M5.Lcd.setTextColor(TFT_LIGHTGREY);
        M5.Lcd.printf("%2d", time_buff3);
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(8, 36);
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.printf("%2d", time_buff2);
        cnt_flag = false;
      }
      time_buff3 = time_buff2;
      time_buff2 = (10000-(millis()-time_buff))/1000;
      if(time_buff2 < time_buff3) {
        cnt_flag = true;
      }
      if( millis() - time_buff >= 9500 ) {
        ledcWrite(LEDC2_CHANNEL_1, 65535);
        pattern = 114;
      }
      break;

    case 114:
      if(cnt_flag) {
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(8, 36);
        M5.Lcd.setTextColor(TFT_LIGHTGREY);
        M5.Lcd.printf("%2d", time_buff3);
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(8, 36);
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.printf("%2d", time_buff2);
        cnt_flag = false;
      }
      time_buff3 = time_buff2;
      time_buff2 = (10000-(millis()-time_buff))/1000;
      if(time_buff2 < time_buff3) {
        cnt_flag = true;
      }
      if( millis() - time_buff >= 10000 ) {
        time_buff = millis();
        pattern = 115;
      }
      break;
    
    case 115: 
      interval_time_buff = micros();      
      pwm = map(ex_pwm, 0, 100, 0, 65535);
      ledcWrite(LEDC_CHANNEL_0, pwm);
      digitalWrite( LED_Pin, 1 );      
      pattern = 116;
      flag = 1;
      break;

    case 116:
      if( IR_flag && flag == 1 ) {
        flag = 0;
        interval_time = micros()-interval_time_buff;
      }
      if( millis() - time_buff >= ex_time ) {
        ledcWrite(LEDC_CHANNEL_0, 0);
        digitalWrite( LED_Pin, 0 );
        pattern = 117;
      }
      break; 
    
    case 117:
      if( millis() - time_buff >= parameters[patternNo][2] ) {
        pattern = 0;
        cnt_flag = false;
        log_flag = false;
        delay(50);
        M5.Lcd.fillRect(0, 20, 60, 60, TFT_LIGHTGREY);
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(8, 36);
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.print("Ej");    
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(80, 100);
        M5.Lcd.printf("ID        %08d", totalSeaquence); 
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(80, 100);
        M5.Lcd.printf("Int Time    %06d", interval_time); 
        delay(5000);
        M5.Lcd.setTextColor(BLACK);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(80, 100);
        M5.Lcd.printf("Int Time    %06d", interval_time); 
      }
      break;

  }
}

//Main #0
//------------------------------------------------------------------//
void taskDisplay(void *pvParameters){

  EEPROM.begin(128);
  taskInit();  
  totalSeaquence = (EEPROM.read(102)<<8) + EEPROM.read(101);
  //sensor.init();
  //sensor.setTimeout(500);
  //sensor.startContinuous();
  while(1){    
    M5.update();
    button_action();
  
    switch (core0_pattern) {
    case 0:
      LCD_Control();
      break;

    case 10:
      core0_pattern = 0;
      break;
    }

    core0_pattern++;
    delay(1);
    cnt_battery++;
    if( cnt_battery >= 5000 && !log_flag ) {
      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(280, 2);
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.printf("%3d",battery_persent);
      battery_status = getBatteryGauge();
      switch (battery_status) {
      case 0xF0:
        battery_persent = 0;
        break;
      case 0xE0:
        battery_persent = 25;
        break;
      case 0xC0:
        battery_persent = 50;
        break;
      case 0x80:
        battery_persent = 75;
        break;
      case 0x00:
        battery_persent = 100;
        break;        
      }
      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(280, 2);
      M5.Lcd.setTextColor(BLACK);
      M5.Lcd.printf("%3d",battery_persent);
      cnt_battery = 0;
    }
  }
}

// Timer Interrupt
//------------------------------------------------------------------//
void Timer_Interrupt( void ){
  if (interruptCounter > 0) {

    portENTER_CRITICAL(&timerMux);
    interruptCounter--;
    portEXIT_CRITICAL(&timerMux);

  }
}

// IRAM
//------------------------------------------------------------------//
void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  interruptCounter=1;
  portEXIT_CRITICAL_ISR(&timerMux);
}

// LCD_Control
//------------------------------------------------------------------//
void LCD_Control() {
  
}

void receiveUDP(){
  int packetSize = udp.parsePacket();
  if(packetSize > 0){
    M5.Lcd.setTextColor(TFT_DARKGREY);
    M5.Lcd.setTextSize(5);
    M5.Lcd.setCursor(0, 150);
    M5.Lcd.printf("%2d", patternNo+1);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(80, 40);
    M5.Lcd.setTextColor(TFT_DARKGREY);
    M5.Lcd.printf("Eject PWM %3d", parameters[patternNo][0]);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(80, 145);
    M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
    M5.Lcd.setCursor(80, 190);
    M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);   
    udp_SH = udp.read();
    udp_SL = udp.read();
    totalSeaquence = (udp_SH<<8) + udp_SL; 
    pattern = udp.read();
    patternNo = udp.read();
    udp_flag = udp.read();    
    EEPROM.write(101, totalSeaquence&0x0F);
    EEPROM.write(102, (totalSeaquence>>8)&0x0F);
    EEPROM.commit();
    delay(20);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(5);
    M5.Lcd.setCursor(0, 150);
    M5.Lcd.printf("%2d", patternNo+1);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(80, 40);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.printf("Eject PWM %3d", parameters[patternNo][0]);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(80, 145);
    M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
    M5.Lcd.setCursor(80, 190);
    M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);
    delay(20);
  }
}
 
void sendUDP(){
  udp.beginPacket(to_udp_address, to_udp_port);  
  udp.write(udp_SH);
  udp.write(udp_SL);
  udp.write(udp_pattern);
  udp.write(udp_No);
  udp.write(udp_flag);
  udp.endPacket();
}
 
void setupWiFiUDPserver(){
  WiFi.disconnect(true, true);
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  udp.begin(myIP, my_server_udp_port);
  delay(1000);
}
 
void button_action(){
  if (M5.BtnA.wasPressed() && pattern == 0) {
    udp_pattern = 11;
    sendUDP();
    udp_pattern = 0;
    pattern = 11;
  } else if (M5.BtnB.wasPressed() && pattern == 0) {
    M5.Lcd.setTextColor(TFT_DARKGREY);
    M5.Lcd.setTextSize(5);
    M5.Lcd.setCursor(0, 150);
    M5.Lcd.printf("%2d", patternNo+1);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(80, 40);
    M5.Lcd.setTextColor(TFT_DARKGREY);
    M5.Lcd.printf("Eject PWM %3d", parameters[patternNo][0]);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(80, 145);
    M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
    M5.Lcd.setCursor(80, 190);
    M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);
    patternNo++;
    if( patternNo >= NOOFPATTERNS ) {
      patternNo = 0;
    }
    udp_No = patternNo;
    sendUDP();
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(5);
    M5.Lcd.setCursor(0, 150);
    M5.Lcd.printf("%2d", patternNo+1);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(80, 40);
    M5.Lcd.printf("Eject PWM %3d", parameters[patternNo][0]);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(80, 145);
    M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
    M5.Lcd.setCursor(80, 190);
    M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);
  } else if (M5.BtnC.wasPressed() && pattern == 0) {    
    totalSeaquence++;
    udp_SL = totalSeaquence&0x0F;
    udp_SH = (totalSeaquence>>8)&0x0F;    
    udp_pattern = 111;
    sendUDP();
    udp_pattern = 0;
    EEPROM.write(101, totalSeaquence&0x0F);
    EEPROM.write(102, (totalSeaquence>>8)&0x0F);
    EEPROM.commit();
    pattern = 111;
  }
} 

uint8_t getBatteryGauge() {
  Wire.beginTransmission(0x75);
  Wire.write(0x78);
  Wire.endTransmission(false);
  if(Wire.requestFrom(0x75, 1)) {
    return Wire.read();
  }
  return 0xff;
}

void taskInit() {
  M5.Lcd.fillRect(0, 0, 320, 20, TFT_WHITE);
  M5.Lcd.fillRect(60, 20, 260, 60, TFT_DARKGREY);
  M5.Lcd.fillRect(0, 80, 60, 160, TFT_DARKGREY);
  M5.Lcd.fillRect(0, 20, 60, 60, TFT_LIGHTGREY);
  M5.Lcd.fillRect(0, 220, 320, 20, TFT_WHITE);

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 2);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.print("Satellite Ejector");
  M5.Lcd.setCursor(40, 222);
  M5.Lcd.print("Eject");
  M5.Lcd.setCursor(140, 222);
  M5.Lcd.print("MODE");
  M5.Lcd.setCursor(228, 222);
  M5.Lcd.print("START");
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(8, 36);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.print("Ej");
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(80, 40);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.printf("Eject PWM %3d", parameters[patternNo][0]);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(8, 110);
  M5.Lcd.print("No.");

  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(5);
  M5.Lcd.setCursor(0, 150);
  M5.Lcd.printf("%2d", patternNo+1);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(80, 145);
  M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(80, 190);
  M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);

  ex_pwm = parameters[patternNo][0];
  ex_time = parameters[patternNo][1];
}
