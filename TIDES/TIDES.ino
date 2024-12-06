/*  To do section:  
Add a check comparing last saved time (preferences) to current time on reset, do something if X minutes difference?
What to do at end of WL data file?
What if outflow is less that 0.01 L/min?
Update Display code to use proper frames
if WL < 0.03 (smallest pulse), then calculate volume and dispense it.
Add a check to see if WL dispensed and calculated WL are similar (add offset?)
Close valve all the way when switcing fill/drain
When reading in the preference file, the date check is done before the date/time is acquired from ntp server
Finish implementing error log code
*/


#include <Arduino.h>            //Library for using Arduinio IDE specific commands
#include "secrets.h"            //wifi and amazon AWS credentials stored here
#include "WiFi.h"               //Library for connecting to Wifi
#include <WiFiClientSecure.h>   //Library for connecting to Amazon Database
#include <MQTTClient.h>         //Library for sending MQTT messages to the Amazon database
#include <ArduinoJson.h>        //Library for structuring MQTT messages for Amazon database
#include <OneWire.h>            //Library for the ds18b20 temp sensor
#include <DallasTemperature.h>  //Library for the ds18b20 temp/humidity sensor
//#include "DHT.h"                //Library for the DHT22 temp/humidity sensor
#include "Adafruit_SHT31.h"  //Library used for Temp/Humidity sensor

#include <TimeLib.h>       //Library for reading and formatting time
#include <millisDelay.h>   //Library for adding delay timers without interupiting the rest of the program
#include "SSD1306Wire.h"   //Library used for controlling the OLED display
#include <esp_task_wdt.h>  //Library used for Watchdog Timer (WDT)
//#include "Preferences.h"  //Library used for storing data (preferneces) in flash memory
#include "FS.h"          //Library used for file system
#include "SD.h"          //Library used for SD card
#include <SPI.h>         //Library used for SPI
#include <CSV_Parser.h>  //include the CSV library
//#include <VL53L0X.h>            //Library used for VL53L0X Time of Flight Sensor
#include <vl53l4cd_class.h>  //Library used for vl53l4cd Time of Flight Sensor
//#include <vl53l4cx_class.h>
//#include "SparkFun_VL53L1X.h" //Click here to get the library: http://librarymanager/All#SparkFun_VL53L1X

#include <MedianFilter.h>       //Library for easy Mean, Median, Mode
#include <SPIFFS.h>             //Library to use SPIFFS, ESP's onboard flash storage
#include <ESPAsyncWebServer.h>  //Library used for the webserver (https://github.com/mathieucarbou/ESPAsyncWebServer/tree/main)
#include <AsyncTCP.h>           //This library is the base for ESPAsyncWebServer (https://github.com/mathieucarbou/AsyncTCP)
//#define CONFIG_ASYNC_TCP_RUNNING_CORE 1
#define CONFIG_HEAP_TRACING

#include "HTML.h"  //HTML.h has the html for the main pages on the webserver

#include "esp_heap_caps.h"
#include "esp_heap_trace.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
const int limit = 10000;

int Device_ID = 1;              //Change this for additional devices
bool Demo_Mode = false;         //Flag to switch to demo Mode
bool Calibration_Flag = false;  //Flag used when calibrating the flow sensor
bool Reset_Flag = true;         //A flag to identify when the device has been reset
bool SystemPause = false;       //Admin flag to pause the system
bool WebServerOn = false;

int PauseButton = HIGH;  //Variables to check the ESP32 BOOT Button to pause system
int PauseButtonState = HIGH;
int PauseSteadyState = HIGH;

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

String errorMSG = "";
String errorLog = "";

#define SD_CS 12
#define SD_MOSI 11  //variables for SD card connection
#define SD_SCLK 10
#define SD_MISO 9

String SD_Status = "";  //Status variables displayed on the webpage
String SPIFFS_Status = "";
String PSRAM_Status = "";

String dataEntry = "";
String WL_Data = "";  //variables used with the WL data
char* column_Date = 0;
float* column_Level = 0;

int CSV_linecount = 0;
int CSV_rows = 0;
int Level_timer = 0;

//int arrLen = 0;

#define SDA 14  //variables for OLED display
#define SCL 13
SSD1306Wire display(0x3c, SDA, SCL);

int counter = 0;        //used for testing, can be deleted later
int Show_Page = 0;      //variable to switch between "pages" on the display
int Display_Count = 0;  //varriable to control how long each page is displayed

#define LED_PIN 48

//#define DHTPIN 5  //variables for Humidity / Air temp sensor (DHT22)
//#define DHTTYPE DHT22
//DHT dht(DHTPIN, DHTTYPE);
//float s_humidity;
//float temp_C;
//float temp_F;

#define TempSDA 6  //variables for SHT31 Temp/humidity sensor (White (blue) SDA, Yellow SCL)
#define TempSCL 5
float temp_C = 0;
float humidP = 0;
int TempErrCount = 0;
Adafruit_SHT31 sht31 = Adafruit_SHT31(&Wire1);  //Load it on Wire1, the OLED is on Wire (ESP32 has 2 I2C channels)
bool SHT31_Flag = false;

const int oneWireBus = 4;  //variables for water temp sensor (ds18b20)
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);
int deviceCount = 0;
float temperatureC;
float tempCheck;
float temperatureF;
int TempCount = 2;  //This is for the delay counting seconds before reading water temperature

#define AWS_IOT_PUBLISH_TOPIC "esp32/1/data"  //Where to send MQTT messages
#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/sub"   //Where to recieve MQTT messages

WiFiClientSecure net = WiFiClientSecure();
MQTTClient AWSclient = MQTTClient(1024);  // Increase the buffer size for larger messages
int DataTimer = 0;                        //Variable that counts seconds before publishing data

const char* ntpServer = "us.pool.ntp.org";  //ntp server and time info
struct tm timeinfo;
char dateBuff[50];  //50 chars should be enough
char timeBuff[25];  //25 chars should be enough
String data_time;
String data_date;

char reset_dateBuff[50];  //these variables are loaded from preferences file, and compared to current time
char reset_timeBuff[25];
const char* reset_data_time;
int reset_data_date;

int AVG_counter = 0;  //Keeps track of count for averaging flow rate
float Flow_AVG = 0;   //variabe for average flow rate
float Flow_sum = 0;

const int Flow_Input_Pin = 16;  //variables for inflow sensor 1
float InFlowSetPoint = 0;       // Setpoint for flow control in L/min
float Liters_per_min_in = 0;
float Liters_per_cycle_in = 0.0;
float InCalFactor = 0.0;

const int Flow_Output_Pin = 7;  //variables for outflow sensor 2
float OutFlowSetPoint = 0;      // Setpoint for flow control in L/min
float Liters_per_min_out = 0;
float Liters_per_cycle_out = 0;
float OutCalFactor = 0;


String Bin_Status = "FILL";  //Flag for bin FILL or DRAIN
float Bin_Volume = 0;
float currentWL = 0;
float futureWL = 0;
float WLdif = 0;
float WLvolume = 0;
float WLspeed = 0;

int M1_Open = 21;   //variables for Motor 1.    the pins GPIO35, GPIO36 and GPIO37 are used for the internal communication between ESP32-S3 and SPI flash/PSRAM memory, thus not available for external use.
int M1_Close = 47;  //the pins GPIO19, GPIO25 are USB -/+
int DetectOpen_M1 = 2;
int DetectClose_M1 = 1;
float M1_Time = 0;

int M2_Open = 40;  //variables for Motor 2
int M2_Close = 41;
int DetectOpen_M2 = 38;
int DetectClose_M2 = 39;
float M2_Time = 0;

//bool M1_On = false;    // keep track of the Motor state
millisDelay M1_Delay;  // Used for how long to turn motor1 on or off
float M1_Milis = 0;
//bool M2_On = false;    // keep track of the Motor state
millisDelay M2_Delay;  // Used for how long to turn motor2 on or off
float M2_Milis = 0;

#define WDT_TIMEOUT 60  //Watchdog Timer (WDT) in Seconds

const char* SOFT_SSID = "ESP32-DataLogger";  //NETWORK CREDENTIALS for WiFi access point
const char* SOFT_PASSWORD = "123test123";

AsyncWebServer AsyncServer(80);  //setup server on port 80 (HTML)
IPAddress IP;
bool Admin_Flag = false;  //variable to determine if the admin page is shown (true)

byte mac[6];  //For MAC address
char mac_Id[18];

// 'wifi icon', 12x12px
const unsigned char wifi_logo[] PROGMEM = { 0x00, 0x00, 0xf0, 0x00, 0xfc, 0x03, 0x07, 0x0e, 0xf9, 0x09, 0x0c, 0x03, 0xf0, 0x00, 0x98, 0x01,
                                            0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x00, 0x00 };

volatile int Input_Pulse_Count = 0;   //counts the pulses from M! flowmeter
volatile int Output_Pulse_Count = 0;  //counts the pulses from M2 flowmeter
volatile int In_Pulse_Total = 0;
volatile int Out_Pulse_Total = 0;

unsigned long Current_Time, Loop_Time, Cal_Timer, TOF_Timer;  //Used to check if we should read sensors and update variables

String FillValvePosition = "";   //flag used for fill ball valve position
String DrainValvePosition = "";  //flag used for drain ball valve position

#define High_Liquid_Detection_Pin 18  //Pins used for pump, and high/low level sensors
#define Low_Liquid_Detection_Pin 17   //yellow wire 17, orange wire 18
#define Pump_Relay_Pin 42

bool Pump_Flag = false;  //flag used for pump status

//#define TOF_SDA 11  //variables for Time of Flight sensor (laser range)
//#define TOF_SCL 12    //Sensor is on Wire1 I2C
//VL53L0X TOFsensor;
VL53L4CD TOFsensor(&Wire1, 8);
//VL53L4CX TOFsensor(&Wire1, 8);
//SFEVL53L1X TOFsensor(Wire1);

int TOFmeasure = 0;  //Measurment in mm from TOF sensor
int TOFempty = 0;    //Offset for an empty bin
int Bin_Depth = 0;   //Variable for calculated bin depth based off TOF measurement

MedianFilter TOFreadings(10, 0);  //Used to reduce noise in the TOF sensor using a rolling average of 10 samples

//**************  For some reason these functions need to go above setup  *****************//

void IRAM_ATTR Detect_Rising_Edge_Input()  // Counts the number of pulses from the inflow meter
{
  Input_Pulse_Count = Input_Pulse_Count + 1;
  In_Pulse_Total = In_Pulse_Total + 1;
  if (In_Pulse_Total >= 100000) { In_Pulse_Total = 0; }
}

void IRAM_ATTR Detect_Rising_Edge_Output()  // Counts the number of pulses from the outflow meter
{
  Output_Pulse_Count = Output_Pulse_Count + 1;
  Out_Pulse_Total = Out_Pulse_Total + 1;
  if (Out_Pulse_Total >= 100000) { Out_Pulse_Total = 0; }
}
//**********************************************************************//

