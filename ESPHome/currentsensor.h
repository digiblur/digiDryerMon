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
    float sctDiff = 0.1;  // difference in current to trigger a publish
    ESP_LOGD("SCT:", "Read");
    digitalWrite(this->intLED1Pin, this->LEDon);
    float sctNewCur = emon1.calcIrms(this->emonCalcIrms);
    digitalWrite(this->intLED1Pin, this->LEDoff);  
    ESP_LOGD("SCT", "Results - Current: %.2f", sctNewCur);
    publish_state(sctNewCur);
  }
};
