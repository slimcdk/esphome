#pragma once

namespace esphome {
namespace tmc {

class DriverTraits {
 public:
  DriverTraits() = default;

  bool get_supports_stallguard() const { return this->supports_stallguard_; }
  void set_supports_stallguard(bool supports_stallguard) { this->supports_stallguard_ = supports_stallguard; }

  bool get_supports_coolconf() const { return this->supports_coolconf_; }
  void set_supports_coolconf(bool supports_coolconf) { this->supports_coolconf_ = supports_coolconf; }

  bool get_is_addressable() const { return this->is_addressable_; }
  void set_is_addressable(bool is_addressable) { this->is_addressable_ = is_addressable; }

 protected:
  bool supports_stallguard_{false};
  bool supports_coolconf_{false};
  bool is_addressable_{false};
};

}  // namespace tmc
}  // namespace esphome
