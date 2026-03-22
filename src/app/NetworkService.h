#ifndef FILAMENTSENSE_APP_NETWORKSERVICE_H
#define FILAMENTSENSE_APP_NETWORKSERVICE_H

#include <Arduino.h>

class NetworkService {
public:
  void connectWifi();
  void syncClock();
  bool sendTelegramReport(const String& message);
};

#endif
