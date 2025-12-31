// Board KLUKA_GP_02
// Load Wi-Fi library
#include <Arduino.h>

// připojení potřebných knihoven
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <EEPROM.h>

BLECharacteristic *pCharacteristic; // TX characteristic
// proměnná pro kontrolu připojených zařízení
bool zarizeniPripojeno = false;

bool flagConnected = false;
bool flagRxAndroid = false;
bool flagRxArduino = false;
bool flagSwitch = false;
byte cmd[2];
#define readPin 32

std::string cData;

#define LED 5
#define LED_R 27
#define LED_B 12
#define LED_G 14

#define RELE_POWER 4

//26 25 16 17

//#define pwmMotorRight 26 //P3
//#define pwmMotorLeft 25 //P11
//#define MotorRight 16//35 //P12
//#define MotorLeft 17//34 //P13

#define pwmMotorLeft 16  // P11
#define MotorLeft 17     // P13

#define pwmMotorRight 25 // P3
#define MotorRight 26    // P12

/* Setting PWM Properties */
const int PWMFreq = 5000; /* 5 KHz */
const int PWMChannelLeft = 0;
const int PWMChannelRight = 1;
const int PWMResolution = 8;

// ---- Rampa pro "vpred" ----
const uint16_t RAMP_START_PWM   = 100;
const uint16_t RAMP_END_PWM     = 255;
const uint32_t RAMP_DURATION_MS = 2000;

// --- Heartbeat a smyčka ---
const int LOOP_DELAY_MS    = 10;     // delay v loop
const int INACTIVITY_MS    = 300;    // motor se vypne, když 200 ms nic nepřijde
const int INACTIVITY_TICKS = INACTIVITY_MS / LOOP_DELAY_MS;

bool     rampForwardActive = false;  // právě běží rampa vpřed
bool     forwardLatched    = false;  // po rampě držíme 255 a znovu nespouštíme rampu
uint32_t rampStartMs       = 0;

// proměnná pro ukládání přijaté zprávy
std::string prijataZprava;
// definice unikátních ID pro různé služby
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// třída pro kontrolu připojení
class MyServerCallbacks: public BLEServerCallbacks
{
  void onConnect(BLEServer* pServer) { zarizeniPripojeno = true; }
  void onDisconnect(BLEServer* pServer) { zarizeniPripojeno = false; }
};

void sendBleData(const char *str)
{
  pCharacteristic->setValue(str);
  pCharacteristic->notify();
}

// třída pro příjem zprávy
class MyCallbacks: public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    // načti přijatou zprávu do proměnné
    prijataZprava = pCharacteristic->getValue();

    if (prijataZprava.length() > 0)
    {
      flagRxAndroid = true;
      for (int i = 0; i < (int)prijataZprava.length(); i++)
      {
        Serial.print(prijataZprava[i]);
      }

      cmd[0] = prijataZprava[0];
      cmd[1] = (prijataZprava.length() > 1) ? prijataZprava[1] : 0;

      delay(10);
    }
  }
};

void setMove()
{
  pinMode(MotorLeft, OUTPUT);
  pinMode(MotorRight, OUTPUT);

  ledcSetup(PWMChannelLeft, PWMFreq, PWMResolution);
  ledcSetup(PWMChannelRight, PWMFreq, PWMResolution);
  /* Attach the LED PWM Channel to the GPIO Pin */
  ledcAttachPin(pwmMotorLeft, PWMChannelLeft);
  ledcAttachPin(pwmMotorRight, PWMChannelRight);
}

void test()
{
  ledcWrite(PWMChannelLeft, 180);
  ledcWrite(PWMChannelRight, 180);

  digitalWrite(MotorLeft, HIGH);
  digitalWrite(MotorRight, HIGH);

  digitalWrite(LED_B, LOW);
  delay(10);
  digitalWrite(LED_B, HIGH);

  delay(2000);

  digitalWrite(MotorLeft, LOW);
  digitalWrite(MotorRight, LOW);

  delay(2000);
}

void setup()
{
  setMove();

  pinMode(LED, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  digitalWrite(LED, LOW);
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, HIGH);
  delay(1000);
  digitalWrite(LED, HIGH);
  digitalWrite(LED_R, LOW);
  delay(1000);
  digitalWrite(LED_B, LOW);
  digitalWrite(LED_R, HIGH);
  delay(1000);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, HIGH);
  delay(1000);
  digitalWrite(LED_G, HIGH);
  delay(1000);

  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);

  Serial.begin(9600);

  // inicializace Bluetooth s nastavením jména zařízení
  BLEDevice::init("Zobo");

  // vytvoření BLE serveru
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // vytvoření BLE služby
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // vytvoření BLE komunikačního kanálu pro odesílání (TX)
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());

  // vytvoření BLE komunikačního kanálu pro příjem (RX)
  BLECharacteristic *pCharacteristicRX = pService->createCharacteristic(
                                           CHARACTERISTIC_UUID_RX,
                                           BLECharacteristic::PROPERTY_WRITE
                                         );
  pCharacteristicRX->setCallbacks(new MyCallbacks());

  // zahájení BLE služby
  pService->start();
  // zapnutí viditelnosti BLE
  pServer->getAdvertising()->start();
  Serial.println("BLE nastaveno, ceka na pripojeni..");
}

int searchData(String str)
{
  String temp = "Data: ";

  int j = 0;

  for (int i = 0; i < str.length(); i++)
  {
    if (str[i] != temp[j])
    {
      j = 0;
    }
    else
    {
      if (j == temp.length() - 1)
      {
        return i;
      }
      j++;
    }
  }

  return 0;
}

