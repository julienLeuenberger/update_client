#include "USBSerialUC.h"
#include <chrono>

#if MBED_CONF_MBED_TRACE_ENABLE
#include "mbed_trace.h"
#define TRACE_GROUP "USBSerialUC"
#endif // MBED_CONF_MBED_TRACE_ENABLE

#include "CandidateApplications.h"
#include "FlashUpdater.h"

namespace update_client {


#if defined(UPDATE_DOWNLOAD)

USBSerialUC::USBSerialUC() :
  m_usbSerial(false) {
  
} 

bool USBSerialUC::isUpdateAvailable() {
  return false;
}

void USBSerialUC::start() {
  m_downloaderThread.start(callback(this, &USBSerialUC::downloadFirmware));
}

void USBSerialUC::stop() {
  m_stopEvent.set(STOP_EVENT_FLAG);
  m_downloaderThread.join();
}

void USBSerialUC::downloadFirmware() {
  while (true) {
    m_usbSerial.connect();
    // we would use wait_ready() with a timeout here, but it is not possible
    // and since we want to make sure to be able to stop the thread
    
    // so check repeatidly if we are connected
    tr_debug("Waiting for connection");
    ThisThread::sleep_for(std::chrono::milliseconds(5000));

    if (m_usbSerial.connected()) {
      // flush the serial connection
      m_usbSerial.sync();

      // initialize internal Flash
      FlashUpdater flashUpdater;
      int err = flashUpdater.init();
      if (0 != err) {
        tr_error("Init flash failed: %d", err);
        return;
      }
      const uint32_t pageSize = flashUpdater.get_page_size();
      tr_debug("Flash page size is %d\r\n", pageSize);

      std::unique_ptr<char> writePageBuffer = std::unique_ptr<char>(new char[pageSize]);
      std::unique_ptr<char> readPageBuffer = std::unique_ptr<char>(new char[pageSize]);

      uint32_t candidateApplicationAddress = 0;
      uint32_t slotSize = 0;
      const uint32_t headerSize = 0x80;
      tr_debug(" Header size is %d", headerSize);  
      update_client::CandidateApplications candidateApplications(flashUpdater, 
                                                                 MBED_CONF_UPDATE_CLIENT_STORAGE_ADDRESS,
                                                                 MBED_CONF_UPDATE_CLIENT_STORAGE_SIZE,
                                                                 headerSize,
                                                                 MBED_CONF_UPDATE_CLIENT_STORAGE_LOCATIONS);
      
      int slotIndex = candidateApplications.getSlotForCandidate();                                                                             
      int32_t result = candidateApplications.getApplicationAddress(slotIndex, candidateApplicationAddress, slotSize);
      uint32_t addr = candidateApplicationAddress; 
      uint32_t sectorSize = flashUpdater.get_sector_size(addr);
      tr_debug("Starting to write at address 0x%08x with sector size %d (aligned %d)", addr, sectorSize, addr % sectorSize);
  
      uint32_t nextSector = addr + sectorSize;
      bool sectorErased = false;
      size_t pagesFlashed = 0;
  
      tr_debug("Please send the update file");
    
      uint32_t nbrOfBytes = 0;    
      while (m_usbSerial.connected()) {
        // receive data for this page
        memset(writePageBuffer.get(), 0, sizeof(char) * pageSize); 
        for (int i = 0; i < pageSize; i++) {
          writePageBuffer.get()[i] = m_usbSerial.getc();
        }

        // write the page to the flash 
        flashUpdater.writePage(pageSize, writePageBuffer.get(), readPageBuffer.get(), 
                               addr, sectorErased, pagesFlashed, nextSector);
        
        // update progress
        nbrOfBytes += pageSize;
        printf("Received %05u bytes\r", nbrOfBytes);
      }
      
      // compare the active application with the downloaded one
      uint32_t activeApplicationHeaderAddress = MBED_ROM_START + MBED_CONF_TARGET_HEADER_OFFSET;
      uint32_t activeApplicationAddress = activeApplicationHeaderAddress + headerSize;
      update_client::MbedApplication activeApplication(flashUpdater, activeApplicationHeaderAddress, activeApplicationAddress);

      update_client::MbedApplication candidateApplication(flashUpdater, candidateApplicationAddress, candidateApplicationAddress + headerSize);
      activeApplication.compareTo(candidateApplication);
    
      writePageBuffer = NULL;
      readPageBuffer = NULL;
      
      flashUpdater.deinit();

      tr_debug("Nbr of bytes received %d", nbrOfBytes);
    }

    // check whether the thread has been stopped

  }

}

#endif

}
