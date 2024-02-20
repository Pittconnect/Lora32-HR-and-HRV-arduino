
#include <Wire.h>
#include <SPI.h>
#include <SdFat.h> // library for sd card
//#include <MAX30100_PulseOximeter.h> // library for max30100
#include <SSD1306Wire.h> // library for Oled
#include <RtcDS3231.h> // Library For RTC In Lora32
#include "images.h" // Call Image.h

SdFat SD;

#define SAMPLING_RATE     MAX30100_SAMPRATE_100HZ
#define PULSE_WIDTH       MAX30100_SPC_PW_1600US_16BITS
#define IR_LED_CURRENTL   MAX30100_LED_CURR_7_6MA
#define IR_LED_CURRENTH   MAX30100_LED_CURR_50MA
#define RED_LED_CURRENT   MAX30100_LED_CURR_27_1MA
#define HIGHRES_MODE      true
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define btnPin            4
#define period            50
#define N_samples         60
#define SAVE              true
#define FAIL              false

// Initialize the OLED display using Arduino Wire:
SSD1306Wire display(0x3c, SDA, SCL);   

PulseOximeter pox;
MAX30100 sensor;
File myFile;

uint16_t tsTempSampStart = 0;
uint8_t count = 0;
uint32_t prev = 0;
bool btnState = 0;

char *dtostrf(double val, signed char width, unsigned char prec, char *s);
void onBeatDetected(void);
void SDbegin(void);
void SDWrite(void);
void SDRead(String);

/////////////////////////////////////////sensor class////////////////////////////////////
class Sensor
{
  protected:
    uint16_t tempT = 0;
    uint16_t IBI = 0;
    uint16_t BPM = 0;
    uint16_t SpO2 = 0;
    uint16_t currCall = 0;
    uint16_t lastCall = 0;
    uint16_t callDuration = 0;
    uint16_t IBIs[N_samples];
    uint16_t SpO2s[N_samples];
    uint16_t tempTs[N_samples];
    uint8_t  wave[64];
    uint8_t  n = 0;
  public:
    Sensor()
    {
      for (int i = 0; i < 64; i++)
        wave[i] = 0;
      for (int i = 0; i < N_samples; i++)
      {
        IBIs[i] = 0;
        SpO2s[i] = 0;
        tempTs[i] = 0;
      } 
    }
    void poxsetup(void);
    void maxsetup(void);
    void senSetup(void);
    void maxupdate(void);
};

void Sensor::poxsetup(void)
{
  Serial.print("Initializing pulse oximeter..");
  if (!pox.begin()) {
    Serial.println("FAILED");
    for (;;);
  } else {
    Serial.println("SUCCESS");
  }
  pox.setIRLedCurrent(IR_LED_CURRENTL);
  pox.setOnBeatDetectedCallback(onBeatDetected);
}

void Sensor::maxsetup(void)
{
  if (!sensor.begin()) {
    Serial.println("FAILED");
    for (;;);
  } else {
    Serial.println("SUCCESS");
  }
  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setLedsCurrent(IR_LED_CURRENTL, RED_LED_CURRENT);
  sensor.setLedsPulseWidth(PULSE_WIDTH);
  sensor.setSamplingRate(SAMPLING_RATE);
  sensor.setHighresModeEnabled(HIGHRES_MODE);
}

void Sensor::maxupdate(void)
{
  currCall = micros();
  callDuration = currCall - lastCall;
  //Serial.println(callDuration);
  pox.update();
  sensor.update();
  lastCall = micros();
}

void Sensor::senSetup(void)
{
  maxsetup();
  poxsetup();
  n = 0;
  BPM = 0;
}
///////////////////////////////////////display class////////////////////////////////////
class OLED :  public Sensor
{
  protected:
    float avgTempT;
    float avgSpO2;
    float avgIBI;
    float avgBPM;
  public:
    void dispsetup(void);
    void readScreen(void);
    void waitScreen(void);
    void resScreen(void);
    void SDsaveScreen(bool);
    friend void SDWrite(void);
};