int timer = 0;
bool flagTimer = false;

void process(byte *data)
{
  switch (data[0])
  {
    case 0: // vzad
      forwardLatched = false;       // měníme režim => zruš latch
      rampForwardActive = false;    // a případnou rampu
      flagTimer = true;
      timer = INACTIVITY_TICKS;
      ledcWrite(PWMChannelLeft, 100);
      ledcWrite(PWMChannelRight, 100);
      digitalWrite(MotorLeft, HIGH);
      digitalWrite(MotorRight, HIGH);
      break;

    case 0x01: // vpred s rampou
      // obnov jen hlídací timer, rampa se NESMÍ resetovat opakovanými příkazy
      flagTimer = true;
      timer = INACTIVITY_TICKS;

      // drž směr vpřed
      digitalWrite(MotorLeft, LOW);
      digitalWrite(MotorRight, LOW);

      // rampu spusť jen pokud ještě neběží a nejsme už „zalatchovaní“ na 255
      if (!rampForwardActive && !forwardLatched) {
        rampStartMs = millis();         // start rampy (jede podle millis)
        rampForwardActive = true;
        ledcWrite(PWMChannelLeft,  RAMP_START_PWM);
        ledcWrite(PWMChannelRight, RAMP_START_PWM);
      }
      break;

    case 0x02: // stop
      forwardLatched = false;
      rampForwardActive = false;
      digitalWrite(LED, LOW);
      delay(20);
      digitalWrite(LED, LOW);
      flagSwitch = true;
      break;

    case 0x04: // vlevo
      forwardLatched = false;
      rampForwardActive = false;
      flagTimer = true;
      timer = INACTIVITY_TICKS;
      ledcWrite(PWMChannelLeft, 255 - 150);
      ledcWrite(PWMChannelRight, 150);
      digitalWrite(MotorLeft, LOW);
      digitalWrite(MotorRight, HIGH);
      break;

    case 0x03: // vpravo
      forwardLatched = false;
      rampForwardActive = false;
      flagTimer = true;
      timer = INACTIVITY_TICKS;
      ledcWrite(PWMChannelLeft, 150);
      ledcWrite(PWMChannelRight, 255 - 150);
      digitalWrite(MotorLeft, HIGH);
      digitalWrite(MotorRight, LOW);
      break;

    case 0x05: // manuální řízení PWM podle data[1]
      forwardLatched = false;
      rampForwardActive = false;
      flagTimer = true;
      timer = INACTIVITY_TICKS;

      if (data[1] >= 50)
      {
        ledcWrite(PWMChannelLeft,  180 - ((data[1]) - 50));
        ledcWrite(PWMChannelRight, 180 + ((data[1]) - 50));
      }
      else // < 50
      {
        ledcWrite(PWMChannelLeft,  180 + (50 - (data[1])));
        ledcWrite(PWMChannelRight, 180 - (50 - (data[1])));
      }

      digitalWrite(MotorLeft, LOW);
      digitalWrite(MotorRight, LOW);
      break;

    case 10:
      // LED příkazy nechávají stav rampy/latche beze změny
      digitalWrite(LED_G, LOW);
      digitalWrite(LED_B, HIGH);
      digitalWrite(LED_R, HIGH);
      Serial.print("green");
      break;

    case 20:
      digitalWrite(LED_G, HIGH);
      digitalWrite(LED_B, HIGH);
      digitalWrite(LED_R, LOW);
      Serial.print("red");
      break;

    case 30:
      digitalWrite(LED_G, HIGH);
      digitalWrite(LED_B, LOW);
      digitalWrite(LED_R, HIGH);
      Serial.print("blue");
      break;

    case 40:
      digitalWrite(LED_G, LOW);
      digitalWrite(LED_B, LOW);
      digitalWrite(LED_R, LOW);
      Serial.print("light");
      break;

    default:
      // ostatní příkazy nezasahují do rampy/latche
      break;
  }
}

void loop()
{
  delay(LOOP_DELAY_MS);

  // Read data from app
  if (flagRxAndroid)
  {
    process(cmd);
    sendBleData("6.25");
    flagRxAndroid = false;
  }

  // ---- Rampa "vpred": plynulé zvyšování PWM 100 -> 255 během 2000 ms ----
  if (rampForwardActive)
  {
    uint32_t elapsed = millis() - rampStartMs;
    if (elapsed >= RAMP_DURATION_MS)
    {
      ledcWrite(PWMChannelLeft,  RAMP_END_PWM);
      ledcWrite(PWMChannelRight, RAMP_END_PWM);
      rampForwardActive = false; // hotovo
      forwardLatched    = true;  // jsme na 255, další 0x01 už rampu nespustí
    }
    else
    {
      uint16_t pwmNow = RAMP_START_PWM +
                        (uint32_t)(RAMP_END_PWM - RAMP_START_PWM) * elapsed / RAMP_DURATION_MS;
      ledcWrite(PWMChannelLeft,  pwmNow);
      ledcWrite(PWMChannelRight, pwmNow);
    }
  }

  // Timer reset motor if no data (hlídací základní timer)
  if (timer > 0)
  {
    timer--;
  }
  else
  {
    if (flagTimer)
    {
      flagTimer = false;

      rampForwardActive = false; // při autostopu stopni i rampu
      forwardLatched    = false; // a uvolni latch
      ledcWrite(PWMChannelLeft,  0);
      ledcWrite(PWMChannelRight, 0);

      digitalWrite(MotorLeft, LOW);
      digitalWrite(MotorRight, LOW);
    }
  }
}
