#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "BLE2902.h"
#include "BLEHIDDevice.h"
#include "HIDTypes.h"
#include "HIDKeyboardTypes.h"
#include <driver/adc.h>
#include "sdkconfig.h"

#include "BleConnectionStatus.h"
#include "BleGamepad.h"

#if defined(CONFIG_ARDUHAL_ESP_LOG)
  #include "esp32-hal-log.h"
  #define LOG_TAG ""
#include "esp32-hal-log.h"
#define LOG_TAG ""
#else
#include "esp_log.h"
static const char *LOG_TAG = "BLEDevice";
#endif

static const uint8_t _hidReportDescriptor[] = {
    USAGE_PAGE(1), 0x01, // USAGE_PAGE (Generic Desktop)
    USAGE(1), 0x05,      // USAGE (Gamepad)
    COLLECTION(1), 0x01, // COLLECTION (Application)
    USAGE(1), 0x01,      //   USAGE (Pointer)
    COLLECTION(1), 0x00, //   COLLECTION (Physical)
    REPORT_ID(1), 0x01,  //     REPORT_ID (1)
    // ------------------------------------------------- Buttons (1 to 14)
    USAGE_PAGE(1), 0x09,      //     USAGE_PAGE (Button)
    USAGE_MINIMUM(1), 0x01,   //     USAGE_MINIMUM (Button 1)
    USAGE_MAXIMUM(1), 0x0e,   //     USAGE_MAXIMUM (Button 14)
    LOGICAL_MINIMUM(1), 0x00, //     LOGICAL_MINIMUM (0)
    LOGICAL_MAXIMUM(1), 0x01, //     LOGICAL_MAXIMUM (1)
    REPORT_SIZE(1), 0x01,     //     REPORT_SIZE (1)
    REPORT_COUNT(1), 0x0e,    //     REPORT_COUNT (14)
    HIDINPUT(1), 0x02,        //     INPUT (Data, Variable, Absolute) ;14 button bits
    // ------------------------------------------------- Padding
    REPORT_SIZE(1), 0x01,  //     REPORT_SIZE (1)
    REPORT_COUNT(1), 0x02, //     REPORT_COUNT (2)
    HIDINPUT(1), 0x03,     //     INPUT (Constant, Variable, Absolute) ;2 bit padding
    // ------------------------------------------------- X/Y position, Z/rZ position
    USAGE_PAGE(1), 0x01,      //     USAGE_PAGE (Generic Desktop)
    USAGE(1), 0x30,           //     USAGE (X)
    USAGE(1), 0x31,           //     USAGE (Y)
    USAGE(1), 0x32,           //     USAGE (Z)
    USAGE(1), 0x35,           //     USAGE (rZ)
    LOGICAL_MINIMUM(1), 0x81, //     LOGICAL_MINIMUM (-127)
    LOGICAL_MAXIMUM(1), 0x7f, //     LOGICAL_MAXIMUM (127)
    REPORT_SIZE(1), 0x08,     //     REPORT_SIZE (8)
    REPORT_COUNT(1), 0x04,    //     REPORT_COUNT (4)
    HIDINPUT(1), 0x02,        //     INPUT (Data, Variable, Absolute) ;4 bytes (X,Y,Z,rZ)

    USAGE_PAGE(1), 0x01,      //     USAGE_PAGE (Generic Desktop)
    USAGE(1), 0x33,           //     USAGE (rX) Left Trigger
    USAGE(1), 0x34,           //     USAGE (rY) Right Trigger
    LOGICAL_MINIMUM(1), 0x81, //     LOGICAL_MINIMUM (-127)
    LOGICAL_MAXIMUM(1), 0x7f, //     LOGICAL_MAXIMUM (127)
    REPORT_SIZE(1), 0x08,     //     REPORT_SIZE (8)
    REPORT_COUNT(1), 0x02,    //     REPORT_COUNT (2)
    HIDINPUT(1), 0x02,        //     INPUT (Data, Variable, Absolute) ;2 bytes rX, rY

    USAGE_PAGE(1), 0x01,      //     USAGE_PAGE (Generic Desktop)
    USAGE(1), 0x39,           //     USAGE (Hat switch)
    USAGE(1), 0x39,           //     USAGE (Hat switch)
    LOGICAL_MINIMUM(1), 0x01, //     LOGICAL_MINIMUM (1)
    LOGICAL_MAXIMUM(1), 0x08, //     LOGICAL_MAXIMUM (8)
    REPORT_SIZE(1), 0x04,     //     REPORT_SIZE (4)
    REPORT_COUNT(1), 0x02,    //     REPORT_COUNT (2)
    HIDINPUT(1), 0x02,        //     INPUT (Data, Variable, Absolute) ;1 byte Hat1, Hat2

    END_COLLECTION(0), //     END_COLLECTION
    END_COLLECTION(0)  //     END_COLLECTION
};