void OLED::dispsetup(void)
{
  display.init();
  display.flipScreenVertically();
  display.drawXbm(0, 0, 128, 64, myBitmap);
  display.display();
  delay(2000);
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 16, " HRV Analysis" );
  display.drawString(36, 32, "Device");
  display.display();
  delay(2000);
  display.clear();
}

void OLED::readScreen(void)
{
  char buff[30];
  sprintf(buff, "BPM : %3d       %3d", BPM, n);
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, buff);
  for (int i = 0; i < 63; i++)
    display.drawLine(i * 2, 63 - wave[i], (i + 1) * 2, 63 - wave[i + 1]);
  maxupdate();
  display.display();
}

void OLED::waitScreen(void)
{
  display.clear();
  display.drawXbm(0, 0, 128, 64, pressb_bits);
  display.display();
}

void OLED::resScreen(void)
{
  bool state = 1;
  while (state)
  {
    if (digitalRead(btnPin) == LOW)
    {
      state = !state;
      delay(500);
    }
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    char buff[22];
    char str[6];
    sprintf(buff, "HRV RESULT");
    display.drawString(20, 0, buff);
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    dtostrf(avgBPM, 4, 2, str);
    sprintf(buff, ": %s", str);
    display.drawString(0, 20, "Avg BPM");
    display.drawString(70, 20, buff);
    dtostrf(avgIBI, 4, 2, str);
    sprintf(buff, ": %s", str);
    display.drawString(0, 30, "Avg IBI");
    display.drawString(70, 30, buff);
    dtostrf(avgSpO2, 4, 2, str);
    sprintf(buff, ": %s", str);
    display.drawString(0, 40, "Avg SpO2");
    display.drawString(70, 40, buff);
    dtostrf(avgTempT, 4, 2, str);
    sprintf(buff, ": %s", str);
    display.drawString(0, 50, "Avg Temp");
    display.drawString(70, 50, buff);
    display.display();
  }
  btnState = 0;
}

void OLED::SDsaveScreen(bool state)
{
  if (state)
  {
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(2, 0, "FILE SAVED!");
    display.drawXbm(24, 14, 80, 50, savef_bits);
    display.display();
    delay(1000);
  }
  else
  {
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(2, 0, "SAVE FAILED!");
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 20, "Please check SD card");
    display.display();
    delay(1000);
  }
}
//////////////////////////////////////main class////////////////////////////////////////
class HRV : public OLED
{
  private:
    uint16_t minr = 65535;
    uint16_t maxr = 0;
    uint16_t red, ir;

  public:
    void drawWave(void);
    void init(void);
    void compute(void);
    friend void onBeatDetected(void);
};

void HRV::init(void)
{
  dispsetup();
  poxsetup();
  maxsetup();
}

void HRV::drawWave(void)
{
  int amp;
  if ((maxr - minr) >= 500)
  {
    minr = 65535;
    maxr = 0;
  }
  while (sensor.getRawValues(&ir, &red))
  {   
    if (red < minr)
      minr = red;
    if (red > maxr)
      maxr = red;
    amp = map(red, minr, maxr, 0, 40);
    for (int i = 0 ; i < 63; i++)
      wave[i] = wave[i + 1];
    wave[63] = constrain(amp, 0, 40);
  }
}

void HRV::compute(void)
{
  uint32_t IBISum = 0;
  uint32_t SpO2sum = 0;
  uint32_t tempTsum = 0;
  for (int i = 0; i < N_samples; i++)
  {
    IBISum += IBIs[i];
    SpO2sum += SpO2s[i];
    tempTsum += tempTs[i];
  }
  avgIBI = (float)IBISum / N_samples;
  avgBPM = avgIBI / 1000;
  avgBPM = 60 / avgBPM;
  avgSpO2  = (float)SpO2sum / N_samples;
  avgTempT = (float)tempTsum / N_samples;
}
/////////////////////////////////////////setup and loop//////////////////////////////////
HRV  objHRV;
void setup()
{
  Serial.begin(9600);
  objHRV.init();
  pinMode(btnPin, INPUT);
}

