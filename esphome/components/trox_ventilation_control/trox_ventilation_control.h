#pragma once

#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/modbus/modbus.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace trox_ventilation_control {

static const float TWO_DEC_UNIT = 0.01;
static const float ONE_DEC_UNIT = 0.1;

class TroxVentilationControl : public PollingComponent, public select::Select, public modbus::ModbusDevice {
 public:
  TroxVentilationControl() {
    this->mapping_ = { 0, 1, 2, 8 };
  }
  void setup() override;
  void update() override;
  void on_modbus_data(const std::vector<uint8_t> &data) override;
  void dump_config() override;

  void set_ventilation_mode_text_sensor(text_sensor::TextSensor *text_sensor) { this->ventilation_mode_ = text_sensor; }
  bool is_response_expected() override;

 protected:
  void control(const std::string &value) override;
  std::vector<uint16_t> mapping_;
  uint16_t mode_, new_mode_;
  ESPPreferenceObject pref_;
  text_sensor::TextSensor *ventilation_mode_{nullptr};
};

}  // namespace trox_ventilation_control
}  // namespace esphome
