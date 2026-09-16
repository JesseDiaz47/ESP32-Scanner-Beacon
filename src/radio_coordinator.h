#pragma once

enum class RadioState {
  Ready,
  BleRequested,
  BleScanning,
  Ota,
};

class RadioCoordinator {
 public:
  RadioState state() const { return state_; }

  bool requestBleScan() {
    if (state_ != RadioState::Ready) return false;
    state_ = RadioState::BleRequested;
    return true;
  }

  bool beginBleScan() {
    if (state_ != RadioState::BleRequested) return false;
    state_ = RadioState::BleScanning;
    return true;
  }

  bool finishBleScan() {
    if (state_ != RadioState::BleScanning) return false;
    state_ = RadioState::Ready;
    return true;
  }

  bool cancelBleScan() {
    if (state_ != RadioState::BleRequested && state_ != RadioState::BleScanning) {
      return false;
    }
    state_ = RadioState::Ready;
    return true;
  }

  void beginOta() { state_ = RadioState::Ota; }

  void finishOta() {
    if (state_ == RadioState::Ota) state_ = RadioState::Ready;
  }

  bool wifiScanAllowed() const { return state_ == RadioState::Ready; }

  bool advertisingShouldRun() const {
    return state_ == RadioState::Ready || state_ == RadioState::BleRequested;
  }

 private:
  RadioState state_ = RadioState::Ready;
};
