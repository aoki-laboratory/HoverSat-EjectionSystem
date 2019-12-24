//------------------------------------------------------------------//
//Supported MCU:   ESP32 (M5Stack)
//File Contents:   HoverSat Ejection System
//Version number:  Ver.1.0
//Date:            2019.12.24
//------------------------------------------------------------------//
 
//This program supports the following boards:
//* M5Stack(Gray version)
 
//Include
//------------------------------------------------------------------//
#include <M5Stack.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <EEPROM.h>
#include "BluetoothSerial.h"


//Define
//------------------------------------------------------------------//
#define   TIMER_INTERRUPT     10      // ms
#define   LCD

#define LEDC_CHANNEL_0 0
#define LEDC_TIMER_BIT 16

#define LEDC_BASE_FREQ 1000
#define GPIO_PIN 19

#define BufferRecords 16

#define NOOFPATTERNS  5

int parameters[NOOFPATTERNS][2] =
{
// PWM, EjctionTime/10
{ 100, 100 },
{ 80, 100 },
{ 60, 100 },
{ 40, 100 },
{ 20, 100 },
};



//Global
//------------------------------------------------------------------//
int     pattern = 0;
int     tx_pattern = 0;
int     rx_pattern = 0;
int     rx_val = 0;
bool    hover_flag = false;
bool    log_flag = false;
bool    telemetry_flag = false;
int     cnt10 = 0;

unsigned int pwm;

unsigned long time_ms;
unsigned long time_stepper = 0;
unsigned long time_buff = 0;
unsigned long time_buff2 = 0;
unsigned char current_time = 0; 
unsigned char old_time = 0;  

byte    counter;
char charBuf[100];
char charBuf2[100];
long steps;
float velocity;
boolean hasData = false;
String label = "Tick";


BluetoothSerial bts;
String  bts_rx;
char bts_rx_buffer[16];
int bts_index = 0;

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "Buffalo-G-0CBA";
char pass[] = "hh4aexcxesasx";
//char ssid[] = "X1Extreme-Hotspot";
//char pass[] = "5]6C458w";
//char ssid[] = "Macaw";
//char pass[] = "1234567890";


// Time
char ntpServer[] = "ntp.nict.jp";
const long gmtOffset_sec = 9 * 3600;
const int  daylightOffset_sec = 0;
struct tm timeinfo;
String dateStr;
String timeStr;

File file;
String fname_buff;
const char* fname;

const char* dfname;

String accel_buff;
const char* accel_out;

typedef struct {
    String  log_time;
    int     log_pattern;
    String  log_time_ms;
    float   log_length;
    float   log_velocity;
    float   log_accel;
} RecordType;

static RecordType buffer[2][BufferRecords];
static volatile int writeBank = 0;
static volatile int bufferIndex[2] = {0, 0};


// Timer Interrupt
volatile int interruptCounter;
volatile int interruptCounterS;
int totalInterruptCounter;
int iTimer10;

static const int LED_Pin = 34;


hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Parameters
unsigned char hover_val = 70;
unsigned int ex_pwm = 100;
unsigned int ex_time = 100;
unsigned char patternNo = 0;




//Global
//------------------------------------------------------------------//
void IRAM_ATTR onTimer(void);
void SendByte(byte addr, byte b);
void SendCommand(byte addr, char *ci);
void Timer_Interrupt( void );
void getTimeFromNTP(void);
void getTime(void);
void bluetooth_rx(void);
void bluetooth_tx(void);
void eeprom_write(void);
void eeprom_read(void);
void TSND121( void );


//Setup
//------------------------------------------------------------------//
void setup() {

  M5.begin();
  Wire.begin();
  EEPROM.begin(128);
  SD.begin(4, SPI, 24000000, "/sd");
  M5.Lcd.clear();
  M5.Lcd.drawJpgFile(SD, "/Image/Picture.jpg");
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(88, 160);
  M5.Lcd.println("HoverSat");
  M5.Lcd.setCursor(82, 200);
  M5.Lcd.println("Satellite1");

  //eeprom_read();
  ex_pwm = parameters[0][0];
  ex_time = parameters[0][1];
  
  delay(1000);

  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(GREEN ,BLACK);
  M5.Lcd.fillScreen(BLACK);


  Serial.begin(115200);
  bts.begin("M5Stack Satellite1");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.print(".");
  }

  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
  ledcAttachPin(GPIO_PIN, LEDC_CHANNEL_0);

  pinMode(LED_Pin, OUTPUT);
  

  // timeSet
  getTimeFromNTP();
  getTime();
  fname_buff  = "/log/Satellite1_log_"
              +(String)(timeinfo.tm_year + 1900)
              +"_"+(String)(timeinfo.tm_mon + 1)
              +"_"+(String)timeinfo.tm_mday
              +"_"+(String)timeinfo.tm_hour
              +"_"+(String)timeinfo.tm_min
              +".csv";
  fname = fname_buff.c_str();


  // Initialize Timer Interrupt
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, TIMER_INTERRUPT * 1000, true);
  timerAlarmEnable(timer);

  //file = SD.open(fname, FILE_APPEND);
  //if( !file ) {
  //  M5.Lcd.setCursor(5, 160);
  //  M5.Lcd.println("Failed to open sd");
  //}

  
}