void setup()  //setup function, used to intialize various components.  This only runs once
{
  Serial.begin(115200);  //Begin serial connection
  Serial.println("");    //Clear the serial buffer on reset
  delay(100);

  esp_task_wdt_deinit();  //wdt is enabled by default, so we need to deinit it first
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,  //
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true,  // Trigger panic if watchdog timer is not reset
  };

  esp_task_wdt_init(&wdt_config);  //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);          //add current thread to WDT watch

  rgbLedWrite(LED_PIN, 0, 0, 0);  //Turn onboard LED off

  void vApplicationStackOverflowHook();  //I dont fully understand this, it was part of example code for overflow handeling

  Wire.begin(SDA, SCL);  //Beigin I2C bus for Oled Display
  //Wire1.begin(TempSDA, TempSCL);    //Begin I2C bus for sensors
  //Wire1.begin(TOF_SDA, TOF_SCL);    //Begin I2C bus for sensors
  //TOFsensor.setBus(&Wire1);         //Switch TOF sensor to Wire1

  WiFi.mode(WIFI_MODE_APSTA);  //Set Wifi mode

  // Serial.print("CPU0 reset reason: ");
  //Serial.println(rtc_get_reset_reason(0));


  //reset_reason(rtc_get_reset_reason(0));
  //Serial.print("CPU1 reset reason: ");
  //Serial.println(rtc_get_reset_reason(1));

  //print_reset_reason(rtc_get_reset_reason(1));

  esp_reset_reason_t r = esp_reset_reason();
  Serial.printf("\r\nReset reason %i - %s\r\n", r, resetReasonName(r));

  // Initialize 0.96 OLED display
  Serial.println("Initializing display");
  display.init();
  display.clear();
  display.drawString(24, 0, "Initializing...");
  display.drawString(34, 18, "TIDES");
  display.drawString(18, 30, "Tiny, Integrated");
  display.drawString(0, 42, "Dinural Event Simulator");
  display.flipScreenVertically();
  display.display();
  Serial.println("Display Ready");
  delay(500);

  SPIFFS_Status.reserve(64);  //Allocate memory for 64 characters
  if (!SPIFFS.begin(true)) {  //Mount SPIFFS, the onboard flash storage used for Error Log
    Serial.println("An Error has occurred mounting SPIFFS");
    display.clear();
    display.drawString(24, 0, "Initializing Storage");
    display.drawString(6, 16, "Failed to mount SPIFFS");
    display.display();
    SPIFFS_Status = ("<font color=\"red\">Fail<font color=\"black\">");
    delay(3000);
  } else {
    display.clear();
    display.drawString(24, 0, "Initializing Storage");
    display.drawString(6, 16, "SPIFFS Mount: Success!");
    display.display();
    delay(200);
    Serial.println("SPIFFS Mount: Success!");
    SPIFFS_Status = ("<font color=\"green\"><b>GOOD<b><font color=\"black\">");

    SPIFFS.remove("/ErrorLog.txt");
    if (SPIFFS.exists("/ErrorLog.txt") == 0) {  //if the Error Log file doesn't exist, create it
      File errLog = SPIFFS.open("/ErrorLog.txt");
      Serial.println("ErrorLog file doens't exist...Creating file");
      writeFile(SPIFFS, "/ErrorLog.txt", "Date,Error \r\n");  //create file and set header for the Error Log
    } else {
      Serial.println("ErrorLog found");
      errorMSG = "Error: System Reset - ";  //Log the reset reason once we know the error log exists
      errorMSG += resetReasonName(r);
      LogError(errorMSG);
    }
  }


  /*  This section of code is for saving a preferences file to the ESP32 eproom, but now we save it to the SD card
  myPreferences.begin("myPrefs", false);                  //Create the namespace for preferences (true = RO, false = RW)
  Bin_Status = myPreferences.getString("Bin_State", "");  //Load previous state before reset
  if (Bin_Status == "") {
    Bin_Status = "FILL";
    myPreferences.putString("Bin_State", Bin_Status);
  }

  Liters_per_cycle_in = myPreferences.getFloat("inCycle", 0);
  if (Liters_per_cycle_in == ""){Liters_per_cycle_in = 0;}
  Liters_per_cycle_out = myPreferences.getFloat("outCycle", 0);
  if (Liters_per_cycle_out = ""){Liters_per_cycle_out = 0;}
  myPreferences.putString("Bin_State", "DRAIN");
  myPreferences.end();
  */

  //Initialize the SD card
  Serial.println("Initializing SD card...");
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  SD_Status.reserve(64);  //Allocate memory for 64 characters
  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR - SD card initialization failed!");
    display.drawString(6, 32, "SD: Initialization Failed!");
    display.display();
    delay(3000);
    SD_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
  } else {

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("No SD card attached");
      display.drawString(6, 32, "SD: No Card");
      display.display();
      delay(3000);
      SD_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
    } else {
      display.drawString(6, 32, "SD Mount: Success!");
      display.display();
      delay(200);
      SD_Status = ("<font color=\"green\"><b>GOOD<b><font color=\"black\">");  //set the flag that the SD card mounted successfully
    }
  }
  //SD.remove("/preferences.txt");  //uncomment this line to reset the preferences file on reboot

  Bin_Status.reserve(12);  //Allocates memory for 12 characters

  //open the preferences file (last saved state)
  WL_Data.reserve(56);  //Allocate memory for 56 characters (probably larger than needed)
  File prefs = SD.open("/preferences.txt");
  if (!prefs) {  //if the file doesn't exist, create it
    Serial.println("File doens't exist...Creating file");
    //Set the parameters to the default values (1)WL Data file, (2)Bin Status, (3)Liters per cycle in, (4)LPC out, (5)TOF Empty bin, (6)Bin Volume, (7)Bin Depth, (8)CSV line, (9)Level timer, (10)time (now), (11)Incal Factor, (12)Outcal Factor
    //writeFile(SD, "/preferences.txt", "GH02_2023_WL2.csv,FILL,0,0,400,0,0,0,0,0,1.99,1.90\r\n");  //Set the default for Preferences.txt

    WL_Data = "GH02_2023_WL2.csv";
    Bin_Status = "FILL";
    Liters_per_cycle_in = 0.0;
    Liters_per_cycle_out = 0.0;
    TOFempty = 400;
    Bin_Volume = 0;
    Bin_Depth = 0;
    CSV_linecount = 0;
    Level_timer = 0;
    reset_data_date = 0;
    InFlowSetPoint = 0.0;
    OutFlowSetPoint = 0.0;
    InCalFactor = 1.99;
    OutCalFactor = 1.90;
    UpdatePrefs();
  } else {
    Serial.println("Preferences file already exists");  //if preferences file exists, extract the relevant data

    int field_count = 0;
    while (prefs.available()) {
      String field = prefs.readStringUntil(',');  // comma character is discarded from buffer
      field_count++;                              // increment field count by 1
      if (field_count == 1) {
        WL_Data = field;
        Serial.print("WL Datafile: ");
        Serial.print(WL_Data);
      } else if (field_count == 2) {
        Bin_Status = field;
        Serial.print(" Bin Status: ");
        Serial.print(Bin_Status);
      } else if (field_count == 3) {
        Liters_per_cycle_in = field.toFloat();
        Serial.print("  L/cycle in: ");
        Serial.print(Liters_per_cycle_in);
      } else if (field_count == 4) {
        Liters_per_cycle_out = field.toFloat();
        Serial.print("  L/cycle out: ");
        Serial.println(Liters_per_cycle_out);
      } else if (field_count == 5) {
        TOFempty = field.toFloat();
        Serial.print("Empty Offset: ");
        Serial.print(TOFempty);
      } else if (field_count == 6) {
        Bin_Volume = field.toFloat();
        Serial.print(" Bin Volume: ");
        Serial.print(Bin_Volume);
      } else if (field_count == 7) {
        Bin_Depth = field.toFloat();
        Serial.print(" Bin Depth: ");
        Serial.print(Bin_Depth);
      } else if (field_count == 8) {
        CSV_linecount = field.toInt();
        Serial.print(" CSV Line: ");
        Serial.print(CSV_linecount);
      } else if (field_count == 9) {
        Level_timer = field.toInt();
        if (Level_timer <= -1) { Level_timer = 0; }
        Serial.print(" Level count: ");
        Serial.println(Level_timer);
      } else if (field_count == 10) {                //This section takes the time from the last reset, and corrects accordingly. (in case of long delay without power)
        /*                                          //However it doesnt work because the ESP32 hasn't recieved date/time from NTP server yet
        reset_data_date = field.toInt();             
        int reset_diff = (now() - reset_data_date);  //determine the difference (in seconds) from now until last save
        Serial.print(reset_diff);
        Serial.print(" seconds since last save");
        if (reset_diff < 900) {  //if its been less than 15 minutes (900 seconds)
          Level_timer = (Level_timer + reset_diff);
          Serial.print(" Level timer set to: ");
          Serial.println(Level_timer);
          if (Level_timer > 900) {  //if the new timer is over 900, switch to the next water level
            int countjump = (Level_timer / 900);
            int remainder = (Level_timer - (900 * countjump));
            CSV_linecount = (CSV_linecount + countjump);
            Level_timer = remainder;
            Serial.print("Jump CSV counter to ");
            Serial.print(CSV_linecount);
            Serial.print(" and new timer to ");
            Serial.println(Level_timer);
          } else {
            Serial.println("WARNING:  UNIT HAS BEEN OFF FOR OVER 15 MINUTES");
          }  // if its been more than 900 seconds since last reset
          */
        }
      } else if (field_count == 11) {
        InCalFactor = field.toFloat();
        Serial.print("Inflow Calibration factor set to: ");
        Serial.println(InCalFactor);
      } else if (field_count == 12) {
        OutCalFactor = field.toFloat();
        Serial.print("Outflow Calibration factor set to: ");
        Serial.println(OutCalFactor);
      }
    }
    /*
    Serial.print("Bin Status: ");  //print out the loaded data for confirmation
    Serial.println(Bin_Status);
    Serial.print("Liters in: ");
    Serial.print(Liters_per_cycle_in);
    Serial.print(" Cal: ");
    Serial.print(InCalFactor);
    Serial.print(" Liters out: ");
    Serial.print(Liters_per_cycle_out);
    Serial.print(" Cal: ");
    Serial.println(OutCalFactor);
    */
  }
  prefs.close();


  data_date.reserve(24);  //Allocate memory for 24 characters
  data_time.reserve(12);  //Allocate memory for 12 characters
  errorLog.reserve(256);  //Allocate memory for 256 bytes
  errorMSG.reserve(128);  //Allocate memory for 128 bytes
  // check to see if the data file exits
  File root = SD.open("/data/", "r");
  if (!root || !root.isDirectory()) {  //check if SD has data folder, if not make one
    if (SD.mkdir("/data/") != 1) {
      Serial.println("Failed to open/make data directory");
      errorMSG = "Error:  Failed to open/make data directory";
      LogError(errorMSG);
      //errorLog = String(data_date + data_time + "," + errorMSG + "\r\n");
      //appendFile(SPIFFS, "/ErrorLog.txt", errorLog.c_str());
      SD_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
    } else {
      Serial.println("Data folder added to SD card");
    }
  }

  File datafile = SD.open("/data/datafile.txt");
  if (!datafile) {  //if the file doesn't exist, create it and add headers
    Serial.println("File doens't exist...Creating file");
    writeFile(SD, "/data/datafile.txt", "mac_id,date,time,sample_time,device_id,air_temp,humidity,water_temp,inflow,inflow_set,liters_in,outflow,outflow_set,liters_out,flow_avg,empty_offset,bin_volume,bin_depth,TOF_mm,bin_status,valve1,valve2,pump,reset,CSV_linecount,wifi,aws \r\n");  //create header line
  } else {
    Serial.println("Data file already exists");
  }
  datafile.close();

  PSRAM_Status.reserve(64);  //Allocate memory for 64 characters
  if (psramInit()) {         //The ESP32N8R2 has 2mb of QSPI PSRAM
    Serial.println("\nPSRAM is correctly initialized.");
    PSRAM_Status = ("<font color=\"green\"><b>GOOD<b><font color=\"black\">");
    display.drawString(6, 48, "PSRAM Mount: Success!");
    display.display();
    int n_elements = 128000;
    //int n_elements = 1000;  //This section stores the parsed datafile in PSRAM (2mb)
    column_Date = (char*)ps_malloc(n_elements * sizeof(char));
    column_Level = (float*)ps_malloc(n_elements * sizeof(float));
    //heap_caps_malloc_extmem_enable(5000);
    Serial.printf("PSRAM size:                 %lu bytes\n", ESP.getPsramSize());
    Serial.printf("PSRAM available memory:     %lu bytes\n", ESP.getFreePsram());
    delay(2000);
  } else {
    Serial.println("PSRAM not available");
    errorMSG = "Error: PSRAM not available";
    LogError(errorMSG);
    PSRAM_Status = ("<font color=\"red\">Fail<font color=\"black\">");
    display.drawString(6, 48, "PSRAM Mount: Fail!");
    display.display();
    delay(3000);
  }


  LoadWLData();  //Load the WL Data CSV into an array


  //Section to initialize Time of Flight sensor
  //TOFsensor.init();
  //TOFsensor.setTimeout(500);
  Wire1.begin(TempSDA, TempSCL, 300000);  //Begin I2C bus for TOF and SHT30 sensors
  pinMode(8, OUTPUT);                     //Toggles the TOF sensor OFF/ON
  digitalWrite(8, LOW);
  delay(100);
  digitalWrite(8, HIGH);
  delay(100);

  Serial.print("Initalizing TOF Sensor: ");
  /*
  int initAttempt = 0;
  TOFsensor.begin();
  while ((TOFsensor.InitSensor(29) != 0) && (initAttempt <= 10)) {         //0 for success, 255 for failure
    initAttempt++;
    Serial.print("...");
    delay(100);
  }
  */
  if (TOFsensor.InitSensor() != 0) {  // returns 0 on a good init
    Serial.println("Failed to detect or initialize TOF sensor");
    errorMSG = "Error: Failed to detect or initialize TOF sensor";
    LogError(errorMSG);
    display.clear();
    display.drawString(24, 0, "Initializing Sensors");
    display.drawString(6, 16, "TOF sensor: Fail");
    display.display();
    delay(2000);
  } else {
    //TOFsensor.VL53L4CX_StartMeasurement();
    Serial.println("TOF Sensor Ready");
    display.clear();
    display.drawString(6, 0, "Initializing Sensors");
    display.drawString(6, 16, "TOF sensor: Good");
    display.display();
    delay(200);
  }
  TOFsensor.VL53L4CD_SetRangeTiming(200, 0);  //When the temperature increases, the ranging value is affected by an offset of 1.3 mm per degree Celsius change. This value is an offset and not a gain, and it does not depend on the target distance.
  //TOFsensor.VL53L4CD_SetRangeTiming(80, 120);    //The device embeds a feature that allows compensation for the temperature variation effect. When the ranging is started, a self-calibration is performed once and this allows to remove the ranging drift.
  TOFsensor.VL53L4CD_StartRanging();  //To get the most accurate performances, perform a self-calibration when temperature varies. To self-calibrate, call the functions “stop” and “start”, in sequence.

  /*
  //TOFsensor.begin();
  //TOFsensor.VL53L4CD_Off();
  //TOFsensor.VL53L4CD_On();

  //TOFsensor.VL53L4CX_Off();
  int initAttempt = 0;
  Serial.println (TOFsensor.InitSensor());      
  while ((TOFsensor.InitSensor() != 0) && (initAttempt <= 10)) {         //0 for success, 255 for failure
    initAttempt++;
    Serial.print("...");
    delay(100);
  }

  if (TOFsensor.InitSensor() != 0) {                  //0 for success, 255 for failure
    Serial.println("Failed to detect or initialize TOF sensor");
    display.clear();
    display.drawString(24, 0, "Initializing Sensors");
    display.drawString(6, 16, "TOF sensor: Fail");
    display.display();
    delay(2000);
  } else {
    TOFsensor.VL53L4CD_SetRangeTiming(200, 0);
    TOFsensor.VL53L4CD_StartRanging();
    //TOFsensor.VL53L4CX_StartMeasurement();
    Serial.println("TOF Sensor Ready");
    display.clear();
    display.drawString(6, 0, "Initializing Sensors");
    display.drawString(6, 16, "TOF sensor: Good");
    display.display();
    delay(200);

    //When the temperature increases, the ranging value is affected by an offset of 1.3 mm per degree Celsius change. This value is an offset and not a gain, and it does not depend on the target distance.
    //The device embeds a feature that allows compensation for the temperature variation effect. When the ranging is started, a self-calibration is performed once and this allows to remove the ranging drift.
    //To get the most accurate performances, perform a self-calibration when temperature varies. To self-calibrate, call the functions “stop” and “start”, in sequence.
  } */

  pinMode(0, INPUT_PULLUP);  //Sets the input pins for BOOT button (System Pause)

  pinMode(Flow_Output_Pin, INPUT_PULLUP);  //Sets the input pins for both flow sensors
  pinMode(Flow_Input_Pin, INPUT_PULLUP);

  pinMode(DetectOpen_M1, INPUT_PULLUP);  //Sets the input pins for both motors
  pinMode(DetectClose_M1, INPUT_PULLUP);
  pinMode(DetectOpen_M2, INPUT_PULLUP);
  pinMode(DetectClose_M2, INPUT_PULLUP);

  pinMode(oneWireBus, INPUT_PULLUP);  //Sets the input pins for ds18b20 water temperature sensor

  //This sets the pins to control fill ballvalve (motor 1) and sets the pins to low (off)
  pinMode(M1_Open, OUTPUT);
  pinMode(M1_Close, OUTPUT);
  digitalWrite(M1_Open, LOW);
  digitalWrite(M1_Close, HIGH);
  //M1_On = false;

  //This sets the pins to control drain ballvalve (motor 2) and sets the pins to low (off)
  pinMode(M2_Open, OUTPUT);
  pinMode(M2_Close, OUTPUT);
  digitalWrite(M2_Open, LOW);
  digitalWrite(M2_Close, HIGH);
  //M2_On = false;

  Serial.println("Closing both valves for 10000ms");  //Start the program with both valves fully closed
  M1_Delay.start(10000);
  M2_Delay.start(10000);
  M1_Milis = millis();
  M2_Milis = millis();

  //This sets the pins for the pump, and High/Low water level sensors and turns the pump off
  pinMode(Low_Liquid_Detection_Pin, INPUT);
  pinMode(High_Liquid_Detection_Pin, INPUT);
  pinMode(Pump_Relay_Pin, OUTPUT);
  digitalWrite(Pump_Relay_Pin, LOW);

  Serial.println("Starting environmental sensors");
  //dht.begin();      //Initialize DHT22 Humidity/temperature sensor

  Serial.print("SHT30 test: ");  //Configure Temp/Humidity monitor
  if (!sht31.begin(0x44)) {      // Set to 0x45 for alternate i2c addr
    Serial.println("No SHT31 temp sensor");
    errorMSG = "Error: No SHT30 temp sensor";
    LogError(errorMSG);
    SHT31_Flag = false;
    display.drawString(6, 32, "Air T/H: Fail");
    display.display();
    delay(2000);
  } else {
    SHT31_Flag = true;  //turn flag to true, indicating no error
    Serial.println("Air T/H: Good");
    display.drawString(6, 32, "Air T/H: Good");
    display.display();
    delay(200);
  }

  sensors.begin();  //Initialize ds18b20 temperature sensor
  //sensors.setResolution(11);
  deviceCount = sensors.getDeviceCount();
  if (deviceCount < 1) {
    Serial.println("No ds18b20 water temp sensor found");
    errorMSG = "Error: No ds18b20 water temp sensor found";
    LogError(errorMSG);
    display.drawString(6, 48, "Water Temp: Fail");
    display.display();
    delay(2000);
  } else {
    Serial.print(deviceCount);
    Serial.println(" ds18b20 water temp sensors found");
    display.drawString(6, 48, "Water Temp: Good");
    display.display();
    delay(2000);
  }

  Serial.println("Attaching Interupts environmental sensors");
  attachInterrupt(Flow_Output_Pin, Detect_Rising_Edge_Output, FALLING);  //Detects pulses from flow sensor 2
  attachInterrupt(Flow_Input_Pin, Detect_Rising_Edge_Input, FALLING);    //Detects pulses from flow sensor 1

  Serial.println("Connect AWS");
  connectAWS();  //Connect to Wifi & Amazon database

  configTzTime("EST5EDT,M3.2.0,M11.1.0", ntpServer);  //set timezone to EST and update time variables

  if (WiFi.status() == WL_CONNECTED) { printLocalTime(5000); }  //Delay up to 5 seconds to try to aquire time from NTP server

  //LoadWLData();  //Load the WL Data CSV into an array
  //StartWebServer();

  /*
  if (reset_data_date != data_date){  //check to see if the device has been offline until the next day
    Serial.println ("Device reset is off by date");
  } else if {((reset_data_date == data_date) && (reset_data_time != data_time))}  //check to see how long since last reset
  */

  Current_Time = millis();  //These are used for controlling the loop interval
  Loop_Time = Current_Time;
  TOF_Timer = millis();


  FillValvePosition.reserve(12);     //allocate memory for 12 characters
  DrainValvePosition.reserve(12);    //allocate memory for 12 characters

  //this is the old code for enabling the watchdog - no longer in use
  //esp_task_wdt_init(WDT_TIMEOUT, true);  // Initialize ESP32 Task WDT
  //esp_task_wdt_add(NULL);                // Subscribe to the Task WDT
  esp_task_wdt_reset();
  delay(1);  //VERY VERY IMPORTANT for Watchdog Reset to apply. At least 1 ms

}  // end setup

