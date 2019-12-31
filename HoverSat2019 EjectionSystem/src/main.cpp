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
#include <VL53L0X.h>

//Define
//------------------------------------------------------------------//
#define TIMER_INTERRUPT       1

#define LEDC_CHANNEL_0        0
#define LEDC_TIMER_BIT        16
#define LEDC_BASE_FREQ        1000
#define GPIO_PIN              19

#define NOOFPATTERNS 5
int parameters[NOOFPATTERNS][3] =
{
// PWM, EjctionTime, HoverTime
{ 20, 100, 5000 },
{ 40, 200, 5000 },
{ 60, 300, 5000 },
{ 80, 400, 5000 },
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
unsigned char udp_bb = 0;
unsigned char udp_flag = 0;

unsigned char pattern = 0;
bool log_flag = false;
unsigned char pwm;
unsigned char core0_pattern = 0;

unsigned long time_ms;
unsigned long time_buff = 0;
volatile int interruptCounter;
int iTimer10;

static const int LED_Pin = 17;
 
WiFiUDP udp;
TaskHandle_t task_handl;
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Parameters
unsigned char hover_val = 70;
unsigned int ex_pwm = 100;
unsigned int ex_time = 100;
unsigned char patternNo = 0;

unsigned int ex_distance;
unsigned int ex_interval;
unsigned char flag = 0;

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

//Setup #1
//------------------------------------------------------------------//
void setup() {
  M5.begin();
  delay(1000);
  setupWiFiUDPserver();
  
  xTaskCreatePinnedToCore(&taskDisplay, "taskDisplay", 8192, NULL, 10, &task_handl, 0);

  // Initialize Timer Interrupt
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, TIMER_INTERRUPT * 1000, true);
  timerAlarmEnable(timer); 
  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
  ledcAttachPin(GPIO_PIN, LEDC_CHANNEL_0);
  pinMode(LED_Pin, OUTPUT);

  delay(500);


}

//Main #1
//------------------------------------------------------------------//
void loop() {
  receiveUDP();
  Timer_Interrupt(); 

  switch (pattern) {
    case 0:
      break;

    case 11:    
      pwm = map(ex_pwm, 0, 100, 0, 65535);
      ledcWrite(LEDC_CHANNEL_0, pwm);
      M5.Lcd.fillRect(0, 0, 80, 80, TFT_RED);
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
      if( millis() - time_buff >= ex_time*10 ) {
        ledcWrite(LEDC_CHANNEL_0, 0);
        digitalWrite( LED_Pin, 0 );
        M5.Lcd.fillRect(0, 0, 80, 80, TFT_DARKGREY);
        pattern = 0;
      }
      break; 

  }
}

//Main #0
//------------------------------------------------------------------//
void taskDisplay(void *pvParameters){
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
  M5.Lcd.setCursor(80, 120);
  M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(80, 170);
  M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);

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
    M5.Lcd.setCursor(80, 120);
    M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
    M5.Lcd.setCursor(80, 170);
    M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);
    pattern = udp.read();
    patternNo = udp.read();
    udp_bb = udp.read();
    udp_flag = udp.read();
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(5);
    M5.Lcd.setCursor(0, 150);
    M5.Lcd.printf("%2d", patternNo+1);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(80, 40);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.printf("Eject PWM %3d", parameters[patternNo][0]);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(80, 120);
    M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
    M5.Lcd.setCursor(80, 170);
    M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);
  }
}
 
void sendUDP(){
  udp.beginPacket(to_udp_address, to_udp_port);
  udp.write(udp_pattern);
  udp.write(udp_No);
  udp.write(udp_bb);
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
  if (M5.BtnA.wasPressed()) {
    udp_pattern = 11;
    sendUDP();
    pattern = 11;
  } else if (M5.BtnB.wasPressed()) {
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
    M5.Lcd.setCursor(80, 120);
    M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
    M5.Lcd.setCursor(80, 170);
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
    M5.Lcd.setCursor(80, 120);
    M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]);
    M5.Lcd.setCursor(80, 170);
    M5.Lcd.printf("Hovering Time %4d", parameters[patternNo][2]);
  } else if (M5.BtnC.wasPressed()) {
    udp_pattern = 111;
    sendUDP();
    pattern = 111;
  }
} 