void loop()
{
  if (digitalRead(btnPin) == LOW)
  {
    btnState = !btnState;
    delay(500);
    objHRV.senSetup();
    count = 0;
  }

  if (btnState)
  {
    objHRV.maxupdate();
    if ((millis() - prev) >= period)
    {
      objHRV.readScreen();
      objHRV.maxupdate();
      objHRV.drawWave();
      prev = millis();
    }
  }
  else
    objHRV.waitScreen();
}

void onBeatDetected()
{
  count++;
  objHRV.BPM = pox.getHeartRate();
  objHRV.IBI = 60000 / objHRV.BPM;
  objHRV.SpO2 = pox.getSpO2();

  tsTempSampStart = millis();
  Serial.print("Sampling die temperature..");
  sensor.startTemperatureSampling();
  while(!sensor.isTemperatureReady()) {
      if (millis() - tsTempSampStart > 1000) {
          Serial.println("ERROR: timeout");
          // reset
          objHRV.init();
      }
  }
  objHRV.tempT = sensor.retrieveTemperature();
    
  Serial.print("done, temp=");
  Serial.print(objHRV.tempT);
  Serial.println("C");
    
  if (count > 10)
  {
    Serial.print(objHRV.BPM);
    Serial.print("\t");
    Serial.print(objHRV.SpO2);
    Serial.println(" BPM/SpO2");
    
    objHRV.IBIs[objHRV.n] = objHRV.IBI;
    objHRV.SpO2s[objHRV.n] = objHRV.SpO2;
    objHRV.tempTs[objHRV.n] = objHRV.tempT;
    
    objHRV.n++;
    if (objHRV.n >= N_samples)
    {
      objHRV.compute();
      objHRV.resScreen();
      SDWrite();
      count = 0;
      objHRV.n = 0;
    }
  }
}

void SDbegin()
{
 Serial.print("Initializing SD card...");
  if (!SD.begin(SS,SD_SCK_MHZ(1))) {
    Serial.println("initialization failed!");
    return;
  }
 Serial.println("initialization done.");
}

void SDWrite()
{
  SDbegin();
  static uint16_t id = 0;
  id++;
  // Coding For Reset User /100
  if ( id >= 100)
  {
    id =0;
  }
  char buff[10];
  sprintf(buff, "READ%d.json", id); // Save data To Sd Card 
  myFile = SD.open((String)buff, FILE_WRITE);
  if (myFile) { 
    Serial.println("Writing to SD card");
    myFile.println("IBI Values:");
    for (int i = 0; i < N_samples ; i++)
    {
      myFile.println(objHRV.IBIs[i]);
    }
    myFile.println("------------HRV RESULTS------------");
    myFile.print("Avg IBI   :");
    myFile.println(objHRV.avgIBI);
    myFile.print("Avg BPM   :");
    myFile.println(objHRV.avgBPM);
    myFile.print("Avg SpO2  :");
    myFile.println(objHRV.avgSpO2);
    myFile.print("Avg Temp :");
    myFile.println(objHRV.avgTempT);
    objHRV.SDsaveScreen(SAVE);
    Serial.println("done.");
  }
  else {
    objHRV.SDsaveScreen(FAIL);
  }
  myFile.close();
  SDRead((String)buff);
  display.init();
  display.flipScreenVertically();
}

void SDRead(String Str)
{
  myFile = SD.open(Str);
  if (myFile) {
    Serial.println(Str);
    while (myFile.available()) {
      Serial.write(myFile.read());
    }
    myFile.close();
  } else {
    Serial.println("error opening test.txt");
  }
}
