#include "esphome.h"
#include <EmonLib.h>
using namespace esphome;

class CurrentSensor : public PollingComponent, public sensor::Sensor {  
  protected:
    uint32_t currentPin;
    uint32_t intLED1Pin;
    int LEDon;
    int LEDoff;
    uint32_t emonCalcIrms;
    uint32_t emonCurrent;
 public:
  EnergyMonitor emon1;
  float sctCur = 99;
  // constructor
  CurrentSensor(uint32_t currentPin,
        uint32_t intLED1Pin,
        int LEDon,
        int LEDoff,
        uint32_t emonCalcIrms,
        uint32_t emonCurrent,
        uint32_t update_interval) : PollingComponent(update_interval) {
            this->currentPin = currentPin;
            this->intLED1Pin = intLED1Pin;
            this->LEDon = LEDon;
            this->LEDoff = LEDoff;
            this->emonCalcIrms = emonCalcIrms;
            this->emonCurrent = emonCurrent;
        }

  void setup() override {
    ESP_LOGD("LS-digiDryerMon - Build", "%s %s",__DATE__,__TIME__);
    // ~~~~ Set PIN Modes
    pinMode(this->intLED1Pin,OUTPUT); 
    pinMode(this->currentPin,INPUT);

    // ~~~~ Setup emonlib
    emon1.current(this->currentPin, this->emonCurrent);
    emon1.calcIrms(this->emonCalcIrms);
    // This will be called by App.setup()
  }
  void update() override {
    char sSCTcurTemp[5]; 
    float sctDiff = 0.1;  // difference in current to trigger a publish

    ESP_LOGD("SCT:", "Read");
    digitalWrite(this->intLED1Pin, this->LEDon);
    float sctNewCur = emon1.calcIrms(this->emonCalcIrms);
    digitalWrite(this->intLED1Pin, this->LEDoff);  
    dtostrf(sctNewCur, 2, 2, sSCTcurTemp);
    ESP_LOGD("SCT", "Results - Current: %s", String(sSCTcurTemp).c_str());
    // check curr difference - update the status?
    if (checkBoundSensor(sctNewCur, sctCur, sctDiff)) {
        sctCur = sctNewCur;
        ESP_LOGD("SCT", "Publish new value");
        digitalWrite(this->intLED1Pin, this->LEDon);
        publish_state(sctCur);
        digitalWrite(this->intLED1Pin, this->LEDoff);     
    }
    
  }
  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
    bool checkBoundSensor(float newValue, float prevValue, float maxDiff) {
        return newValue < prevValue - maxDiff || newValue > prevValue + maxDiff;
    }
};
