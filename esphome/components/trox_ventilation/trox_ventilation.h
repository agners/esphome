#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/modbus/modbus.h"

namespace esphome {
namespace trox_ventilation {

static const float TWO_DEC_UNIT = 0.01;
static const float ONE_DEC_UNIT = 0.1;

class TroxVentilation : public PollingComponent, public modbus::ModbusDevice {
 public:
  void update() override;
  void on_modbus_data(const std::vector<uint8_t> &data) override;
  void dump_config() override;

  void set_drosselstellung_sensor(sensor::Sensor *sensor) { this->drosselstellung_ = sensor; }
  void set_volumenstrom_prozent_sensor(sensor::Sensor *sensor) { this->volumenstrom_prozent_ = sensor; }
  void set_druck_drosselorgan_sensor(sensor::Sensor *sensor) { this->druck_drosselorgan_ = sensor; }
  void set_volumenstrom_sensor(sensor::Sensor *sensor) { this->volumenstrom_ = sensor; }
  void set_temperatur_sensor(sensor::Sensor *sensor) { this->temperatur_ = sensor; }
  void set_voc_sensor(sensor::Sensor *sensor) { this->voc_ = sensor; }

  bool is_response_expected() override;

 protected:

  sensor::Sensor *drosselstellung_{nullptr};
  sensor::Sensor *volumenstrom_prozent_{nullptr};
  sensor::Sensor *druck_drosselorgan_{nullptr};
  sensor::Sensor *volumenstrom_{nullptr};
  sensor::Sensor *temperatur_{nullptr};
  sensor::Sensor *voc_{nullptr};
};

}  // namespace trox_ventilation
}  // namespace esphome
