#pragma once

#include "mbed.h"
#include "USBSerial.h"

namespace update_client {

#if defined(UPDATE_DOWNLOAD)

class USBSerialUC {
  
public:
  // constructor
  USBSerialUC();

  // IFirmwareUpdater implementation
  virtual bool isUpdateAvailable();
  virtual void start();
  virtual void stop();

private:
  // private method
  void downloadFirmware();

  // data members  
  USBSerial m_usbSerial;
  Thread m_downloaderThread;
  enum { 
    STOP_EVENT_FLAG = 1
  };
  EventFlags m_stopEvent;
};

#endif

} // namespace