void loop()  //Main loop for code, runs continously after void setup()
{
  checkTimers();  //Check the motor timers

  /*  
  PauseButton = digitalRead(0);    //Check if BOOT button on ESP32 has been pressed to pause system
  //if (PauseButton == LOW){Serial.println("GPIO 0 Low");}

  if (PauseButton != PauseButtonState){     //Code to debounce accidental button presses
    lastDebounceTime = millis();
    PauseButtonState = PauseButton;
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {  //Check if button state is the same after debounce delay 
    if (PauseSteadyState == LOW && PauseButton == HIGH) {
      //Serial.print("GPIO 0 pressed: ");
      //Serial.println(PauseButton);           //reset the button if different
      lastDebounceTime = 0;
      if (SystemPause == false){
        SystemPause = true;                     //Turn on the System Pause Flag
        Show_Page = 5;                          //Show the Pause Screen
        UpdateDisplay();

        digitalWrite(Pump_Relay_Pin, LOW);      //Turn the pump off

        digitalWrite(M1_Open, LOW);             //Close both valves for 10 seconds
        digitalWrite(M1_Close, HIGH);
        digitalWrite(M2_Open, LOW);
        digitalWrite(M2_Close, HIGH);

        M1_Delay.start(10000);
        M2_Delay.start(10000);
        M1_Milis = millis();
        M2_Milis = millis();

        Serial.println("Pause Button Pressed!");
        } else if (SystemPause == true){        // Unpause if the system is paused
          SystemPause = false;
          Serial.println("Unpause");
        }
    }

    PauseSteadyState = PauseButton;
  }
  */

  Current_Time = millis();

  if (Current_Time >= (Loop_Time + 1000)) {
    esp_task_wdt_reset();
    delay(1);
  }  //Reset Watch Dog Timer after loop successfully completes

  if (Current_Time >= (Loop_Time + 1000) && (SystemPause == false))  //run this loop every second
  {
    //TOFsensor.VL53L4CD_ClearInterrupt();   //clear the sensor interupt incase it wasnt read successfully

    Serial.print("RAM: ");
    Serial.print(ESP.getHeapSize());
    Serial.print(" Free: ");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" Max: ");
    Serial.print(ESP.getMaxAllocHeap());
    Serial.print(" Low: ");
    Serial.print(ESP.getMinFreeHeap());
    Serial.print(" xPort:");
    Serial.println(xPortGetFreeHeapSize());

    //Serial.print("Heap check: ");
    //heap_caps_check_integrity_all();
    if (!heap_caps_check_integrity(MALLOC_CAP_8BIT, true)) {
      //heap_trace_dump();
      //abort();
      Serial.println("!! HEAP OVERFLOW !!");
    }

    checkWebClient();

    /* //temporary debug code to make sure the column array is parsing correctly
    Serial.printf("CSV Value: %f\n", column_Level[CSV_linecount]);
    //myPreferences.begin("myPrefs", false);  //Open the namespace for preferences (True = RO, False = RW)
    Serial.print("Column_level [");  //temporary debug code to make sure the column array is parsing correctly
    Serial.print(CSV_linecount);
    Serial.print("] value: ");
    Serial.println(column_Level[CSV_linecount]);
    */
 
    printLocalTime(5);  //update time variables
    AWSclient.loop();   // check for MQTT messages and keep the AWS client alive

    //Loop_Time = Current_Time;
    //Serial.println(Pulse_Count);
    //flowRate = ((1000.0 / (millis() - previousMillis)) * pulse1Sec) / calibrationFactor;  This accounts for the fact that the loop isnt exactly every second
    //Liters_per_min_in = ((((1000.0 / (millis() - Loop_Time)) * (Input_Pulse_Count / 1.99)) * 60) / 1000);  // Code was originally (Pulse_Count * 60 / 7.5);
    int loopFactor = (1000 / (millis() - Loop_Time));  //Take into account that its possible more than 1 second has elapsed
    Loop_Time = Current_Time;

    Liters_per_min_in = (((Input_Pulse_Count / InCalFactor) * 60) / 1000)*loopFactor;  //Recalibrate and change the pulse count
    Liters_per_cycle_in = (Liters_per_cycle_in + (Liters_per_min_in / 60));
    Liters_per_min_out = (((Output_Pulse_Count / OutCalFactor) * 60) / 1000)*loopFactor;  //Recalibrate and change the pulse count
    Liters_per_cycle_out = (Liters_per_cycle_out + (Liters_per_min_out / 60));

    Bin_Volume = (Bin_Volume + (Liters_per_min_in / 60) - (Liters_per_min_out / 60));

    //myPreferences.begin("myPrefs", false);  //Create the namespace for preferences (true = RO, false = RW)
    //myPreferences.end();

    Serial.print(Input_Pulse_Count);
    Serial.print(" Pulses IN, Total: ");
    Serial.print(In_Pulse_Total);
    Input_Pulse_Count = 0;

    Serial.print("    ");

    Serial.print(Output_Pulse_Count);
    Serial.print(" Pulses OUT, Total: ");
    Serial.println(Out_Pulse_Total);
    Output_Pulse_Count = 0;

    Serial.print("InCal: ");
    Serial.print(InCalFactor);
    Serial.print(" OutCal: ");
    Serial.print(OutCalFactor);
    Serial.print(" LoopF: ");
    Serial.println("loopFactor");

    Serial.print(Liters_per_min_in, 2);
    Serial.print(" Liters per minute in, ");
    Serial.print(Liters_per_min_out, 2);
    Serial.println(" Liters per minute out.   ");

    Serial.print("[");
    Serial.print(Bin_Status);
    if (Bin_Status == "FILL") {
      Serial.print("] Inflow Set to: ");
      Serial.print(InFlowSetPoint);
      Serial.print(" L/min Avg: ");
      Serial.print(" ");
      Serial.print(Flow_AVG);
      Serial.print(Liters_per_cycle_in);
      Flow_sum = (Flow_sum + Liters_per_min_in);
    } else if (Bin_Status == "DRAIN") {
      Serial.print("] Outflow Set to: ");
      Serial.print(OutFlowSetPoint);
      Serial.print(" L/min Avg: ");
      Serial.print(Flow_AVG);
      Serial.print(Liters_per_cycle_out);
      Flow_sum = (Flow_sum + Liters_per_min_out);
    }
    Serial.println(" Liters/Cycle  ");

    Serial.print("Bin Volume: ");
    Serial.print(Bin_Volume);
    Serial.print(" Bin Depth: ");
    Serial.print(Bin_Depth);
    Serial.println("mm");

    AVG_counter++;      //This takes an average every 10 samples and then resets
    if (AVG_counter >= 10) {
      Flow_AVG = (Flow_sum / AVG_counter);
      Serial.print("Flow Average: ");
      Serial.println(Flow_AVG);
      AVG_counter = 0;
      Flow_sum = 0;
    }

    //This section is for reading water temperature from ds18b20 sensor
    deviceCount = sensors.getDeviceCount();
    if (deviceCount <= 0) {
      Serial.println("No ds18b20 water temp sensor found");
      temperatureC = 0;
    } else {
      sensors.requestTemperatures();
      temperatureC = sensors.getTempCByIndex(0);
      //temperatureF = sensors.getTempFByIndex(0);
      Serial.print("Water Temp: ");
      Serial.print(temperatureC);
      Serial.println("ºC");
      //Serial.print("    ");
      //Serial.print(temperatureF);
      //Serial.println("ºF");
    }

    //This section is for reading air Humidity/Temp from DHT22 sensor
    /*
    s_humidity = dht.readHumidity();
    temp_C = dht.readTemperature();      // Read temperature as Celsius (the default)
    temp_F = dht.readTemperature(true);  // Read temperature as Fahrenheit (isFahrenheit = true)

    // Check if any reads failed and exit early (to try again).
    if (isnan(s_humidity) || isnan(temp_C) || isnan(temp_F)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      s_humidity = 0.0;
      temp_C = 0.0;
      temp_F = 0.0;
      //return;
    }  */

    /*
    // Compute heat index in Fahrenheit (the default)
    float hif = dht.computeHeatIndex(temp_F, s_humidity);
    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(temp_C, s_humidity, false);

    Serial.print(F("Humidity: "));
    Serial.print(s_humidity);
    Serial.print(F("%  Temperature: "));
    Serial.print(temp_C);
    Serial.print(F("°C "));
    Serial.print(temp_F);
    Serial.print(F("°F  Heat index: "));
    Serial.print(hic);
    Serial.print(F("°C "));
    Serial.print(hif);
    Serial.println(F("°F"));
    */

    temp_C = sht31.readTemperature();  //Read the Temperature and Humidity data from the sensor
    humidP = sht31.readHumidity();

    if (!isnan(temp_C)) {  // check if Temp 'is not a number'
      Serial.print("Air Temp: ");
      Serial.print(temp_C);
      Serial.print("C   ");

      SHT31_Flag = true;  //turn flag to false, indicating no error
      //if (SD_Flag == true) { rgbLedWrite(LED_PIN, 0, 40, 0); }  //turn onboard led green
    } else {
      temp_C = 0.0;
      SHT31_Flag = false;  //turn flag to false, indicating an error
      Serial.print("Failed to read temperature   ");
      //errorMSG = "Error: Failed to read temp/humidity";
      //LogError(errorMSG);  //Turned off because this will fill the log if the sensor is not reading
    }

    if (!isnan(humidP)) {  // check if humidity 'is not a number'
      Serial.print("Hum. % = ");
      Serial.println(humidP);
    } else {
      humidP = 0.0;
      Serial.println("Failed to read humidity");
    }

    //This section is for controlling the pump based on High/Low water level sensors
    if (digitalRead(High_Liquid_Detection_Pin) == HIGH) {
      Pump_Flag = true;
      Serial.println("High Level Sensor: Detected, turning pump on!");
    }

    if (digitalRead(Low_Liquid_Detection_Pin) == LOW) {
      Pump_Flag = false;
      Serial.println("Low Level Sensor: No Liquid Detected, turning pump off!");
    }

    if (Pump_Flag == false) {    //Turn the pump OFF/ON according to the flag
      digitalWrite(Pump_Relay_Pin, LOW);
      Serial.println("Pump is off");
    } else if (Pump_Flag == true) {
      digitalWrite(Pump_Relay_Pin, HIGH);
      Serial.println("Pump is on");
    }

    //This section is for controlling when the grow bin is filling or draining in demo mode
    if (Demo_Mode == true) {
      if (Liters_per_cycle_in > 1.0) {
        OutFlowSetPoint = 0.5;
        Bin_Status = "DRAIN";
        Serial.println("Bin Full, switching to Drain");
        Liters_per_cycle_in = 0;
        //myPreferences.putString("Bin_State", Bin_Status);
      }

      if (Liters_per_cycle_out > 0.5) {
        InFlowSetPoint = 1.0;
        Bin_Status = "FILL";
        Serial.println("Bin Full, switching to Fill");
        Liters_per_cycle_out = 0;
        //myPreferences.putString("Bin_State", Bin_Status);
      }
    }

    //This section is for controling the fill ball valve based on water flow from the sensor
    if (Liters_per_min_in < (InFlowSetPoint - 0.01) && Bin_Status == "FILL") {
      M1_Time = abs(InFlowSetPoint - Liters_per_min_in) * 500;  // this was originally 500
      M1_Delay.start(M1_Time);
      M1_Milis = millis();
      digitalWrite(M1_Open, HIGH);
      digitalWrite(M1_Close, LOW);
      Serial.print(" ** Opening Valve1 for ");
      Serial.print(M1_Time, 4);
      Serial.println(" ms");
    }

    else if (Liters_per_min_in > (InFlowSetPoint + 0.01) && Bin_Status == "FILL") {
      M1_Time = abs(Liters_per_min_in - InFlowSetPoint) * 500;  // this was originally 500
      M1_Delay.start(M1_Time);
      M1_Milis = millis();
      digitalWrite(M1_Open, LOW);
      digitalWrite(M1_Close, HIGH);
      Serial.print(" ** Closing Valve1 for ");
      Serial.print(M1_Time, 4);
      Serial.println(" ms");
    } 
    
    else if ((Liters_per_min_in > (InFlowSetPoint + 0.01)) && (Liters_per_min_in < (InFlowSetPoint - 0.01)) && Bin_Status == "FILL") {
      digitalWrite(M1_Open, LOW);
      digitalWrite(M1_Close, LOW);
      M1_Milis = millis();
      M1_Time = 1;
      M1_Delay.start(M1_Time);
      Serial.print(M1_Time, 4);
      Serial.println(" ** Perfect, not moving valve1**");
    }

    FillValvePosition = getFillValvePosition();
    //if (strcmp(Bin_Status, "DRAIN") == 0 && strcmp(FillValvePosition, "CLOSED") == 1) {

    if ((Bin_Status == "DRAIN") && (FillValvePosition != "CLOSED")) {  //Close the first valve when we want to drain the bin
      digitalWrite(M1_Open, LOW);
      digitalWrite(M1_Close, HIGH);
      Serial.print(FillValvePosition);
      Serial.println(" valve.  Closing fill ballvalve, draining bin");
    } else if ((Bin_Status == "DRAIN") && (FillValvePosition == "CLOSED")) {  //Turn motor1 off when closed
      digitalWrite(M1_Open, LOW);
      digitalWrite(M1_Close, LOW);
      Serial.println("Fill valve closed, turning off motor1");
    }

    //This section is for controling the drain ball valve based on water flow from the sensor
    if (Liters_per_min_out < (OutFlowSetPoint - 0.01) && Bin_Status == "DRAIN") {
      M2_Time = abs(OutFlowSetPoint - Liters_per_min_out) * 500;  // this was originally 500
      M2_Delay.start(M2_Time);
      M2_Milis = millis();
      digitalWrite(M2_Open, HIGH);
      digitalWrite(M2_Close, LOW);
      Serial.print(" ** Opening Valve2 for ");
      Serial.print(M2_Time, 4);
      Serial.println(" ms");
    }

    else if (Liters_per_min_out > (OutFlowSetPoint + 0.01) && Bin_Status == "DRAIN") {
      M2_Time = abs(Liters_per_min_out - OutFlowSetPoint) * 500;  // this was originally 500
      M2_Delay.start(M2_Time);
      M2_Milis = millis();
      digitalWrite(M2_Open, LOW);
      digitalWrite(M2_Close, HIGH);
      Serial.print(" ** Closing Valve2 for ");
      Serial.print(M2_Time, 4);
      Serial.println(" ms");

    } else if ((Liters_per_min_out > (OutFlowSetPoint + 0.01)) && (Liters_per_min_out < (OutFlowSetPoint - 0.01)) && Bin_Status == "DRAIN") {
      digitalWrite(M2_Open, LOW);
      digitalWrite(M2_Close, LOW);
      M2_Time = 1;
      M2_Delay.start(M2_Time);
      M2_Milis = millis();
      Serial.print(M2_Time, 4);
      Serial.println(" ** Perfect, not moving valve2**");
    }

    DrainValvePosition = getDrainValvePosition();
    if ((Bin_Status == "FILL") && (DrainValvePosition != "CLOSED")) {  //Close the second valve when we want to FILL the bin
      digitalWrite(M2_Open, LOW);
      digitalWrite(M2_Close, HIGH);
      Serial.print(DrainValvePosition);
      Serial.println(" valve.  Closing drain ballvalve, filling bin");
    } else if ((Bin_Status == "FILL") && (DrainValvePosition == "CLOSED")) {  //Turn motor2 off when closed
      digitalWrite(M2_Open, LOW);
      digitalWrite(M2_Close, LOW);
      Serial.println("Drain valve closed, turning off motor2");
    }

    /*
    TOFstatus = TOFsensor.checkForDataReady();
    if (TOFstatus) {
  //while (!distanceSensor.checkForDataReady(&NewDataReady))

  int distance = TOFsensor.getDistance(); //Get the result of the measurement from the sensor
  TOFsensor.clearInterrupt();
  //distanceSensor.stopRanging();

  Serial.print("Distance(mm): ");
  Serial.print(distance);

  float distanceInches = distance * 0.0393701;
  float distanceFeet = distanceInches / 12.0;

  Serial.print("\tDistance(ft): ");
  Serial.print(distanceFeet, 2);

  Serial.println();
} */

    uint8_t TOFstatus;
    uint8_t NewDataReady = 0;
    VL53L4CD_Result_t results;
    //VL53L4CX_MultiRangingData_t MultiRangingData;
    //VL53L4CX_MultiRangingData_t *pMultiRangingData = &MultiRangingData;
    if (millis() >= (TOF_Timer + 5000)) {  //Reinitialize TOF if we havent had a reading in 5 seconds
      if (TOFsensor.InitSensor() != 0) {
        Serial.println("Failed to detect and initialize TOF sensor!");
        TOF_Timer = millis();
      } else {
        TOFsensor.VL53L4CD_SetRangeTiming(200, 0);
        TOFsensor.VL53L4CD_StartRanging();
        Serial.println("TOF Sensor Reset");
        TOF_Timer = millis();
        //When the temperature increases, the ranging value is affected by an offset of 1.3 mm per degree Celsius change. This value is an offset and not a gain, and it does not depend on the target distance.
        //The device embeds a feature that allows compensation for the temperature variation effect. When the ranging is started, a self-calibration is performed once and this allows to remove the ranging drift.
        //To get the most accurate performances, perform a self-calibration when temperature varies. To self-calibrate, call the functions “stop” and “start”, in sequence.
      }
    }

    TOFstatus = TOFsensor.VL53L4CD_CheckForDataReady(&NewDataReady);
    //TOFstatus = TOFsensor.VL53L4CX_GetMeasurementDataReady(&NewDataReady);
    Serial.print("TOF ");
    if (digitalRead(8)){Serial.print("ON");} else {Serial.print("OFF");}
    //TOFsensor.VL53L4CD_ClearInterrupt();
    Serial.print(" -> ");

    if ((!TOFstatus) && (NewDataReady != 0)) {
      TOF_Timer = millis();
      // (Mandatory) Clear HW interrupt to restart measurements
      TOFsensor.VL53L4CD_ClearInterrupt();

      // Read measured distance. RangeStatus = 0 means valid data
      TOFsensor.VL53L4CD_GetResult(&results);

      Serial.print("TOF measure: ");
      Serial.print(results.distance_mm);
      Serial.print(" mm  AVG: ");
      TOFmeasure = results.distance_mm;
      TOFreadings.in(TOFmeasure);
      Serial.print(TOFreadings.getMean());
      Serial.print(" mm Depth: ");
      Bin_Depth = (TOFempty - TOFreadings.getMean());
      Serial.print(Bin_Depth);
      Serial.print(" mm");

      /*
      status = TOFsensor.VL53L4CX_GetMultiRangingData(pMultiRangingData);
      no_of_object_found = pMultiRangingData->NumberOfObjectsFound;
      Serial.print("Depth: ");
      Serial.println(pMultiRangingData->RangeData[0].RangeMilliMeter);
      */
    }
    Serial.println("");  //print TOF status all on one line


    //This section controls when the data is logged
    DataTimer++;
    if (DataTimer >= 300) {  //Publish data every X seconds (300 = 5 min)
      Serial.println("Data Timer!");

      /*
      if (!AWSclient.connected()) {
        Serial.println("Trying to reconnect AWS");
        connectAWS();  //reconnect to AWS if disconnected
      } else {
        publishMessage();
      }  // uncomment this line publish all data to AWS database
      */

      publishMessage();
      /*
      dataEntry = String(mac_Id) + "," + String(data_date) + "," + String(data_time) + "," + String(time(nullptr)) + "," + String(Device_ID) + "," 
                + String(temp_C) + "," + String(humidP) + "," + String(temperatureC) + "," + String(Liters_per_min_in) + "," + String(InFlowSetPoint) + "," 
                + String(Liters_per_cycle_in) + "," + String(Liters_per_min_out) + "," + String(OutFlowSetPoint) + "," + String(Liters_per_cycle_out) + "," 
                + String(Flow_AVG) + "," + String(TOFempty) + "," + String(Bin_Volume) + "," + String(Bin_Depth) + "," + String(TOFreadings.getMean()) + "," 
                + String(Bin_Status) + "," + String(FillValvePosition) + "," + String(DrainValvePosition) + "," + String(Pump_Flag) + "," + String(Reset_Flag) + "," 
                + String(CSV_linecount) + "," + String(WiFi.status()) + "," + String(AWSclient.connected()) + "\r\n";
      */

      //Format the dataEntry string for saving to CSV
      dataEntry = String(mac_Id); dataEntry += ","; 
      dataEntry += String(data_date); dataEntry += ",";
      dataEntry += String(data_time); dataEntry += ",";
      dataEntry += String(time(nullptr)); dataEntry += ",";
      dataEntry += String(Device_ID); dataEntry += ",";
      dataEntry += String(temp_C); dataEntry += ",";
      dataEntry += String(humidP); dataEntry += ",";
      dataEntry += String(temperatureC); dataEntry += ",";
      dataEntry += String(Liters_per_min_in); dataEntry += ",";
      dataEntry += String(InFlowSetPoint); dataEntry += ",";
      dataEntry += String(Liters_per_cycle_in); dataEntry += ",";
      dataEntry += String(Liters_per_min_out); dataEntry += ",";
      dataEntry += String(OutFlowSetPoint); dataEntry += ",";
      dataEntry += String(Liters_per_cycle_out); dataEntry += ",";
      dataEntry += String(Flow_AVG); dataEntry += ",";
      dataEntry += String(TOFempty); dataEntry += ",";
      dataEntry += String(Bin_Volume); dataEntry += ",";
      dataEntry += String(Bin_Depth); dataEntry += ",";
      dataEntry += String(TOFreadings.getMean()); dataEntry += ",";
      dataEntry += String(Bin_Status); dataEntry += ",";
      dataEntry += String(FillValvePosition); dataEntry += ",";
      dataEntry += String(DrainValvePosition); dataEntry += ",";
      dataEntry += String(Pump_Flag); dataEntry += ",";
      dataEntry += String(Reset_Flag); dataEntry += ",";
      dataEntry += String(CSV_linecount); dataEntry += ",";
      dataEntry += String(WiFi.status()); dataEntry += ",";
      dataEntry += String(AWSclient.connected()); dataEntry += "\r\n";

      appendFile(SD, "/data/datafile.txt", dataEntry.c_str());  //append a new line of data to the datafile

      if (WL_Data == "") { WL_Data = ("GH02_2023_WL2.csv"); }  //remove this line, or add what to "default to"

      UpdatePrefs();
      /* old code, replaced with UpdatePrefs()
      //Pref = (1)WL Data file, (2)Bin Status, (3)Liters per cycle in, (4)LPC out, (5)TOF Empty bin, (6)Bin Volume, (7)Bin Depth, (8)CSV line, (9)Level timer, (10)time (now), (11)Incal Factor, (12)Outcal Factor
      String prefEntry = WL_Data + "," + String(Bin_Status) + "," + String(Liters_per_cycle_in) + "," + String(Liters_per_cycle_out) + "," + String(TOFempty) + "," + String(Bin_Volume) + "," + String(Bin_Depth) + "," + String(CSV_linecount) + "," + String(Level_timer) + "," + String(now()) + "," + String(InCalFactor) + "," + String(OutCalFactor) + "\r\n";
      writeFile(SD, "/preferences.txt", prefEntry.c_str());
      */

      DataTimer = 0;
      if (Reset_Flag == true) { Reset_Flag = false; }  //Return the flag so we only count reset once
      //myPreferences.putFloat("inCycle", Liters_per_cycle_in);
      //myPreferences.putFloat("outCycle", Liters_per_cycle_out);
    }

    //This section is for stepping through the water levels in the data sheet (15 min interval)
    Level_timer++;
    if (Level_timer >= 900 && Demo_Mode == false) {  //900 seconds = 15 min
      Serial.print("Level Timer! Column level: ");
      Serial.println(column_Level[CSV_linecount]);

      currentWL = (column_Level[CSV_linecount] / 2);
      futureWL = (column_Level[CSV_linecount + 1] / 2);
      WLdif = (futureWL - currentWL);
      WLvolume = (WLdif * .55125);  //.55125 is the area of the bin (square m LxW)
      WLspeed = (WLvolume / 15);    //15 is the # of minutes between data entries

      Serial.print("currentWL: ");
      Serial.print(currentWL);
      Serial.print(" futureWL: ");
      Serial.print(futureWL);
      Serial.print(" WLdif: ");
      Serial.print(WLdif);
      Serial.print(" WLvolume: ");
      Serial.print(WLvolume);
      Serial.print(" WLspeed: ");
      Serial.println(WLspeed);

      if ((WLdif < 0) && (Bin_Status == "FILL")) {  //Check to see if the WL difference is negative, if so switch to draining the bin
        Bin_Status = "DRAIN";
        Liters_per_cycle_in = 0;
        InFlowSetPoint = 0;
        Serial.println("Bin switching to DRAIN, LPC in 0, InSP 0");
      } else if ((WLdif > 0) && (Bin_Status == "DRAIN")) {
        Bin_Status = "FILL";
        Liters_per_cycle_out = 0;
        OutFlowSetPoint = 0;
        Serial.println("Bin switching to FILL, LPC out 0, OutSP 0");
      }

      if (Bin_Status == "FILL") {         //set the new flow point depending on if the bin is filling or draining
        InFlowSetPoint = WLspeed * 1000;  //convert from cm^3 to Liters;
        Serial.print("Setting InFlow rate to ");
        Serial.println(InFlowSetPoint);

      } else if (Bin_Status == "DRAIN") {
        OutFlowSetPoint = abs(WLspeed * 1000);  //convert from negative, and cm^3 to Liters;
        Serial.print("Setting OutFlow rate to ");
        Serial.println(OutFlowSetPoint);
      }

      if (CSV_linecount != CSV_rows) {  //Check to see if were at end of file, if so restart from begining
        CSV_linecount++;
        Level_timer = 0;
      } else {
        CSV_linecount = 0;
        Serial.println("CSV Linecount reset to 0");
        Level_timer = 0;
      }
    }

    // This section is for updating the screen
    //Serial.println("Updating Display");
    Display_Count++;
    if (Display_Count <= 3) {  //update display every 2 loops (2 seconds)
      Show_Page = 1;
    } else if ((Display_Count >= 4) && (Display_Count <= 6)) {  //show page 2 for 2 seconds
      Show_Page = 2;
    } else if ((Display_Count >= 7) && (Display_Count <= 9)) {  //show page 3 for 2 seconds
      Show_Page = 3;
    } else if (Display_Count >= 10) {
      Display_Count = 0;  //reset display count @ 10 seconds
    };

    UpdateDisplay();
    checkTimers();
    //esp_task_wdt_reset();  //Reset Watch Dog Timer after loop successfully completes
    // delay(1); //VERY VERY IMPORTANT for Watchdog Reset to apply. At least 1 ms

    Serial.println("");
    Serial.println("-------------------------");
    Serial.println("");
  }  //End Loop_Time Loop

  if ((Calibration_Flag == true) && (SystemPause == true) && (Current_Time >= (Cal_Timer + 1000))) {
    Cal_Timer = Current_Time;
    Liters_per_min_in = (((Input_Pulse_Count / 1.99) * 60) / 1000);  //Recalibrate and change the pulse count
    Liters_per_cycle_in = (Liters_per_cycle_in + (Liters_per_min_in / 60));
    Liters_per_min_out = (((Output_Pulse_Count / 1.92) * 60) / 1000);  //Recalibrate and change the pulse count
    Liters_per_cycle_out = (Liters_per_cycle_out + (Liters_per_min_out / 60));

    Serial.print(Input_Pulse_Count);
    Serial.print(" Pulses IN, Total: ");
    Serial.print(In_Pulse_Total);
    Input_Pulse_Count = 0;

    Serial.print("  ");

    Serial.print(Output_Pulse_Count);
    Serial.print(" Pulses OUT, Total: ");
    Serial.println(Out_Pulse_Total);
    Output_Pulse_Count = 0;

    Serial.print(Liters_per_min_in, 2);
    Serial.print(" Liters per minute in, ");
    Serial.print(Liters_per_min_out, 2);
    Serial.println(" Liters per minute out.   ");

    UpdateDisplay();
    //esp_task_wdt_reset();  //Reset Watch Dog Timer after loop successfully completes
    //delay(1); //VERY VERY IMPORTANT for Watchdog Reset to apply. At least 1 ms
  }

}  //End main loop