// HID data bits size in bytes
#define HID_DATA_SIZE 9

BleGamepad::BleGamepad(std::string deviceName, std::string deviceManufacturer, uint8_t batteryLevel) : hid(0)
{
  this->deviceName = deviceName;
  this->deviceManufacturer = deviceManufacturer;
  this->batteryLevel = batteryLevel;
  this->connectionStatus = new BleConnectionStatus();
  this->_previousState = new uint8_t[HID_DATA_SIZE] {0};
  this->_currentState = new uint8_t[HID_DATA_SIZE] {0};
}

void BleGamepad::begin(void)
{
  xTaskCreate(this->taskServer, "server", 20000, (void *)this, 5, NULL);
}

void BleGamepad::end(void)
{
}

void BleGamepad::notify(void)
{
  if (this->isConnected() && memcmp(_previousState, _currentState, HID_DATA_SIZE))
  {
    this->inputGamepad->setValue(_currentState, HID_DATA_SIZE);
    this->inputGamepad->notify();
    memcpy(_previousState, _currentState, HID_DATA_SIZE);
  }
}

void BleGamepad::setAxes(signed char x, signed char y, signed char z, signed char rZ, char rX, char rY, signed char hat)
{
  _currentState[2] = x;
  _currentState[3] = y;
  _currentState[4] = z;
  _currentState[5] = rZ;
  _currentState[6] = (signed char)(rX - 128);
  _currentState[7] = (signed char)(rY - 128);
  _currentState[8] = hat;
  if (_currentState[6] == -128)
  {
    _currentState[6] = -127;
  }
  if (_currentState[7] == -128)
  {
    _currentState[7] = -127;
  }
  notify();
}

void BleGamepad::setHat(uint8_t hat)
{
  _currentState[8] = hat;
  notify();
}

void BleGamepad::press(uint16_t b)
{
  uint16_t *buttons = (uint16_t *)_currentState;
  *buttons = *buttons | b;
  notify();
}

void BleGamepad::release(uint16_t b)
{
  uint16_t *buttons = (uint16_t *)_currentState;
  *buttons = *buttons & ~b;
  notify();
}

bool BleGamepad::isPressed(uint16_t b)
{
  uint16_t *buttons = (uint16_t *)_currentState;
  if ((b & *buttons) > 0)
    return true;
  return false;
}

bool BleGamepad::isConnected(void)
{
  return this->connectionStatus->connected;
}

void BleGamepad::setBatteryLevel(uint8_t level)
{
  this->batteryLevel = level;
  if (hid != 0)
    this->hid->setBatteryLevel(this->batteryLevel);
}

void BleGamepad::taskServer(void *pvParameter)
{
  BleGamepad *BleGamepadInstance = (BleGamepad *)pvParameter; //static_cast<BleGamepad *>(pvParameter);
  BLEDevice::init(BleGamepadInstance->deviceName);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(BleGamepadInstance->connectionStatus);

  BleGamepadInstance->hid = new BLEHIDDevice(pServer);
  BleGamepadInstance->inputGamepad = BleGamepadInstance->hid->inputReport(1); // <-- input REPORTID from report map
  BleGamepadInstance->connectionStatus->inputGamepad = BleGamepadInstance->inputGamepad;

  BleGamepadInstance->hid->manufacturer()->setValue(BleGamepadInstance->deviceManufacturer);

  BleGamepadInstance->hid->pnp(0x01, 0x02e5, 0xabcd, 0x0110);
  BleGamepadInstance->hid->hidInfo(0x00, 0x01);

  BLESecurity *pSecurity = new BLESecurity();

  pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);

  BleGamepadInstance->hid->reportMap((uint8_t *)_hidReportDescriptor, sizeof(_hidReportDescriptor));
  BleGamepadInstance->hid->startServices();

  BleGamepadInstance->onStarted(pServer);

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->setAppearance(HID_GAMEPAD);
  pAdvertising->addServiceUUID(BleGamepadInstance->hid->hidService()->getUUID());
  pAdvertising->start();
  BleGamepadInstance->hid->setBatteryLevel(BleGamepadInstance->batteryLevel);

  ESP_LOGD(LOG_TAG, "Advertising started!");
  vTaskDelay(portMAX_DELAY); //delay(portMAX_DELAY);
}
