#include <SoftTimer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

  /*
   * I2C
   * pin D1 - SCL
   * pin D2 - SDA
   *
   * PIN D7 - green LED
   * PIN D? - red LED TODO
   * PIN D5 - 1-wire DS18B20
   * PIN D6 - soil moisture sensor Vcc pin
   * PIN A0 - soil moisture sensor data pin
   */

Adafruit_BME280 bme;
#define OLED_RESET LED_BUILTIN //4
Adafruit_SSD1306 display(OLED_RESET);

float temperature;
float pressure;
float humidity;

char ssid[] = "ssid";
char wifiPass[] = "pass";

//example
IPAddress ip(127, 0, 0, 1);
IPAddress gateway(127, 0, 0, 1);
IPAddress subnet(255, 255, 255, 255);
IPAddress dns(127, 0, 0, 1);

char wsAddrBme280[] = "";
char wsAddrflowerSoil[] = "";

int buttonPin = D8; //GPIO15
int greenLedPin = D7; //GPIO13

float soilMoisture;
int soilMoistureVccPin = D6;
int soilMoistureDataPin = A0;

float soilTemp;
//byte ds18b20SensorAddress[8] = {0x28, 0xB1, 0x6D, 0xA1, 0x3, 0x0, 0x0, 0x11};
OneWire onewire(D5);
DallasTemperature ds18b20(&onewire);

// ############### tasks ###############
// Define method signature.
void mainFunctions(Task* me);
void checkButton(Task* me);
void feedWatchdog(Task* me);

Task mainFunctionsTask(600000, mainFunctions);
Task checkButtonTask(250, checkButton);
Task watchdogFeederTask(100, feedWatchdog);

// ############ end tasks ############

void setup() {

  Serial.begin(9600);
  Serial.println("Setup");

  //TODO
  //bme.begin() works fine, but wifi network doesn't work this function is invoked
  //bme.begin() must be invoked to init BME280 sensor

  //FIXME wait several moments and push buttons - stupid code!
  if (!bme.begin()) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }
  // bme.begin();
//END

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
//  delay(2000);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2.5);

  ds18b20.begin();

  Serial.println("Connecting ");

  WiFi.config(ip, gateway, subnet, dns);
  WiFi.begin(ssid, wifiPass);

  Serial.println("WiFi status:");
  Serial.println(WiFi.status());

  int wifiStatus = WiFi.status();

  while (wifiStatus != WL_CONNECTED && wifiStatus != 6) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  //button
  pinMode(buttonPin, INPUT);

  //green led
  pinMode(greenLedPin, OUTPUT);
  digitalWrite(greenLedPin, LOW);

  pinMode(soilMoistureVccPin, OUTPUT);

  SoftTimer.add(&mainFunctionsTask);
  SoftTimer.add(&checkButtonTask);
  SoftTimer.add(&watchdogFeederTask);
}

void mainFunctions(Task* me){
  readDataFromBme280();

  readSoilParams();

  displayDataOnOled();

  sendJson(getJsonBme(), true);
  sendJson(getJsonflowerSoil(), false);

  display.clearDisplay();
}

void checkButton(Task* me) {
  if (digitalRead(buttonPin) == 1) {
    Serial.println("button pushed");
    mainFunctions(me);
  }
}

void readDataFromBme280() {
  temperature = bme.readTemperature();
  pressure = bme.readPressure() / 100;
  humidity = bme.readHumidity();
}

void readSoilParams() {
  soilMoisture = 0;
  digitalWrite (soilMoistureVccPin, HIGH);
  delay (500);
  int n = 3;
  for(int i = 0; i < n; i++) // read sensor "n" times and get the average
  {
    soilMoisture += analogRead(soilMoistureDataPin);
    delay(500);
  }
  digitalWrite (soilMoistureVccPin, LOW);
  soilMoisture = soilMoisture / n;
  soilMoisture = map(soilMoisture, 600, 0, 0, 100);

  readSoilTemp();
}

void readSoilTemp() {
  soilTemp = 0;
  
  ds18b20.requestTemperatures();

  // Reads the temperature from sensor
  soilTemp = ds18b20.getTempCByIndex(0);
}

void displayDataOnOled() {
  display.setCursor(0,0);
  display.print(temperature);
  display.println(" C");
  display.print(pressure);
  display.println("hPa");
  display.print(humidity);
  display.println(" %");
  display.print("k1 ");
  display.print(soilMoisture);
  display.println(" %");
  display.display();
}

char* getJsonBme() {
  static char jsonMessageBuffer[300];
  StaticJsonBuffer<300> JSONbuffer;
  JsonObject& bme280 = JSONbuffer.createObject();
  bme280["temperature"] = temperature;
  bme280["pressure"] = pressure;
  bme280["humidity"] = humidity;

  bme280.prettyPrintTo(jsonMessageBuffer, sizeof(jsonMessageBuffer));
  Serial.println("Sending JSON:");
  Serial.println(jsonMessageBuffer);
  Serial.println();
  return jsonMessageBuffer;
}

char* getJsonflowerSoil() {
  static char jsonMessageBuffer[300];
  StaticJsonBuffer<300> JSONbuffer;
  JsonObject& flowerSoil = JSONbuffer.createObject();
  flowerSoil["flowerId"] = "k1";
  flowerSoil["temperature"] = soilTemp;
  flowerSoil["moisture"] = soilMoisture;

  flowerSoil.prettyPrintTo(jsonMessageBuffer, sizeof(jsonMessageBuffer));
  Serial.println("Sending JSON:");
  Serial.println(jsonMessageBuffer);
  return jsonMessageBuffer;
}

void sendJson(char* jsonMessageBuffer, boolean isBme) {
  HTTPClient http;

  if (isBme) {
    http.begin(wsAddrBme280);
  } else {
    http.begin(wsAddrflowerSoil);
  }

  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(jsonMessageBuffer);

  http.end();

  Serial.println("HTTP response:");
  Serial.println(httpCode);

  if (httpCode == 200) {
    digitalWrite(greenLedPin, HIGH);
  } else {
    digitalWrite(greenLedPin, LOW);
  }
}

void feedWatchdog(Task* me) {
  ESP.wdtFeed();
}
