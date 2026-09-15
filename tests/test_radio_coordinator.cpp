#include <cassert>
#include <iostream>

#include "radio_coordinator.h"

static void test_ble_scan_lifecycle() {
  RadioCoordinator radio;

  assert(radio.state() == RadioState::Ready);
  assert(radio.wifiScanAllowed());
  assert(radio.advertisingShouldRun());

  assert(radio.requestBleScan());
  assert(radio.state() == RadioState::BleRequested);
  assert(!radio.wifiScanAllowed());
  assert(radio.advertisingShouldRun());

  assert(radio.beginBleScan());
  assert(radio.state() == RadioState::BleScanning);
  assert(!radio.wifiScanAllowed());
  assert(!radio.advertisingShouldRun());

  assert(radio.finishBleScan());
  assert(radio.state() == RadioState::Ready);
  assert(radio.wifiScanAllowed());
  assert(radio.advertisingShouldRun());
}

static void test_duplicate_request_is_rejected() {
  RadioCoordinator radio;
  assert(radio.requestBleScan());
  assert(!radio.requestBleScan());
  assert(radio.beginBleScan());
  assert(!radio.requestBleScan());
}

static void test_failed_scan_start_returns_to_ready() {
  RadioCoordinator radio;
  assert(radio.requestBleScan());
  assert(radio.beginBleScan());
  assert(radio.cancelBleScan());
  assert(radio.state() == RadioState::Ready);
  assert(radio.wifiScanAllowed());
  assert(radio.advertisingShouldRun());
}

static void test_ota_preempts_ble_without_stale_completion() {
  RadioCoordinator radio;
  assert(radio.requestBleScan());
  assert(radio.beginBleScan());

  radio.beginOta();
  assert(radio.state() == RadioState::Ota);
  assert(!radio.wifiScanAllowed());
  assert(!radio.advertisingShouldRun());
  assert(!radio.finishBleScan());
  assert(radio.state() == RadioState::Ota);

  radio.finishOta();
  assert(radio.state() == RadioState::Ready);
}

int main() {
  test_ble_scan_lifecycle();
  test_duplicate_request_is_rejected();
  test_failed_scan_start_returns_to_ready();
  test_ota_preempts_ble_without_stale_completion();
  std::cout << "radio coordinator: 4 tests passed\n";
  return 0;
}