/////////////////////This section contains various functions called in setup() and loop()///////////////////////


const char* getFillValvePosition() {
  int open_state_M1 = digitalRead(DetectOpen_M1);
  int close_state_M1 = digitalRead(DetectClose_M1);
  if (open_state_M1 == LOW) {
    return "OPEN";
  } else if (close_state_M1 == LOW) {
    return "CLOSED";
  } else if ((open_state_M1 == HIGH) && (close_state_M1 == HIGH)) {
    return "MIDWAY";
  }
  return "ERROR";
}

const char* getDrainValvePosition() {
  int open_state_M2 = digitalRead(DetectOpen_M2);
  int close_state_M2 = digitalRead(DetectClose_M2);
  if (open_state_M2 == LOW) {
    return "OPEN";
  } else if (close_state_M2 == LOW) {
    return "CLOSED";
  } else if ((open_state_M2 == HIGH) && (close_state_M2 == HIGH)) {
    return "MIDWAY";
  }
  return "ERROR";
}

void connectAWS() {

  //WiFi.disconnect();                 //Stops any previous Wifi sessions
  Serial.print("Starting WiFi... ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  //Initialize Wifi

  WiFi.macAddress(mac);
  snprintf(mac_Id, sizeof(mac_Id), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("MAC Address: ");
  Serial.println(mac_Id);

  Serial.print("Setting up soft AP... ");  //Setup Wifi Access Point and print IP
  WiFi.softAP(SOFT_SSID, SOFT_PASSWORD);
  IP = WiFi.softAPIP();
  Serial.print("Address: ");
  Serial.println(IP);

  display.clear();
  display.drawString(24, 0, "Starting Networks");

  Serial.println("Trying to connect to Wi-Fi: ");
  unsigned long connectAttempt = 1;
  while ((WiFi.status() != WL_CONNECTED) && (connectAttempt <= 10)) {  //Timeout after 10 attempts
    connectAttempt++;
    if (Show_Page == 0) {  //Show that we are trying to connect on splash screen
      display.drawString(0, 16, "Connect to WiFi: ");
      display.display();
    }
    Serial.print("connecting...");
    delay(500);

    //Connect_Counter ++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    display.drawString(0, 16, "Connect to WiFi: Success!");
    display.display();
    rgbLedWrite(LED_PIN, 0, 51, 0);  //Set onboard LED to green @ 20% brightness to indicate Wifi connected
    Serial.print("Wifi Connected!  MAC:");
    WiFi.macAddress(mac);
    snprintf(mac_Id, sizeof(mac_Id), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println(mac_Id);

    WiFi.onEvent(WiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  //If Wifi Discconnected, Reconnect to Wifi/Amazon
    WiFi.onEvent(OnWiFiEvent);

    // Configure WiFiClientSecure to use the AWS IoT device credentials
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);

    // Connect to the MQTT broker on the AWS endpoint we defined earlier
    AWSclient.begin(AWS_IOT_ENDPOINT, 8883, net);

    // Create a message handler
    AWSclient.onMessage(messageHandler);

    display.drawString(0, 32, "Connect to AWS:");
    display.display();
    Serial.print("Connecting to AWS IOT... ");

    while (!AWSclient.connect(THINGNAME)) {
      Serial.print(".");
      delay(100);
    }

    if (!AWSclient.connected()) {
      Serial.println("AWS IoT Timeout!");
      display.drawString(0, 32, "Conenct to AWS: Failed!");
      errorMSG = "Error: Failed to connect to AWS";
      LogError(errorMSG);
      display.display();
      delay(1000);
      return;
    }

    // Subscribe to a topic
    AWSclient.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    rgbLedWrite(LED_PIN, 0, 0, 51);  //Set onboard LED to blue @ 20% brightness to indicate AWS connected
    Serial.println("AWS IoT Connected!");
    display.drawString(0, 32, "Connect to AWS: Success!");
    display.display();
    delay(2000);
  } else {
    display.drawString(0, 16, "Connect to WiFi: Failed!");
    display.drawString(0, 32, "Connect to AWS: Failed!");
    display.display();
    delay(2000);
    Serial.println("Failed to connect!");
    errorMSG = "Error: Failed to connect to WiFi";
    LogError(errorMSG);
    //errorLog = String(data_date + data_time + "," + errorMSG + "\r\n");
    //appendFile(SD, "/ErrorLog.txt", errorLog.c_str());
  }
}

void publishMessage() {  //send all the data via MQTT message
  //StaticJsonDocument<1024> doc;
  JsonDocument doc;
  doc["mac_id"] = mac_Id;
  doc["date"] = data_date;
  doc["time"] = data_time;
  doc["sample_time"] = time(nullptr);
  doc["device_id"] = Device_ID;
  doc["air_temp"] = String(temp_C).substring(0, 5);
  doc["humidity"] = String(humidP).substring(0, 5);
  doc["water_temp"] = String(temperatureC).substring(0, 5);
  doc["inflow"] = String(Liters_per_min_in).substring(0, 5);
  doc["liters_in"] = String(Liters_per_cycle_in).substring(0, 5);
  doc["inflow_set"] = String(InFlowSetPoint).substring(0, 5);
  doc["outflow"] = String(Liters_per_min_out).substring(0, 5);
  doc["liters_out"] = String(Liters_per_cycle_out).substring(0, 5);
  doc["outflow_set"] = String(OutFlowSetPoint).substring(0, 5);
  doc["flow_avg"] = String(Flow_AVG).substring(0, 5);
  doc["empty_offset"] = TOFempty;
  doc["bin_volume"] = Bin_Volume;
  doc["bin_depth"] = Bin_Depth;
  doc["water_mm"] = TOFreadings.getMean();
  doc["bin_status"] = Bin_Status;
  doc["valve1"] = FillValvePosition;
  doc["valve2"] = DrainValvePosition;
  doc["pump"] = Pump_Flag;
  doc["reset"] = Reset_Flag;
  doc["CSV_Line"] = CSV_linecount;
  doc["WiFi"] = WiFi.status();
  doc["AWS"] = AWSclient.connected();

  char jsonBuffer[1024];
  serializeJson(doc, jsonBuffer);  // print to client

  AWSclient.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);

  Serial.print("Publishing data: ");
  Serial.println(jsonBuffer);
}

void messageHandler(String& topic, String& payload) {  //what to do when recieving a message
  //Serial.println("incoming: " + topic + " - " + payload);
  //  StaticJsonDocument<200> doc;
  //  deserializeJson(doc, payload);
  //  const char* message = doc["message"];
}

void printLocalTime(int timeout_wait) {

  if (!getLocalTime(&timeinfo, timeout_wait)) {
    Serial.println("Failed to obtain time");
    return;
  } else {
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");  // print full date / time

    strftime(dateBuff, sizeof(dateBuff), "%b %d %Y", &timeinfo);  //extract just the date (month abbreviated)
    strftime(timeBuff, sizeof(timeBuff), "%H:%M:%S", &timeinfo);  //extract just the time (hour in 24 hour)

    data_date = dateBuff;  //convert to string
    data_time = timeBuff;  //convert to string
    //Serial.println(data_date);
    //Serial.println(data_time);
  }

  /* This is just demo code showing the various components of Date/Time
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");
  */
}

void UpdateDisplay() {  // Temp, Humidity, Water temp, Time, Flow, Motor Status, Wifi/AWS connected
  Serial.print("Showing Page: ");
  Serial.print(Show_Page);
  Serial.print(" ");
  display.clear();
  if (Show_Page == 1) {

    if (!getLocalTime(&timeinfo, 5)) {  // The 5 is a delay to check for the time, default is 5000
      display.drawString(0, 0, "Time: No NTP");
    } else {
      display.drawString(0, 0, data_date);
      display.drawString(0, 16, data_time);
    }

    if (WiFi.status() == WL_CONNECTED) {
      display.drawXbm(70, 0, 12, 12, wifi_logo);

      if (AWSclient.connected()) {
        display.drawString(90, 0, "AWS: Y");
      } else if (!AWSclient.connected()) {
        display.drawString(90, 0, "AWS: N");
      }

      else if (WiFi.status() != WL_CONNECTED) {
        display.drawString(0, 6, "WiFi: No");
        display.drawString(90, 0, "AWS: N");
      }
    }

    display.drawString(55, 16, "Device ID:");
    display.drawString(105, 16, String(Device_ID));

    display.drawString(0, 32, "Mac:");
    display.drawString(26, 32, mac_Id);

    display.drawString(0, 48, "Bin State:");
    display.drawString(50, 48, Bin_Status);
    display.drawString(80, 48, String(Bin_Volume).substring(0, 4));

    Serial.println("Display updated!");
  }

  else if (Show_Page == 2) {

    if (!getLocalTime(&timeinfo, 5)) {
      display.drawString(0, 0, "Time: No NTP");
    } else {
      display.drawString(0, 0, data_date);
      display.drawString(0, 16, data_time);
    }

    if (WiFi.status() == WL_CONNECTED) {
      display.drawXbm(70, 0, 12, 12, wifi_logo);

      if (AWSclient.connected()) {
        display.drawString(90, 0, "AWS: Y");
      } else if (!AWSclient.connected()) {
        display.drawString(90, 0, "AWS: N");
      }

      else if (WiFi.status() != WL_CONNECTED) {
        //display.drawString(0,6, "WiFi: No");
        display.drawString(108, 0, "AWS: N");
      }
    }
    display.drawString(0, 28, "Temp:");
    display.drawString(30, 28, String(temp_C).substring(0, 4));  //convert air temp to string of 4 chars
    display.drawString(52, 28, "C");

    display.drawString(64, 28, "Humi:");
    display.drawString(92, 28, String(humidP).substring(0, 4));  //convert humidity to string of 3 chars
    display.drawString(112, 28, "%");

    display.drawString(0, 40, "Water:");
    display.drawString(32, 40, String(temperatureC).substring(0, 4));  //convert water temp to string of 4 chars
    display.drawString(56, 40, "C");
    display.drawString(66, 40, "Pump:");

    if (Pump_Flag == true) {
      display.drawString(96, 40, "ON");
    } else {
      display.drawString(96, 40, "OFF");
    }

    display.drawString(32, 52, String(TOFreadings.getMean()));  //Show water level in mm
    display.drawString(60, 52, "mm");

    //display.drawString(70, 48, String(Liters_per_min).substring(0, 3));  //convert LPM to string of 3 chars
    //display.drawString(90, 48, "L/min");

    //display.drawString(55, 16, "Valve:");
    //display.drawString(85, 16, FillValvePosition);

    Serial.println("Display updated!");
    Show_Page = 1;
  }

  else if (Show_Page == 3) {

    display.drawString(0, 0, "Inflow:");
    display.drawString(0, 16, FillValvePosition);
    display.drawString(0, 28, String(Liters_per_min_in).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(20, 28, "L/min");
    display.drawString(0, 40, String(Liters_per_cycle_in).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(20, 40, "L/cyc");
    display.drawString(0, 52, String(InFlowSetPoint).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(20, 52, "L/min");

    display.drawString(62, 0, "Outflow :");
    display.drawString(62, 16, DrainValvePosition);
    display.drawString(62, 28, String(Liters_per_min_out).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(82, 28, "L/min");
    display.drawString(62, 40, String(Liters_per_cycle_out).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(82, 40, "L/cyc");
    display.drawString(62, 52, String(OutFlowSetPoint).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(82, 52, "L/min");

    Serial.println("Display updated!");
  }

  else if (Show_Page == 4) {  //this page is not currently displayed at all
    display.drawString(0, 0, "Inflow:");
    display.drawString(0, 16, String(In_Pulse_Total).substring(0, 4));  //Show total # of pulses for Calibration
    display.drawString(20, 16, "Pulses");
    display.drawString(0, 28, String(Liters_per_min_in).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(20, 28, "L/min");
    display.drawString(0, 40, String(Liters_per_cycle_in).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(20, 40, "L/cyc");
    display.drawString(0, 52, String(InFlowSetPoint).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(20, 52, "L/min");

    display.drawString(62, 0, "Outflow :");
    display.drawString(62, 16, String(Out_Pulse_Total).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(82, 16, "Pulses");
    display.drawString(62, 28, String(Liters_per_min_out).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(82, 28, "L/min");
    display.drawString(62, 40, String(Liters_per_cycle_out).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(82, 40, "L/cyc");
    display.drawString(62, 52, String(OutFlowSetPoint).substring(0, 4));  //convert LPM to string of 3 chars
    display.drawString(82, 52, "L/min");

    Serial.println("Display updated!");
  } else if (Show_Page == 5) {  //this page is for system pause
    display.setFont(ArialMT_Plain_24);
    display.drawString(24, 12, "System");
    display.drawString(24, 36, "Paused");
    display.setFont(ArialMT_Plain_10);
    Serial.println("Display updated!");

  }
  display.display();   //draw the page
}

void WiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("WiFi lost connection.  Trying to Reconnect");
  errorMSG = "Error: Lost connection to WiFi";
  LogError(errorMSG);
  rgbLedWrite(LED_PIN, 51, 0, 0);  //Set onboard LED to red @ 20% brightness to indicate AWS connected
  connectAWS();                    //Connect to Wifi & Amazon database
}

void checkTimers() {
  if (M1_Delay.justFinished())  //Check for motor delay every loop, turn motor off after delay
  {
    M1_Milis = (millis() - M1_Milis);
    digitalWrite(M1_Open, LOW);  //Set both of motor 1 pins low, turns motor off
    digitalWrite(M1_Close, LOW);
    Serial.print("M1 timer off after ");
    Serial.println(M1_Milis);
  }

  if (M2_Delay.justFinished())  //Check for motor delay every loop, turn motor off after delay
  {
    M2_Milis = (millis() - M2_Milis);
    digitalWrite(M2_Open, LOW);  //Set both of motor 2 pins low, turns motor off
    digitalWrite(M2_Close, LOW);
    Serial.print("M2 timer off after ");
    Serial.println(M2_Milis);
  }
}

void OnWiFiEvent(WiFiEvent_t event) {
  switch (event) {

    case WIFI_EVENT_STA_CONNECTED:
      Serial.println("ESP32 Connected to WiFi Network");
      break;
    case WIFI_EVENT_AP_START:
      Serial.println("ESP32 soft AP started");
      break;
    case WIFI_EVENT_AP_STACONNECTED:
      Serial.println("Station connected to ESP32 soft AP");
      break;
    case WIFI_EVENT_AP_STADISCONNECTED:
      Serial.println("Station disconnected from ESP32 soft AP");
      break;
    default: break;
  }
}

String processor(const String& var) {  // Replaces website %placeholder% with stored values

  if (var == "SYSTEMSTATUS") {  //show system state

    String systemHTML = data_date;
    systemHTML += " ";
    systemHTML += data_time;
    systemHTML += "<br>WL Data: <font color=\"blue\">";
    systemHTML += WL_Data;
    systemHTML += "<font color=\"black\"><br><br>";
    systemHTML += "<table style=\"margin-left:auto; margin-right:auto;\">";
    systemHTML += "<tr><td>";
    systemHTML += "SD Card: ";
    systemHTML += SD_Status;
    systemHTML += "</td><td>";
    systemHTML += "SPIFFS: ";
    systemHTML += SPIFFS_Status;
    systemHTML += "</td></tr><tr><td>";
    systemHTML += "PSRAM: ";
    systemHTML += PSRAM_Status;
    systemHTML += "</td><td>";
    //systemHTML += "WL Data:";    //place holder cell
    //systemHTML += WL_Data;
    systemHTML += "</td></tr><tr><td>";

    systemHTML += "WiFi: ";
    systemHTML += (wl_status_to_string(WiFi.status()));
    systemHTML += "</td><td>";

    systemHTML += "          ";
    if (AWSclient.connected()) {
      systemHTML += "AWS: <font color=\"green\"><b>CONNECTED</b><font color=\"black\">";
    } else {
      systemHTML += "AWS: <font color=\"red\"><b>FAILED</b><font color=\"black\">";
    }

    systemHTML += "</td></tr></table>";

    Serial.print("System Status:");
    Serial.println(systemHTML);

    return systemHTML;
  }

  else if (var == "FLOW") {  //show flow parameters
    String flowHTML = "<tr><td>";
    flowHTML += "<b>Water In: </b>";
    flowHTML += "</td><td>";
    flowHTML += "<b>Water Out: </b>";
    flowHTML += "</td></tr><tr><td> ";
    flowHTML += "Flow: ";
    flowHTML += Liters_per_min_in;
    flowHTML += " L/min</td><td>";
    flowHTML += "Flow: ";
    flowHTML += Liters_per_min_out;
    flowHTML += " L/min</td></tr><tr><td>";
    flowHTML += "Set: ";
    flowHTML += InFlowSetPoint;
    flowHTML += " L/min</td><td>";
    flowHTML += "Set: ";
    flowHTML += OutFlowSetPoint;
    flowHTML += " L/min</td></tr><tr><td>";
    flowHTML += "Cycle: ";
    flowHTML += Liters_per_cycle_in;
    flowHTML += " L</td><td>";
    flowHTML += "Cycle: ";
    flowHTML += Liters_per_cycle_out;
    flowHTML += " L</td></tr>";

    return flowHTML;
  }

  /*
  flowHTML += "Average Flow (10 sec): ";
  flowHTML += Flow_AVG;
  flowHTML += " L/min<br>";

  Serial.print ("Flow Parameters:");
  Serial.println(flowHTML);
  return flowHTML;*/

  else if (var == "BINSTATUS") {  //show grow bin status

    String binHTML = "<tr><td>";
    binHTML += "Bin Status: ";
    binHTML += Bin_Status;
    binHTML += "</td><td>";
    binHTML += "Bin Volume: ";
    binHTML += Bin_Volume;
    binHTML += " L</td></tr><tr><td>";
    binHTML += "Valve 1: ";
    binHTML += FillValvePosition;
    binHTML += "</td><td>";
    binHTML += "Valve 2: ";
    binHTML += DrainValvePosition;
    binHTML += "</td></tr><tr><td>";
    binHTML += "CSV Line: ";
    binHTML += CSV_linecount;
    binHTML += "</td><td>";
    binHTML += "WL Set: ";
    binHTML += futureWL;
    binHTML += " L</td></tr><tr><td>";
    binHTML += "Bin Depth: ";
    binHTML += Bin_Depth;
    binHTML += "mm</td><td>";
    binHTML += "Pump: ";
    binHTML += Pump_Flag;
    binHTML += "</td></tr>";

    //Serial.print("Bin Status:");
    //Serial.println(binHTML);
    return binHTML;
  }

  else if (var == "SENSORS") {
    String sensorHTML = "<tr><td>";
    sensorHTML += "Air Temp: ";
    sensorHTML += temp_C;
    sensorHTML += " C</td><td>";
    sensorHTML += "Water Temp: ";
    sensorHTML += temperatureC;
    sensorHTML += " C</td></tr><tr><td>";
    sensorHTML += "Humidity: ";
    sensorHTML += humidP;
    sensorHTML += "%</td><td>";
    sensorHTML += "TOF: ";
    sensorHTML += TOFreadings.getMean();
    sensorHTML += "mm</td></tr>";

    //Serial.print("Sensors:");
    //Serial.println(sensorHTML);
    return sensorHTML;
  }

  else if (var == "DEMO") {  //show demo mode start/stop parameters
    if (Demo_Mode == true) {
      return "Stop";
    } else if (Demo_Mode == false) {
      return "Start";
    }
  }

  //Variables for admin page
  else if (var == "DATAFILE") {
    return WL_Data;
  } else if (var == "SDSTATUS") {
    return String(SD_Status);
  } else if (var == "SPIFFSTATUS") {
    return String(SPIFFS_Status);
  } else if (var == "PSRAMSTAT") {
    return String(PSRAM_Status);
  }

  else if (var == "aSTATUS") {
    return Bin_Status;
  } else if (var == "aVALVE1") {
    return FillValvePosition;
  } else if (var == "aVALVE2") {
    return DrainValvePosition;
  } else if (var == "BINVOL") {
    return String(Bin_Volume);
  } else if (var == "FUTUREWL") {
    return String(futureWL);
  } else if (var == "PUMP") {
    if (Pump_Flag == false) {
      return "Turn OFF";
    } else {
      return "Turn ON";
    }
  } else if (var == "WIFI") {
    return wl_status_to_string(WiFi.status());
  } else if (var == "FLOWIN") {
    return String(Liters_per_min_in);
  } else if (var == "FLOWOUT") {
    return String(Liters_per_min_out);
  } else if (var == "FLOWINSET") {
    return String(InFlowSetPoint);
  } else if (var == "FLOWOUTSET") {
    return String(OutFlowSetPoint);
  } else if (var == "CYCLEIN") {
    return String(Liters_per_cycle_in);
  } else if (var == "CYCLEOUT") {
    return String(Liters_per_cycle_out);
  } else if (var == "TOTINPULSE") {
    return String(In_Pulse_Total);
  } else if (var == "TOTOUTPULSE") {
    return String(Out_Pulse_Total);
  } else if (var == "CALIN") {
    return String(InCalFactor);
  } else if (var == "CALOUT") {
    return String(OutCalFactor);
  } else if (var == "UPDETECT") {
    return String(digitalRead(High_Liquid_Detection_Pin));
  } else if (var == "LOWDETECT") {
    return String(digitalRead(Low_Liquid_Detection_Pin));
  } else if (var == "FLOWAVG") {
    return String(Flow_AVG);
  } else if (var == "CSVLINE") {
    return String(CSV_linecount);
  } else if (var == "BINDEPTH") {
    return String(Bin_Depth);
  } else if (var == "TOFMM") {
    return String(TOFreadings.getMean());
  } else if (var == "EMPTYOFF") {
    return String(TOFempty);
  }

  else if (var == "SENSORLIST") {
    String sensorHTML = "<tr><td>";
    sensorHTML += "<a href=\"/AIRTEMPRES\">Air Temp: ";
    sensorHTML += temp_C;
    sensorHTML += " C</a></td><td>";
    sensorHTML += "<a href=\"/WATERTEMPRES\">Water Temp: ";
    sensorHTML += temperatureC;
    sensorHTML += " C</a></td></tr><tr><td>";
    sensorHTML += "Humidity: ";
    sensorHTML += humidP;
    sensorHTML += "%%</td><td>";
    sensorHTML += "<a href=\"/TOFRES\">TOF: ";
    sensorHTML += TOFreadings.getMean();
    sensorHTML += "mm</td>";
    sensorHTML += "</tr></table>";

    //Serial.print("Sensors:");
    //Serial.println(sensorHTML);
    return sensorHTML;
  }

  else if (var == "DEMOMODE") {
    if (Demo_Mode == true) {
      return ("Demo Mode (OFF)");
    } else {
      return ("Demo Mode (ON)");
    }
  }

  else if (var == "AWSstatus") {
    if (AWSclient.connected()) {
      return ("<font color=\"green\"><b>CONNECTED</b><font color=\"black\">");
    } else {
      return ("<font color=\"red\"><b>FAILED</b><font color=\"black\">");
    }
  }

  else if (var == "PAUSE") {
    if (SystemPause == true) {
      return ("<font color=\"red\"><b>PAUSED</b><font color=\"black\">");
    } else {
      return ("<font color=\"green\"><b>RUNNING</b><font color=\"black\">");
    }
  }

  else if (var == "RAMSTATUS") {
    String RAMHTML = "<tr style =\"height:35px\"><td>Ram: ";
    RAMHTML += ESP.getHeapSize();
    RAMHTML += "</td><td>Max: ";
    RAMHTML += ESP.getMaxAllocHeap();
    RAMHTML += "</td></tr>";
    RAMHTML += "<tr style =\"height:35px\"><td>Free: ";
    RAMHTML += ESP.getFreeHeap();
    RAMHTML += "</td><td>Min: ";
    RAMHTML += ESP.getMinFreeHeap();
    RAMHTML += "</td></tr>";
    return RAMHTML;
  }

  else if (var == "FILELIST") {  //list the files in the /data folder of the SD card
    String listText = "";
    File root = SD.open("/data/", "r");
    if (!root || !root.isDirectory()) {  //check if SD has data folder, if not make one
      if (SD.mkdir("/data/") != 1) {
        Serial.println("Failed to open/make directory");
        errorMSG = "Error:  Failed to open/make directory";
        LogError(errorMSG);
        //errorLog = String(data_date + data_time + "," + errorMSG + "\r\n");
        //appendFile(SPIFFS, "/ErrorLog.txt", errorLog.c_str());
        listText += "Failed to open directory";
        SD_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
        return listText;
      }
      //else {
      //  return "";
      //}
    }

    listText += "<h2><u>Files on SD Card</u></h2><h3>";

    File entry = root.openNextFile();
    while (entry) {  //List file names as links to the file download
      listText += "<a href=\"/download?file=/data/" + String(entry.name()) + "\">" + String(entry.name()) + "</a>";

      if (Admin_Flag == true) {  //if admin page, show the delete button
        listText += " (" + humanReadableSize(entry.size()) + ") ";
        listText += "<button onclick=\"window.location.href=\'/delete?file=/data/" + String(entry.name()) + "\'\">Delete</button>";
        //listText += "<button onclick=\"window.location.href=\'/delete?file=/data/" + String(entry.name()) + "\'\;\">Delete</button>";
      }
      listText += "<br>";
      entry = root.openNextFile();
    }

    listText += "</h3>";
    entry.close();
    root.close();
    return listText;
  }

  else if (var == "WLDATA") {  //list the files in the /data folder of the SD card
    String listText = "";
    listText += "<center>Water Level Data File:</center><br>";

    File root = SD.open("/WL Data/", "r");
    if (!root || !root.isDirectory()) {  //check if SD has data folder, if not make one
      if (SD.mkdir("/WL Data/") != 1) {
        Serial.println("Failed to open/make directory");
        errorMSG = "Error:  Failed to open/make directory";
        LogError(errorMSG);
        //errorLog = String(data_date + data_time + "," + errorMSG + "\r\n");
        //appendFile(SPIFFS, "/ErrorLog.txt", errorLog.c_str());
        listText += "Failed to open directory";
        SD_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
        return listText;
      }
    }

    listText += "<select name=\"WLdataselect\" id=\"WLdataselect\">";

    File entry = root.openNextFile();
    while (entry) {  //List file names as links to the file download
      listText += "<option value=\"" + String(entry.name()) + "\">" + String(entry.name()) + "</option>";
      //listText += "<option value=\"" + String(entry.name()) + "(" + humanReadableSize(entry.size()) + ")\">" + String(entry.name()) + "</option>";
      entry = root.openNextFile();
    }

    listText += "</select>";
    entry.close();
    root.close();

    listText += "<button type=\"submit\" name=\"Action\" value=\"load\">Load Data</button>";
    listText += "<button type=\"submit\" name=\"Action\" value=\"delete\">Delete Data</button></form>";
    listText += "<br><form method=\"POST\" action=\"/upload\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"data\"><input type=\"submit\" name=\"upload\" value=\"upload\" title=\"Upload File\"></form>";  //Add the upload buttons
    return listText;
  }

  else if (var == "ERRORLIST") {  //Show the ErrorLog.txt from SPIFFS (onboard flash storage)
                                  ///*************************************///
    return "";   ///remove this later and fix Error List.///

    CSV_Parser errorCSV(/*format*/ "ss", /*has_header*/ true, /*delimiter*/ ',');  //set parameters for loading the error log csv

    if (errorCSV.readSPIFFSfile("/ErrorLog.txt")) {  //load in the Error Log file if it exists
      Serial.print("Error log loaded successfully: ");
      Serial.println(errorCSV.getRowsCount());

      if (errorCSV.getRowsCount() >= 2) {

        char** err_Date = (char**)errorCSV["Date"];
        char** err_Text = (char**)errorCSV["Error"];

        String errList = "<h4><u>Error Log (";
        errList += errorCSV.getRowsCount();
        errList += "):</u></h4><h5>";
        for (int row = 0; row < errorCSV.getRowsCount(); row++) {
          errList += err_Date[row];
          errList += "   ";
          errList += err_Text[row];
          errList += "<br>";
        }

        errList += "</h5>";
        return errList;
      }
    }
  }

  return String("");  //may be redundant code
}

void notFound(AsyncWebServerRequest* request) {  //what to do when a unrecognized request is recieved by the client
  request->send(404, "text/plain", "Not found");
}

void handleUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  Serial.println(logmessage);

  if (!index) {
    logmessage = "Upload Start: " + String(filename);
    // open the file on first call and store the file handle in the request object
    request->_tempFile = SD.open("/WL Data/" + filename, "w");
    Serial.println(logmessage);
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    Serial.println(logmessage);
  }

  if (final) {
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    // close the file handle as the upload is now done
    request->_tempFile.close();
    Serial.println(logmessage);
    request->redirect("../admin");
  }
}

const char* wl_status_to_string(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return "<font color=\"red\"><b>NO_WiFi</b><font color=\"black\">";
    case WL_IDLE_STATUS: return "<font color=\"red\"><b>IDLE_STATUS</b><font color=\"black\">";
    case WL_NO_SSID_AVAIL: return "<font color=\"red\"><b>NO_SSID</b><font color=\"black\">";
    case WL_SCAN_COMPLETED: return "<font color=\"yellow\"><b>SCAN_DONE</b><font color=\"black\">";
    case WL_CONNECTED: return "<font color=\"green\"><b>CONNECTED</b><font color=\"black\">";
    case WL_CONNECT_FAILED: return "<font color=\"red\"><b>FAILED</b><font color=\"black\">";
    case WL_CONNECTION_LOST: return "<font color=\"yellow\"><b>LOST</b><font color=\"black\">";
    case WL_DISCONNECTED: return "<font color=\"yellow\"><b>DISCONNECT</b><font color=\"black\">";
    default: return "NO Wifi Status";
  }
}

String humanReadableSize(const size_t bytes) {  //show file sizes in a human readable form
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
  else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

void LoadWLData() {  //Load water level data into an array

  if (WL_Data == "") { WL_Data = ("GH02_2023_WL2.csv"); }                             //remove this line, or add what to "default to"
  CSV_Parser waterLevelCSV(/*format*/ "sf", /*has_header*/ true, /*delimiter*/ ',');  //for loading the water level csv
  String WL_CSV = ("/WL Data/" + WL_Data);
  Serial.print("Loading Water level file: ");
  Serial.println(WL_CSV);
  display.clear();
  display.drawString(24, 0, "Water Level Data");
  display.drawString(0, 16, "File:");
  display.drawString(20, 16, String(WL_Data));
  display.display();
  delay(100);

  if (waterLevelCSV.readSDfile(SD, WL_CSV.c_str()) && WL_Data != "") {  //load in the water level data file if it exists
    Serial.println(" ...loaded");
    //column_Date = (char**)waterLevelCSV[0];
    //column_Level = (float*)waterLevelCSV[1];

    //char* column_Date_ps = (char *)ps_malloc(n_elements * sizeof(char));
    //column_Date_ps =(char**)waterLevelCSV[0];

    if (psramFound()) {                                                                                             //copies to wl data to PSRAM
      memcpy(column_Date, (char**)waterLevelCSV["Date"], sizeof(waterLevelCSV[0]) * waterLevelCSV.getRowsCount());  //memcpy(destination,source,length)
      memcpy(column_Level, (float*)waterLevelCSV["Level"], sizeof(waterLevelCSV[1]) * waterLevelCSV.getRowsCount());
      Serial.println("WL data copied to PSRAM");
    }
    //column_Level_ps = (float*)waterLevelCSV[1];
    //column_Date = (char**)waterLevelCSV["Date"];
    //column_Level = (float*)waterLevelCSV["Level"];

    if (column_Date && column_Level) {
      currentWL = (column_Level[CSV_linecount] / 2);
      futureWL = (column_Level[CSV_linecount + 1] / 2);
      WLdif = (futureWL - currentWL);
      WLvolume = (WLdif * .55125);  //.55125 is the area of the bin (square m LxW)
      WLspeed = (WLvolume / 15);    //15 is the # of minutes between data entries

      if (Bin_Status == "FILL") {
        InFlowSetPoint = WLspeed * 1000;  //convert from cm^3 to Liters
        OutFlowSetPoint = 0;
        Serial.print("Setting InFlow rate to ");
        Serial.print(InFlowSetPoint);
        Serial.println(" L/min");
      } else if (Bin_Status == "DRAIN") {
        OutFlowSetPoint = abs(WLspeed * 1000);  //convert from negative, and cm^3 to Liters
        InFlowSetPoint = 0;
        Serial.print("Setting OutFlow rate to ");
        Serial.print(OutFlowSetPoint);
        Serial.println(" L/min");
      }

      CSV_rows = waterLevelCSV.getRowsCount();  //the # of rows in the data sheet, used for EOF
      Serial.print(CSV_rows);
      Serial.println(" rows of water data loaded");
      display.drawString(0, 32, String(CSV_rows));
      display.drawString(24, 32, "rows of data");
      display.drawString(0, 48, "Starting at: ");
      display.drawString(54, 48, String(CSV_linecount));
      display.display();

      //Serial.printf("PSRAM available memory:     %lu bytes\n", ESP.getFreePsram());

      delay(2000);

      /*    //For testing that the data file was loaded correctly

      for (int row = 0; row < waterLevelCSV.getRowsCount(); row++) {
        //This code does not know what to do at the end of the file
        float currentWL = (column_Level_ps[row] / 2);
        futureWL = (column_Level_ps[row + 1] / 2);
        float WLdif = (futureWL - currentWL);
        float WLvolume = (WLdif * .55125);  //.55125 is the area of the bin (square m LxW)
        float WLspeed = (WLvolume / 15);    //15 is the # of minutes between data entries

        Serial.print("current WL = ");
        Serial.print(currentWL);
        Serial.print(", future WL = ");
        Serial.print(futureWL);
        Serial.print(", WL difference = ");
        Serial.print(WLdif);
        Serial.print(", WL volume = ");
        Serial.print(WLvolume);
        Serial.print(", WL speed = ");
        Serial.print(WLspeed);
        Serial.print(" cm^3/min");
        Serial.print(" or ");
        Serial.print(WLspeed * 1000);
        Serial.println(" L/min");   
      } */

    } else {
      Serial.println("ERROR: At least 1 of the columns was not found, something went wrong.");
      errorMSG = "ERROR: At least 1 of the columns was not found, something went wrong.";
      LogError(errorMSG);
      display.drawString(0, 32, "No Water Level data!");
      display.display();
      delay(2000);
    }

    // output parsed values (allows to check that the file was parsed correctly)
    //waterLevelCSV.print(); // assumes that "Serial.begin()" was called before (otherwise it won't work)
  } else {
    Serial.print("ERROR: File named ");
    Serial.print(WL_Data);
    Serial.println(" does not exist...");
    errorMSG = "Error:  File named " + WL_Data + "does not exist...";
    LogError(errorMSG);
    display.drawString(0, 32, "No data file!");
    display.display();
    delay(2000);
  }
}

void writeFile(fs::FS& fs, const char* path, const char* message) {  // Write to the SD card (DON'T MODIFY THIS FUNCTION)

  Serial.printf("Writing file: %s\n", path);
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    errorMSG = "Error:  Failed to open ";
    errorMSG += path;
    errorMSG += " for writing";
    LogError(errorMSG);
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
    errorMSG = "Error:  Failed to write to ";
    errorMSG += path;
    LogError(errorMSG);
  }
  file.close();
}

// Append data to the SD card (DON'T MODIFY THIS FUNCTION)
void appendFile(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    errorMSG = "Error:  Failed to open ";
    errorMSG += path;
    errorMSG += " for appending";
    LogError(errorMSG);
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
    errorMSG = "Error:  Failed to append to ";
    errorMSG += path;
    LogError(errorMSG);
  }
  file.close();
}

const char* reset_reason(int reason) {
  switch (reason) {
    case 1: return ("POWERON_RESET"); break;           /**<1,  Vbat power on reset*/
    case 3: return ("SW_RESET"); break;                /**<3,  Software reset digital core*/
    case 4: return ("OWDT_RESET"); break;              /**<4,  Legacy watch dog reset digital core*/
    case 5: return ("DEEPSLEEP_RESET"); break;         /**<5,  Deep Sleep reset digital core*/
    case 6: return ("SDIO_RESET"); break;              /**<6,  Reset by SLC module, reset digital core*/
    case 7: return ("TG0WDT_SYS_RESET"); break;        /**<7,  Timer Group0 Watch dog reset digital core*/
    case 8: return ("TG1WDT_SYS_RESET"); break;        /**<8,  Timer Group1 Watch dog reset digital core*/
    case 9: return ("RTCWDT_SYS_RESET"); break;        /**<9,  RTC Watch dog Reset digital core*/
    case 10: return ("INTRUSION_RESET"); break;        /**<10, Instrusion tested to reset CPU*/
    case 11: return ("TGWDT_CPU_RESET"); break;        /**<11, Time Group reset CPU*/
    case 12: return ("SW_CPU_RESET"); break;           /**<12, Software reset CPU*/
    case 13: return ("RTCWDT_CPU_RESET"); break;       /**<13, RTC Watch dog Reset CPU*/
    case 14: return ("EXT_CPU_RESET"); break;          /**<14, for APP CPU, reseted by PRO CPU*/
    case 15: return ("RTCWDT_BROWN_OUT_RESET"); break; /**<15, Reset when the vdd voltage is not stable*/
    case 16: return ("RTCWDT_RTC_RESET"); break;       /**<16, RTC Watch dog reset digital core and rtc module*/
    default: return ("NO_MEAN");
  }
}

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_UNKNOWN: return "Unknown";
    case ESP_RST_POWERON: return "PowerOn";    //Power on or RST pin toggled
    case ESP_RST_EXT: return "ExtPin";         //External pin - not applicable for ESP32
    case ESP_RST_SW: return "Reboot";          //esp_restart()
    case ESP_RST_PANIC: return "Crash";        //Exception/panic
    case ESP_RST_INT_WDT: return "WDT_Int";    //Interrupt watchdog (software or hardware)
    case ESP_RST_TASK_WDT: return "WDT_Task";  //Task watchdog
    case ESP_RST_WDT: return "WDT_Other";      //Other watchdog
    case ESP_RST_DEEPSLEEP: return "Sleep";    //Reset after exiting deep sleep mode
    case ESP_RST_BROWNOUT: return "BrownOut";  //Brownout reset (software or hardware)
    case ESP_RST_SDIO: return "SDIO";          //Reset over SDIO
    default: return "";
  }
}

void checkWebClient() {
  int myclient = WiFi.softAPgetStationNum();

  if (myclient == 0 && WebServerOn) {
    Serial.println("No clients connected, stopping webserver");
    AsyncServer.end();
    WebServerOn = false;
  }

  else if (myclient >= 1 && !WebServerOn) {
    Serial.print(myclient);
    Serial.println(" client(s) connected");
    StartWebServer();
    WebServerOn = true;
  }
}

void StartWebServer() {
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  /// This section is how the webserver will handle requests when the client interacts with the webpage //
  ////////////////////////////////////////////////////////////////////////////////////////////////////////

  Serial.println("<---   Starting Webserver   --->");

  AsyncServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {  //Load the main page
    request->send(200, "text/html", main_html, processor);
    //request->send_P(200, "text/html", main_html, processor);

    Admin_Flag = false;  //admin options are not shown
  });

  AsyncServer.on("/admin", HTTP_GET, [](AsyncWebServerRequest* request) {  //Load the admin page
    request->send(200, "text/html", admin_html, processor);
    //request->send_P(200, "text/html", admin_html, processor);
    Serial.println("<----- Admin page loaded ----->");
    Admin_Flag = true;  //admin options are  shown
  });

  AsyncServer.on("/download", HTTP_GET, [](AsyncWebServerRequest* request) {  //send the datafile to client for download
    Serial.print("Url /download executed... ");
    if (request->hasParam("file")) {
      const char* dlfileName = request->getParam("file")->value().c_str();
      Serial.println(dlfileName);

      AsyncWebServerResponse* response = request->beginResponse(SD, dlfileName, "text/plain", true);  //eend the file to client
      request->send(response);
    }
  });

  AsyncServer.on("/resetESP", HTTP_GET, [](AsyncWebServerRequest* request) {  //restart ESP32
    request->redirect("../admin");
    delay(500);
    ESP.restart();
  });

  AsyncServer.on("/SysPause", HTTP_GET, [](AsyncWebServerRequest* request) {  //Pause the TIDES system
    if (SystemPause == true) {
      Serial.println("System Unpaused!");
      SystemPause = false;
      Calibration_Flag = false;
    } else if (SystemPause == false) {
      Serial.println("System Paused!");
      SystemPause = true;
      Show_Page = 5;  //Show the pause screen
      UpdateDisplay();
    }

    request->redirect("../admin");
  });

  AsyncServer.on("/Calibrate", HTTP_GET, [](AsyncWebServerRequest* request) {  //restart ESP32
    if (SystemPause == false) {
      Serial.println("Please pause system before calibrating");
    } else {
      Serial.println("Calibration Mode: Remember to open valves!");
      Calibration_Flag = true;
      Show_Page = 4;
      In_Pulse_Total = 0;
      Out_Pulse_Total = 0;
    }

    request->redirect("../admin");
  });

  AsyncServer.on(
    "/upload", HTTP_POST, [](AsyncWebServerRequest* request) {  //recieve WLdata upload
      Serial.println("upload request");
      request->send(200);
    },
    handleUpload);

  AsyncServer.on("/valve1", HTTP_GET, [](AsyncWebServerRequest* request) {  //Open/close valve 1
    FillValvePosition = getFillValvePosition();
    if (FillValvePosition == "MIDWAY" || FillValvePosition == "CLOSED") {
      Serial.println("Admin: Opening Valve 1 for 10000ms");
      digitalWrite(M1_Open, HIGH);
      digitalWrite(M1_Close, LOW);
      M1_Delay.start(10000);
      M1_Milis = millis();
    } else if (FillValvePosition == "OPEN") {
      Serial.println("Admin: Closing Valve 1 for 10000ms");
      digitalWrite(M1_Open, LOW);
      digitalWrite(M1_Close, HIGH);
      M1_Delay.start(10000);
      M1_Milis = millis();
    }
    request->redirect("../admin");
  });

  AsyncServer.on("/valve2", HTTP_GET, [](AsyncWebServerRequest* request) {  //Open/close valve2
    DrainValvePosition = getDrainValvePosition();

    if (DrainValvePosition == "MIDWAY" || DrainValvePosition == "CLOSED") {
      Serial.println("Admin: Opening Valve 2 for 10000ms");
      digitalWrite(M2_Open, HIGH);
      digitalWrite(M2_Close, LOW);
      M2_Delay.start(10000);
      M2_Milis = millis();
    } else if (DrainValvePosition == "OPEN") {
      Serial.println("Admin: Closing Valve 2 for 10000ms");
      digitalWrite(M2_Open, LOW);
      digitalWrite(M2_Close, HIGH);
      M2_Delay.start(10000);
      M2_Milis = millis();
    }
    request->redirect("../admin");
  });

  AsyncServer.on("/demo", HTTP_GET, [](AsyncWebServerRequest* request) {  //Switch to demo mode
    if (Demo_Mode == false) {
      Serial.println("Admin: Demo Mode Activated");
      Bin_Status = "FILL";
      Liters_per_cycle_in = 0;
      Liters_per_cycle_out = 0;
      InFlowSetPoint = 1.0;
      OutFlowSetPoint = 0.5;
      Demo_Mode = true;
    } else if (Demo_Mode == true) {
      Serial.println("Admin: Demo Mode Stopped");
      Demo_Mode = false;
    }
    request->redirect("../admin");
  });

  AsyncServer.on("/SDrestart", HTTP_GET, [](AsyncWebServerRequest* request) {  //Reinitialize SD card
    Serial.println("Admin: Reinitilizing SD card");
    SPI.end();
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SD_Status = ("<font color=\"green\"><b>GOOD<b><font color=\"black\">");  //set the flag that the SD card mounted successfully
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("No SD card attached");
      display.clear();
      display.drawString(12, 0, "No SD card attached");
      display.display();
      delay(2000);
      SD_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
    }

    if (!SD.begin(SD_CS)) {
      Serial.println("ERROR - SD card initialization failed!");
      display.clear();
      display.drawString(12, 0, "SD initialization failed!");
      display.display();
      delay(2000);
      SD_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
    }

    request->redirect("../admin");
  });

  AsyncServer.on("/SPIFFSrestart", HTTP_GET, [](AsyncWebServerRequest* request) {  //Reinitialize SPIFFS
    Serial.println("Admin: Reinitilizing SPIFFS");
    if (!SPIFFS.begin(true)) {  //Mount SPIFFS, the onboard flash storage (used for Error Log)
      Serial.println("An Error has occurred mounting SPIFFS");
      display.drawString(12, 16, "Failed to mount SPIFFS");
      display.display();
      display.clear();
      SPIFFS_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
      delay(2000);
    } else {
      display.drawString(12, 16, "SPIFFS Mount: Success!");
      display.display();
      Serial.println("SPIFFS Mount: Success!");
      //checks if the file exists, prints 0 for no or 1 for yes
      SPIFFS_Status = ("<font color=\"green\"><b>GOOD</b><font color=\"black\">");
      delay(200);
    }
    request->redirect("../admin");
  });

  AsyncServer.on("/PSRAMreset", HTTP_GET, [](AsyncWebServerRequest* request) {  //Reinitialize PSRAM
    Serial.println("Admin: Reinitilizing PSRAM");
    if (psramInit()) {
      Serial.println("\nPSRAM is correctly initialized");
      PSRAM_Status = ("<font color=\"green\"><b>GOOD<b><font color=\"black\">");
    } else {
      Serial.println("PSRAM not available");
      PSRAM_Status = ("<font color=\"red\"><b>FAIL</b><font color=\"black\">");
    }
    request->redirect("../admin");
  });

  AsyncServer.on("/Prefsave", HTTP_GET, [](AsyncWebServerRequest* request) {                     //Reset Preferences.txt
    Serial.println("Admin: Saving Preferences.txt");  
    UpdatePrefs();
    request->redirect("../admin");
  });

  AsyncServer.on("/Prefreset", HTTP_GET, [](AsyncWebServerRequest* request) {                     //Reset Preferences.txt
    Serial.println("Admin: Resetting Preferences.txt");                                           //Pref = (1)WL Data file, (2)Bin Status, (3)Liters per cycle in, (4)LPC out, (5)TOF Empty bin, (6)Bin Volume, (7)Bin Depth, (8)CSV line, (9)Level timer, (10)time (now), (11)Incal Factor, (12)Outcal Factor
    writeFile(SD, "/preferences.txt", "GH02_2023_WL2.csv,FILL,0,0,400,0,0,0,0,0,1.99,1.90\r\n");  //Set the default for Preferences.txt
    request->redirect("../admin");
  });

  AsyncServer.on("/WIFIreset", HTTP_GET, [](AsyncWebServerRequest* request) {  //Reset WiFi
    Serial.println("Admin: Reinitilizing WiFi");
    WiFi.disconnect();                     //Stops any previous Wifi sessions
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  //Initialize Wifi

    WiFi.macAddress(mac);
    snprintf(mac_Id, sizeof(mac_Id), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print("MAC Address: ");
    Serial.println(mac_Id);

    Serial.print("Setting up AP... ");  //Setup Wifi Access Point and print IP
    WiFi.softAP(SOFT_SSID, SOFT_PASSWORD);
    IP = WiFi.softAPIP();
    Serial.print("Address: ");
    Serial.println(IP);

    unsigned long connectAttempt = 1;
    Serial.println("Connecting to Wi-Fi");
    while ((WiFi.status() != WL_CONNECTED) && (connectAttempt <= 10)) {  //Timeout after 10 attempts
      connectAttempt++;
      rgbLedWrite(LED_PIN, 51, 0, 0);
      if (Show_Page == 0) {  //Show that we are trying to connect on splash screen change this later
        display.clear();
        display.drawString(0, 32, "Connecting to WiFi:");
        display.drawString(34, 18, "TIDES");
        display.drawString(18, 30, "Tiny, Integrated");
        display.drawString(0, 42, "Dinural Event Simulator");
        display.display();
      }
      delay(500);
      Serial.print("Trying to connect...");
      //Connect_Counter ++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      rgbLedWrite(LED_PIN, 0, 51, 0);  //Set onboard LED to green @ 20% brightness to indicate Wifi connected
      Serial.print("Wifi Connected!  MAC:");

      WiFi.onEvent(WiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  //If Wifi Discconnected, Reconnect to Wifi/Amazon
      WiFi.onEvent(OnWiFiEvent);
    }
    AsyncServer.begin();  //Start the AP webserver
    request->redirect("../admin");
  });

  AsyncServer.on("/AWSreset", HTTP_GET, [](AsyncWebServerRequest* request) {  //Close valve 1
    Serial.println("Admin: Reinitilizing AWS");
    if (WiFi.status() == WL_CONNECTED) {
      net.setCACert(AWS_CERT_CA);  // Configure WiFiClientSecure to use the AWS IoT device credentials
      net.setCertificate(AWS_CERT_CRT);
      net.setPrivateKey(AWS_CERT_PRIVATE);

      AWSclient.begin(AWS_IOT_ENDPOINT, 8883, net);  // Connect to the MQTT broker on the AWS endpoint we defined earlier
      AWSclient.onMessage(messageHandler);           //Create a message handler


      Serial.print("Connecting to AWS IOT... ");
      while (!AWSclient.connect(THINGNAME)) {
        Serial.print(".");
        delay(100);
      }

      if (!AWSclient.connected()) {
        Serial.println("AWS IoT Timeout!");
        errorMSG = "Error: Failed to connect AWS";
        LogError(errorMSG);
        //errorLog = String(data_date + data_time + "," + errorMSG + "\r\n");
        //appendFile(SD, "/ErrorLog.txt", errorLog.c_str());
        return;
      }

      // Subscribe to a topic
      AWSclient.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
      rgbLedWrite(LED_PIN, 0, 0, 51);  //Set onboard LED to blue @ 20% brightness to indicate AWS connected
      Serial.println("AWS IoT Connected!");
    }
    request->redirect("../admin");
  });

  AsyncServer.on("/AIRTEMPRES", HTTP_GET, [](AsyncWebServerRequest* request) {  //Load the admin page
    if (!sht31.begin(0x44)) {                                                   // Set to 0x45 for alternate i2c addr
      Serial.println("No SHT31 temp sensor");
      SHT31_Flag = false;
    } else {
      SHT31_Flag = true;  //turn flag to true, indicating no error
      Serial.println("Air T/H: Good");
    }

    request->redirect("../admin");
  });

  AsyncServer.on("/WATERTEMPRES", HTTP_GET, [](AsyncWebServerRequest* request) {  //Load the admin page
    sensors.begin();                                                              //Initialize ds18b20 temperature sensor
    //sensors.setResolution(11);
    deviceCount = sensors.getDeviceCount();
    if (deviceCount < 1) {
      Serial.println("No ds18b20 water temp sensor found");
    } else {
      Serial.print(deviceCount);
      Serial.println(" ds18b20 water temp sensors found");
    }

    request->redirect("../admin");
  });


  AsyncServer.on("/TOFRES", HTTP_GET, [](AsyncWebServerRequest* request) {  //Load the admin page
    digitalWrite(8, LOW);       //toggle the TOF OFF/ON
    delay(100);
    digitalWrite(8, HIGH);
    delay(100);
    //TOFsensor.VL53L4CX_Off();

    if (TOFsensor.InitSensor() != 0) {
      Serial.println("Failed to detect and initialize TOF sensor!");

    } else {
      TOFsensor.VL53L4CD_SetRangeTiming(200, 0);
      TOFsensor.VL53L4CD_StartRanging();
      Serial.println("TOF Sensor Ready");
      //When the temperature increases, the ranging value is affected by an offset of 1.3 mm per degree Celsius change. This value is an offset and not a gain, and it does not depend on the target distance.
      //The device embeds a feature that allows compensation for the temperature variation effect. When the ranging is started, a self-calibration is performed once and this allows to remove the ranging drift.
      //To get the most accurate performances, perform a self-calibration when temperature varies. To self-calibrate, call the functions “stop” and “start”, in sequence.
    }

    request->redirect("../admin");
  });

  AsyncServer.on("/switchstatus", HTTP_GET, [](AsyncWebServerRequest* request) {  //Toggle the bin from fill/drain
    Serial.print("Bin Status: ");
    Serial.println(Bin_Status);
    Serial.print("Admin: Switching bin to ");
    if (Bin_Status == "FILL") {
      Serial.println("Drain");
      Bin_Status = "DRAIN";
    } else if (Bin_Status == "DRAIN") {
      Serial.println("Fill");
      Bin_Status = "FILL";
    }
    request->redirect("../admin");
    // Pref = (1)WL Data file, (2)Bin Status, (3)Liters per cycle in, (4)LPC out, (5)TOF Empty bin, (6)Bin Volume, (7)Bin Depth, (8)CSV line, (9)Level timer, (10)time (now), (11)Incal Factor, (12)Outcal Factor
    //String prefEntry = WL_Data + "," + String(Bin_Status) + "," + String(Liters_per_cycle_in) + "," + String(Liters_per_cycle_out) + "," + String(TOFempty) + "," + String(Bin_Volume) + "," + String(Bin_Depth) + "," + String(CSV_linecount) + "," + String(Level_timer) + "," + String(now()) + "," + String(InCalFactor) + "," + String(OutCalFactor) + "\r\n";
    //writeFile(SD, "/preferences.txt", prefEntry.c_str());
    UpdatePrefs();
  });

  AsyncServer.on("/pumpswitch", HTTP_GET, [](AsyncWebServerRequest* request) {  //Toggle the pump relay on/off
    if (Pump_Flag == true) {                                                    //strcmp return 0 for equal strings
      Serial.println("Admin: Switching pump OFF");
      Pump_Flag = false;
      digitalWrite(Pump_Relay_Pin, LOW);
    } else if (Pump_Flag == false) {
      Serial.println("Admin: Switching pump ON");
      Pump_Flag = true;
      digitalWrite(Pump_Relay_Pin, HIGH);
    }
    request->redirect("../admin");
  });

  AsyncServer.on("/ClearErrorLog", HTTP_GET, [](AsyncWebServerRequest* request) {  //Recieve message to erase the error log
    Serial.print("Clear Error Log executed... ");
    SPIFFS.remove("/ErrorLog.txt");  //remove the file from SPIFFS (onboard flash)

    if (SPIFFS.exists("/ErrorLog.txt") == 0) {  //if the file doesn't exist, create it
      File errLog = SPIFFS.open("/ErrorLog.txt");
      Serial.println("ErrorLog file doens't exist...Creating file");
      writeFile(SPIFFS, "/ErrorLog.txt", "Date,Error \r\n");  //create file and set header for the Data File
    }

    request->redirect("../admin");  //redirect to admin page so that nothing weird happens if the page is refreshed by client
  });

  AsyncServer.on("/NextLevel", HTTP_GET, [](AsyncWebServerRequest* request) {  //Load the main page
    Level_timer = 900;
    request->redirect("../admin");  //redirect to admin page so that nothing weird happens if the page is refreshed by client
  });

  // Send a GET request to <ESP_IP>/get?inputString=<inputMessage>
  AsyncServer.on("/get", HTTP_GET, [](AsyncWebServerRequest* request) {
    String inputMessage;
    Serial.print("GET Request: ");
    Serial.println(request->url());

    if (request->hasParam("WLdataselect")) {  //Load or Delete the WL datafile selected on admin page
      WL_Data = request->getParam("WLdataselect")->value();
      if (request->hasParam("Action")) {
        String actionMessage = request->getParam("Action")->value();
        if (actionMessage == "load") {
          Serial.print("Admin: WL Data file changed to ");
          Serial.println(WL_Data);
          LoadWLData();
        } else if (actionMessage == "delete") {
          Serial.print("Admin: WL Data file removed: ");
          Serial.println(WL_Data);
          SD.remove(String("/WL Data/" + WL_Data));
        }
      }

    } else if (request->hasParam("csvLinecount")) {  //change the setting where to read in the WL datafile
      inputMessage = request->getParam("csvLinecount")->value();
      CSV_linecount = inputMessage.toInt();
      Serial.print("Admin: CSV_Linecount changed to ");
      Serial.println(CSV_linecount);
    } else if (request->hasParam("binVolume")) {  //change the current setting for bin volume
      inputMessage = request->getParam("binVolume")->value();
      Bin_Volume = inputMessage.toFloat();
      Serial.print("Admin: Bin_Volume changed to ");
      Serial.println(Bin_Volume);
    } else if (request->hasParam("binDepth")) {  //change the current setting for bin depth
      inputMessage = request->getParam("binDepth")->value();
      Bin_Depth = inputMessage.toInt();
      Serial.print("Admin: Bin_Depth changed to ");
      Serial.println(Bin_Depth);
    } else if (request->hasParam("Emptyoffset")) {  //change the current setting for bin depth
      inputMessage = request->getParam("Emptyoffset")->value();
      TOFempty = inputMessage.toInt();
      Serial.print("Admin: Empty Bin offset changed to ");
      Serial.println(TOFempty);
    } else if (request->hasParam("setWL")) {  //change the current setting for water level
      inputMessage = request->getParam("setWL")->value();
      futureWL = inputMessage.toFloat();
      Serial.print("Admin: Future WL changed to ");
      Serial.println(futureWL);
    } else if (request->hasParam("SetFlowIn")) {  //change the current setting for flow in setpoint
      inputMessage = request->getParam("SetFlowIn")->value();
      InFlowSetPoint = inputMessage.toFloat();
      Serial.print("Admin: InFlow Setpoint changed to ");
      Serial.println(InFlowSetPoint);
    } else if (request->hasParam("SetFlowOut")) {  //change the current setting for flow out setpoint
      inputMessage = request->getParam("SetFlowOut")->value();
      OutFlowSetPoint = inputMessage.toFloat();
      Serial.print("Admin: OutFlow Setpoint changed to ");
      Serial.println(OutFlowSetPoint);
    } else if (request->hasParam("SetCycleIn")) {  //change the current setting for liters per cycle in
      inputMessage = request->getParam("SetCycleIn")->value();
      Liters_per_cycle_in = inputMessage.toFloat();
      Serial.print("Admin: InFlow cycle changed to ");
      Serial.println(Liters_per_cycle_in);
    } else if (request->hasParam("SetCycleOut")) {  //change the current setting for liters per cycle out
      inputMessage = request->getParam("SetCycleOut")->value();
      Liters_per_cycle_out = inputMessage.toFloat();
      Serial.print("Admin: OutFlow cycle changed to ");
      Serial.println(Liters_per_cycle_out);
    } else if (request->hasParam("In_Pulse_Tot")) {  //change the current setting for liters per cycle in
      inputMessage = request->getParam("In_Pulse_Tot")->value();
      In_Pulse_Total = inputMessage.toInt();
      Serial.print("Admin: Total In Pulse changed to ");
      Serial.println(Liters_per_cycle_in);
    } else if (request->hasParam("Out_Pulse_Tot")) {  //change the current setting for liters per cycle out
      inputMessage = request->getParam("Out_Pulse_Tot")->value();
      Out_Pulse_Total = inputMessage.toInt();
      Serial.print("Admin: OutFlow cycle changed to ");
      Serial.println(Liters_per_cycle_out);
    } else if (request->hasParam("In_Cal_Factor")) {
      inputMessage = request->getParam("In_Cal_Factor")->value();
      InCalFactor = inputMessage.toFloat();
      Serial.print("Inflow Calibration Factor set to: ");
      Serial.println(InCalFactor);
    } else if (request->hasParam("Out_Cal_Factor")) {
      inputMessage = request->getParam("Out_Cal_Factor")->value();
      OutCalFactor = inputMessage.toFloat();
      Serial.print("Outflow Calibration Factor set to: ");
      Serial.println(OutCalFactor);
    }
    // Pref = (1)WL Data file, (2)Bin Status, (3)Liters per cycle in, (4)LPC out, (5)TOF Empty bin, (6)Bin Volume, (7)Bin Depth, (8)CSV line, (9)Level timer, (10)time (now), (11)Incal Factor, (12)Outcal Factor

    UpdatePrefs();
    /* old code, has been replaced with UpdatePrefs()
    String prefEntry = WL_Data + "," + String(Bin_Status) + "," + String(Liters_per_cycle_in) + "," + String(Liters_per_cycle_out) + "," + String(TOFempty) + "," + String(Bin_Volume) + "," + String(Bin_Depth) + "," + String(CSV_linecount) + "," + String(Level_timer) + "," + String(now()) + "," + String(InCalFactor) + "," + String(OutCalFactor) + "\r\n";
    writeFile(SD, "/preferences.txt", prefEntry.c_str());
    Serial.print("Updating preference file: ");
    Serial.println(prefEntry);
    */
    request->redirect("../admin");
  });

  ////////////////////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////////////////////

  AsyncServer.onNotFound(notFound);  //what to do if the cleint sends an unknown request
  AsyncServer.begin();               //Start the AP webserver
}

void LogError(String& errlog) {
  errorLog = data_date;
  errorLog += data_time;
  errorLog += ",";
  errorLog += errlog;
  errorLog += "\r\n";
  //errorLog = String(data_date + data_time + "," + errorMSG + "\r\n");
  appendFile(SPIFFS, "/ErrorLog.txt", errorLog.c_str());
}

void UpdatePrefs() {   //Format the preferences string to CSV

  String prefEntry = WL_Data; prefEntry += ",";
  prefEntry += String(Bin_Status); prefEntry += ",";
  prefEntry += String(Liters_per_cycle_in); prefEntry += ",";
  prefEntry += String(Liters_per_cycle_out); prefEntry += ",";
  prefEntry += String(TOFempty); prefEntry += ",";
  prefEntry += String(Bin_Volume); prefEntry += ",";
  prefEntry += String(Bin_Depth); prefEntry += ",";
  prefEntry += String(CSV_linecount); prefEntry += ",";
  prefEntry += String(Level_timer); prefEntry += ",";
  prefEntry += String(now()); prefEntry += ",";
  prefEntry += String(InCalFactor); prefEntry += ",";
  prefEntry += String(OutCalFactor); prefEntry += "\r\n";  //end the string with the new line character

  writeFile(SD, "/preferences.txt", prefEntry.c_str());  //update the preference file on the SD card
  Serial.print("Updating preference file: ");
  Serial.println(prefEntry);
}