#pragma once

#include "mbed.h"
#include <cstdint>

#include "FlashUpdater.h"

namespace update_client {

class MbedApplication {
public:
  MbedApplication(FlashUpdater& flashUpdater, uint32_t applicationHeaderAddress, uint32_t applicationAddress);

  bool isValid();
  uint64_t getFirmwareVersion();
  uint64_t getFirmwareSize();
  bool isNewerThan(MbedApplication& otherApplication);
  int32_t checkApplication();
  void compareTo(MbedApplication& otherApplication);
  
private:
  int32_t readApplicationHeader();
  int32_t parseInternalHeaderV2(const uint8_t *pBuffer);
  
  static uint32_t parseUint32(const uint8_t *pBuffer);
  static uint64_t parseUint64(const uint8_t *pBuffer);
  static uint32_t crc32(const uint8_t *pBuffer, uint32_t length);

  // data members
  FlashUpdater& m_flashUpdater;
  const uint32_t m_applicationHeaderAddress;
  const uint32_t m_applicationAddress;

  // application header
  // GUID type  
  static const int GUID_SIZE = (128/8);
  typedef uint8_t guid_t[GUID_SIZE];
   
  // SHA256 hash
  static const int SHA256_SIZE = (256/8);
  typedef uint8_t hash_t[SHA256_SIZE];

  enum ApplicationState {
    NOT_CHECKED,
    VALID, 
    NOT_VALID
  };
  struct ApplicationHeader {
    bool initialized;
    uint32_t magic;
    uint32_t headerVersion;
    uint64_t firmwareVersion;
    uint64_t firmwareSize;
    hash_t hash;
    guid_t campaign;
    uint32_t signatureSize;
    uint8_t signature[0];
    ApplicationState state;
  };
  ApplicationHeader m_applicationHeader;

  // the size and offsets defined below do not correspond to the 
  // application header defined above but rather to the definition in
  // the mbed_lib.json file
  // constants defining the header
  static const uint32_t HEADER_VERSION_V2 = 2;
  static const uint32_t HEADER_MAGIC_V2 = 0x5a51b3d4UL;
  static const uint32_t HEADER_SIZE_V2 = 112;
  static const uint32_t FIRMWARE_VERSION_OFFSET_V2 = 8;
  static const uint32_t FIRMWARE_SIZE_OFFSET_V2 = 16;
  static const uint32_t HASH_OFFSET_V2 = 24;
  static const uint32_t CAMPAIGN_OFFSET_V2 = 88;
  static const uint32_t SIGNATURE_SIZE_OFFSET_V2 = 104;
  static const uint32_t HEADER_CRC_OFFSET_V2 = 108;

  // other constants
  static const uint32_t SIZEOF_SHA256 = (256/8);
  static const uint32_t BUFFER_SIZE = 256;
  // buffer used in storage operations 
  uint8_t m_buffer[BUFFER_SIZE];
};

} // namespace