//Main
//------------------------------------------------------------------//
void loop() {

  Timer_Interrupt();
  //ReceiveStepperData();
  bluetooth_rx();
  bluetooth_tx();

  int readBank = !writeBank;

  if (bufferIndex[readBank] >= BufferRecords) {
    static RecordType temp[BufferRecords];

    memcpy(temp, buffer[readBank], sizeof(temp));
    bufferIndex[readBank] = 0;
    file = SD.open(fname, FILE_APPEND);
    for (int i = 0; i < BufferRecords; i++) {
        file.print(temp[i].log_time);
        file.print(",");
        file.print(temp[i].log_pattern);
        file.print(",");
        file.print(temp[i].log_time_ms);
        file.print(",");
        file.print(temp[i].log_velocity);
        file.println(",");
    }
    file.close();
  }

  switch (pattern) {
    case 0:
      break;

    case 11:    
      pwm = map(ex_pwm, 0, 100, 0, 65535);
      ledcWrite(LEDC_CHANNEL_0, pwm);
      digitalWrite( LED_Pin, 1 );
      time_buff2 = millis();
      pattern = 12;
      break;

    case 12:
      if( millis() - time_buff2 >= ex_time*10 ) {
        ledcWrite(LEDC_CHANNEL_0, 0);
        digitalWrite( LED_Pin, 0 );
        pattern = 0;
      }
      break;

      case 21:    
      time_buff2 = millis();
      pattern = 22;
      break;

    case 22:
      if( millis() - time_buff2 >= 1000 ) {
        pattern = 0;
      }
      break;


    // CountDown
    case 111:    
      if( current_time >= 52  ) {
        time_buff2 = millis();
        pattern = 113;      
        M5.Lcd.clear();
        break;
      }
      M5.Lcd.setCursor(180, 100);
      M5.Lcd.clear();
      M5.Lcd.println(60 - current_time);
      bts.println( 60 - current_time );
      break;

    case 112:     
      if( current_time < 1 ) {
        pattern = 111;
        break;
      }
      bts.println( 60 - current_time + 60 );
      M5.Lcd.setCursor(180, 100);
      M5.Lcd.clear();
      M5.Lcd.println(60 - current_time);
      break;

    case 113:    
      if( millis() - time_buff2 >= 3000 ) {
        bts.println(" - Start within 5 seconds -");
        time_buff2 = millis();
        log_flag = false;
        pattern = 122;
        break;
      }    
      bts.println( 60 - current_time );
      M5.Lcd.setCursor(180, 100);
      M5.Lcd.clear();
      M5.Lcd.println(60 - current_time);
      break;

    case 122:   
      if( millis() - time_buff2 >= 3000 ) {
        time_buff2 = millis();
        pattern = 114;
        bts.println( "\n - Log start -" );
        break;
      }        
      break;

    case 114:   
      if( millis() - time_buff2 >= 5000 ) {
        time_buff = millis();
        pattern = 115;
        bts.println( "\n - Sequence start -" );
        break;
      }        
      break;

  }

      
  // Button Control
  M5.update();
  if (M5.BtnA.wasPressed()) {
    pattern = 11;
  } else if (M5.BtnB.wasPressed() && pattern == 0) {  
    patternNo++;
    M5.Lcd.fillScreen(BLACK);
    if( patternNo >= NOOFPATTERNS ) {
      patternNo = 0;
    }
    ex_pwm = parameters[patternNo][0];
    ex_time = parameters[patternNo][1];
    
  } else if (M5.BtnC.wasPressed() && pattern == 0) { 
    M5.Lcd.clear();
    M5.Lcd.setCursor(82, 100);
    if( current_time >= 52 ) {   
      pattern = 112;
    } else {
      pattern = 111;
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

    cnt10++;
    time_ms = millis()-time_buff;
    
    getTime();

    if (bufferIndex[writeBank] < BufferRecords && log_flag) {
      RecordType* rp = &buffer[writeBank][bufferIndex[writeBank]];
      rp->log_time = timeStr;
      rp->log_pattern = pattern;
      rp->log_time_ms = time_ms;
      if (++bufferIndex[writeBank] >= BufferRecords) {
          writeBank = !writeBank;
      }      
    }

  //    totalInterruptCounter++;

    iTimer10++;
    switch( iTimer10 ) {
    case 1:
      M5.Lcd.fillRect(0, 0, 80, 80, TFT_WHITE);
      M5.Lcd.fillRect(80, 0, 240, 80, TFT_DARKGREY);
      M5.Lcd.fillRect(0, 80, 80, 160, TFT_DARKGREY);
      M5.Lcd.setTextSize(5);
      M5.Lcd.setCursor(13, 23);
      M5.Lcd.setTextColor(BLACK);
      M5.Lcd.print("Ej");
      M5.Lcd.setTextSize(3);
      M5.Lcd.setCursor(96, 30);
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.printf("PWM      %3d", parameters[patternNo][0]);
      M5.Lcd.setCursor(15, 120);
      M5.Lcd.print("No.");
      M5.Lcd.setTextSize(5);
      M5.Lcd.setCursor(10, 160);
      M5.Lcd.printf("%2d", patternNo+1);

      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(96, 152);
      M5.Lcd.printf("Ejection Time %4d", parameters[patternNo][1]*10);
      break;

    case 2:
      if( tx_pattern == 11 ) {
        telemetry_flag = true;
      }
      break;
    
    case 10:
      iTimer10 = 0;
      break;

    }

  }
}


// EEPROM Write
//------------------------------------------------------------------// 
void eeprom_write(void) {
  EEPROM.write(1, (ex_pwm & 0xFF));
  EEPROM.write(2, (ex_time & 0xFF));
  EEPROM.commit();
}

// EEPROM Read
//------------------------------------------------------------------// 
void eeprom_read(void) {
    ex_pwm = EEPROM.read(1);
    ex_time = EEPROM.read(2);
}


// Bluetooth RX
//------------------------------------------------------------------//
void bluetooth_rx(void) {

  while (bts.available() > 0) {
    bts_rx_buffer[bts_index] = bts.read();
    bts.write(bts_rx_buffer[bts_index]);
    
    if( bts_rx_buffer[bts_index] == '/' ) {
      bts.print("\n\n"); 
      if( tx_pattern == 1 ) {
        rx_pattern = atoi(bts_rx_buffer);
      } else {
        rx_val = atof(bts_rx_buffer);
      }
      bts_index = 0;
      
      switch ( rx_pattern ) {
          
      case 0:
        tx_pattern = 0;
        break;
        
      case 11:
        rx_pattern = 0;
        tx_pattern = 11;
        break;

      case 21:
        rx_pattern = 0;
        tx_pattern = 20;
        if( current_time >= 52 ) {   
          pattern = 112;
          break;
        } else {
          pattern = 111;
          break;
        }
        break;

      case 31:
        tx_pattern = 31;
        rx_pattern = 41;
        break;

      case 41:
        ex_pwm = rx_val;
        eeprom_write();
        tx_pattern = 0;
        rx_pattern = 0;
        break;

      case 32:
        tx_pattern = 32;
        rx_pattern = 42;
        break;

      case 42:
        ex_time = rx_val;
        eeprom_write();
        tx_pattern = 0;
        rx_pattern = 0;
        break;

      }
      
    } else {
        bts_index++;
    }
  }


}


// Bluetooth TX
//------------------------------------------------------------------//
void bluetooth_tx(void) {

    switch ( tx_pattern ) {
            
    case 0:
      delay(30);
      bts.print("\n\n\n\n\n\n");
      bts.print(" HoverSat Ejection Syestem (M5Stack version) "
                         "Test Program Ver1.00\n");
      bts.print("\n");
      bts.print(" Satellite control\n");
      bts.print(" 11 : Telemetry\n");
      bts.print("\n");
      bts.print(" 21 : Sequence Control\n");
      bts.print("\n");
      bts.print(" Set parameters  [Current val]\n");

      bts.print(" 31 : Ejection PWM [");
      bts.print(ex_pwm);
      bts.print("%]\n");

      bts.print(" 32 : Ejection Time [");
      bts.print(ex_time);
      bts.print("s]\n");

      
      bts.print("\n");
      bts.print(" Please enter 11 to 32  ");
      
      tx_pattern = 1;
      break;
        
    case 1: 
      break;
        
    case 2:
      break;
        
    case 11:
      //Telemetry @ Interrupt
      break;

    case 21:
      bts.print(" Starting Sequence...\n");
      tx_pattern = 1;
      break;
              
    case 31:
      bts.print(" Ejection PWM [%] -");
      bts.print(" Please enter 0 to 100 ");
      tx_pattern = 2;
      break;

    case 32:
      bts.print(" Extension Time [mm] -");
      bts.print(" Please enter 0 to 100 ");
      tx_pattern = 2;
      break;
                
    }
}



// IRAM
//------------------------------------------------------------------//
void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  interruptCounter=1;
  portEXIT_CRITICAL_ISR(&timerMux);
}



//Get Time From NTP
//------------------------------------------------------------------//
void getTimeFromNTP(void){
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  while (!getLocalTime(&timeinfo)) {
    delay(1000);
  }
}

//Get Convert Time
//------------------------------------------------------------------//
void getTime(void){
  getLocalTime(&timeinfo);
  dateStr = (String)(timeinfo.tm_year + 1900)
          + "/" + (String)(timeinfo.tm_mon + 1)
          + "/" + (String)timeinfo.tm_mday;
  timeStr = (String)timeinfo.tm_hour
          + ":" + (String)timeinfo.tm_min
          + ":" + (String)timeinfo.tm_sec;
  current_time = timeinfo.tm_sec;
}
