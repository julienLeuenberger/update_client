#include "MbedApplication.h"
#include "UCErrorCodes.h"

#if MBED_CONF_MBED_TRACE_ENABLE
#include "mbed_trace.h"
#define TRACE_GROUP "MbedApplication"
#endif // MBED_CONF_MBED_TRACE_ENABLE

#include "bootloader_mbedtls_user_config.h"

#include "mbedtls/sha256.h"

namespace update_client {
  
MbedApplication::MbedApplication(FlashUpdater& flashUpdater, uint32_t applicationHeaderAddress, uint32_t applicationAddress) :
  m_flashUpdater(flashUpdater),
  m_applicationHeaderAddress(applicationHeaderAddress),
  m_applicationAddress(applicationAddress) {
  memset(m_buffer, 0, sizeof(m_buffer));
  memset((void*) &m_applicationHeader, 0, sizeof(m_applicationHeader));
  m_applicationHeader.initialized = false;
  m_applicationHeader.state = NOT_CHECKED;
}

bool MbedApplication::isValid() {
  if (! m_applicationHeader.initialized) {
    int32_t result = readApplicationHeader();
    if (result != UC_ERR_NONE) {
      tr_error(" Invalid application header: %d", result);
      m_applicationHeader.state = NOT_VALID;
    }
  }
  if (m_applicationHeader.state == NOT_CHECKED) {
    int32_t result = checkApplication();
    if (result != UC_ERR_NONE) {
      tr_error(" Application not valid: %d", result);
      m_applicationHeader.state = NOT_VALID;
    }
  }

  return m_applicationHeader.state != NOT_VALID;
}

uint64_t MbedApplication::getFirmwareVersion() {
  if (! m_applicationHeader.initialized) {
    int32_t result = readApplicationHeader();
    if (result != UC_ERR_NONE) {
      tr_error(" Invalid application header: %d", result);
      m_applicationHeader.state = NOT_VALID;
      return 0;
    }
  }

  return m_applicationHeader.firmwareVersion;
}

uint64_t MbedApplication::getFirmwareSize() {
  if (! m_applicationHeader.initialized) {
    int32_t result = readApplicationHeader();
    if (result != UC_ERR_NONE) {
      tr_error(" Invalid application header: %d", result);
      m_applicationHeader.state = NOT_VALID;
      return 0;
    }
  }

  return m_applicationHeader.firmwareSize;
}

bool MbedApplication::isNewerThan(MbedApplication& otherApplication) {
  // read application header if required
  if (! m_applicationHeader.initialized) {
    readApplicationHeader();
  }
  if (! otherApplication.m_applicationHeader.initialized) {
    otherApplication.readApplicationHeader();
  }
  
  // if this application is not valid or empty, it cannot be newer
  if (m_applicationHeader.headerVersion < HEADER_VERSION_V2 ||
      m_applicationHeader.firmwareSize == 0 ||
      m_applicationHeader.state == NOT_VALID) {
    return false;
  }
  // if the other application is not valid or empty, this one is newer
  if (otherApplication.m_applicationHeader.headerVersion < HEADER_VERSION_V2 ||
      otherApplication.m_applicationHeader.firmwareSize == 0 ||
      otherApplication.m_applicationHeader.state == NOT_VALID) {
    return true;
  }
  
  // both applications are valid and not empty
  return otherApplication.m_applicationHeader.firmwareVersion < m_applicationHeader.firmwareVersion;
}
  
int32_t MbedApplication::checkApplication() {
  // read the header
  int32_t result = readApplicationHeader();
  if (result != UC_ERR_NONE) {
    tr_error(" Invalid application header: %d", result);
    m_applicationHeader.state = NOT_VALID;
    return result;
  }
  tr_debug(" Application size is %lld", m_applicationHeader.firmwareSize);

  // at this stage, the header is valid
  // calculate hash if slot is not empty
  if (m_applicationHeader.firmwareSize > 0) {
    // initialize hashing facility 
    mbedtls_sha256_context mbedtls_ctx;
    mbedtls_sha256_init(&mbedtls_ctx);
    mbedtls_sha256_starts(&mbedtls_ctx, 0);

    uint8_t SHA[SIZEOF_SHA256] = { 0 };
    uint32_t remaining = m_applicationHeader.firmwareSize;
    
    // read full image 
    tr_debug(" Calculating hash (start address 0x%08x, size %lld)", m_applicationAddress, m_applicationHeader.firmwareSize);
    while (remaining > 0) {
      // read full buffer or what is remaining 
      uint32_t readSize = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;
     
      // read buffer using FlashIAP API for portability */
      int err = m_flashUpdater.read(m_buffer, m_applicationAddress + (m_applicationHeader.firmwareSize - remaining), readSize);
      if (err != 0) {
        tr_error(" Error while reading flash %d", err);
        result = UC_ERR_READING_FLASH;
        break;
      }

      // update hash
      mbedtls_sha256_update(&mbedtls_ctx, m_buffer, readSize);

      // update remaining bytes
      remaining -= readSize;
    }

    // finalize hash
    mbedtls_sha256_finish(&mbedtls_ctx, SHA);
    mbedtls_sha256_free(&mbedtls_ctx);

    // compare calculated hash with hash from header
    int diff = memcmp(m_applicationHeader.hash, SHA, SIZEOF_SHA256);

    if (diff == 0) {
      result = UC_ERR_NONE;
    }
    else {
      result = UC_ERR_HASH_INVALID;
    }
  } 
  else {
    // header is valid but application size is 0
    result = UC_ERR_FIRMWARE_EMPTY;
  }
  if (result == UC_ERR_NONE) {
    m_applicationHeader.state = VALID;
  }
  else {
    m_applicationHeader.state = NOT_VALID;
  }
  return result;
}
  
void MbedApplication::compareTo(MbedApplication& otherApplication) {
  tr_debug(" Comparing applications at address 0x%08x and 0x%08x", m_applicationAddress, otherApplication.m_applicationAddress);
  
  int32_t result = checkApplication();
  if (result != UC_ERR_NONE) {
    tr_error(" Application not valid");
    return;
  }
  result = otherApplication.checkApplication();
  if (result != UC_ERR_NONE) {
    tr_error(" Other application not valid");
    return;
  }
  tr_debug(" Both applications are valid");

  if (m_applicationHeader.magic != otherApplication.m_applicationHeader.magic) {
    tr_debug("Magic numbers differ");
  }
  if (m_applicationHeader.headerVersion != otherApplication.m_applicationHeader.headerVersion) {
    tr_debug("Header versions differ");
  }
  if (m_applicationHeader.firmwareSize != otherApplication.m_applicationHeader.firmwareSize) {
    tr_debug("Firmware sizes differ");
  }
  if (m_applicationHeader.firmwareVersion != otherApplication.m_applicationHeader.firmwareVersion) {
    tr_debug("Firmware versions differ");
  }
  if (memcmp(m_applicationHeader.hash, otherApplication.m_applicationHeader.hash, sizeof(m_applicationHeader.hash)) != 0) {
    tr_debug("Hash differ");
  }
  
  if (m_applicationHeader.firmwareSize == otherApplication.m_applicationHeader.firmwareSize) {
    tr_debug(" Comparing application binaries");
    const uint32_t pageSize = m_flashUpdater.get_page_size();
    tr_debug("Flash page size is %d", pageSize);

    std::unique_ptr<char> readPageBuffer1 = std::unique_ptr<char>(new char[pageSize]);
    std::unique_ptr<char> readPageBuffer2 = std::unique_ptr<char>(new char[pageSize]);
    uint32_t address1 = m_applicationAddress;
    uint32_t address2 = otherApplication.m_applicationAddress;
    uint32_t nbrOfBytes = 0;
    bool binariesMatch = true;
    while (nbrOfBytes < m_applicationHeader.firmwareSize) {
      result = m_flashUpdater.readPage(pageSize, readPageBuffer1.get(), address1);
      if (result != UC_ERR_NONE) {
        tr_error("Cannot read application 1 (address 0x%08x)", address1);
        binariesMatch = false;
        break;       
      }
      result = m_flashUpdater.readPage(pageSize, readPageBuffer2.get(), address2);
      if (result != UC_ERR_NONE) {
        tr_error("Cannot read application 2 (address 0x%08x)", address2);
        binariesMatch = false;
        break;       
      }

      if (memcmp(readPageBuffer1.get(), readPageBuffer2.get(), pageSize) != 0) {
        tr_error("Applications differ at byte %d (address1 0x%08x - address2 0x%08x)", nbrOfBytes, address1, address2);
        binariesMatch = false;
        break;       
      }
      nbrOfBytes += pageSize;
    }

    if (binariesMatch) {
      tr_debug("Application binaries are identical");
    }
  }  
}
  
int32_t MbedApplication::readApplicationHeader() {  
  // default return code
  int32_t result = UC_ERR_INVALID_HEADER;

  // read magic number and version
  uint8_t version_buffer[8] = { 0 };  
  int err = m_flashUpdater.read(version_buffer, m_applicationHeaderAddress, 8);
  if (0 == err) {    
    // read out header magic
    m_applicationHeader.magic = parseUint32(&version_buffer[0]);
    // read out header magic
    m_applicationHeader.headerVersion = parseUint32(&version_buffer[4]);
    
    tr_debug(" Magic %d, Version %d", m_applicationHeader.magic, m_applicationHeader.headerVersion);
    
    // choose version to decode 
    // TODO: the code below MUST be IMPLEMENTED
    switch (m_applicationHeader.headerVersion) {
      case HEADER_VERSION_V2: {
        // TODO : check magic, if successful read the entire header and call parseInternalHeaderV2
        if (m_applicationHeader.magic == 0x5a51b3d4)
        {
          tr_debug("Magic number is equal to 0x5a51b3d4");
          result = UC_ERR_NONE;
          uint8_t read_buffer[HEADER_SIZE_V2] = {0};
          m_flashUpdater.read(read_buffer, m_applicationHeaderAddress, HEADER_SIZE_V2);
          result = parseInternalHeaderV2(read_buffer);
        }
        else
        {
          tr_debug("Magic number is not equal to 0x5a51b3d4");
        }
        
      }   
      break;

      // Other firmware header versions can be supported here
      default:
      break;
    }
  } 
  else {
    tr_error("Flash read failed: %d", err);
    result = UC_ERR_READING_FLASH;
  }

  m_applicationHeader.initialized = true;
  if (result == UC_ERR_NONE) {
    m_applicationHeader.state = VALID;
  }
  else {
    m_applicationHeader.state = NOT_VALID;
  }
  
  return result;
}

int32_t MbedApplication::parseInternalHeaderV2(const uint8_t *pBuffer) {
  // we expect pBuffer to contain the entire header (version 2)
  int32_t result = UC_ERR_INVALID_HEADER;

  if (pBuffer != NULL) {
    // calculate CRC
    uint32_t calculatedChecksum = crc32(pBuffer, HEADER_CRC_OFFSET_V2);

    // read out CRC
    uint32_t temp32 = parseUint32(&pBuffer[HEADER_CRC_OFFSET_V2]);

    if (temp32 == calculatedChecksum) {
      // parse content 
      m_applicationHeader.firmwareVersion = parseUint64(&pBuffer[FIRMWARE_VERSION_OFFSET_V2]);
      m_applicationHeader.firmwareSize = parseUint64(&pBuffer[FIRMWARE_SIZE_OFFSET_V2]);
      
      tr_debug(" headerVersion %d, firmwareVersion %lld, firmwareSize %lld", 
               m_applicationHeader.headerVersion, m_applicationHeader.firmwareVersion, m_applicationHeader.firmwareSize); 
      
      memcpy(m_applicationHeader.hash, &pBuffer[HASH_OFFSET_V2], SHA256_SIZE);
      memcpy(m_applicationHeader.campaign, &pBuffer[CAMPAIGN_OFFSET_V2], GUID_SIZE);

      // set result
      result = UC_ERR_NONE;
    }
    else {
      result = UC_ERR_INVALID_CHECKSUM;
    }
  }

  return result;
}

uint32_t MbedApplication::parseUint32(const uint8_t* pBuffer) {
  uint32_t result = 0;
  if (pBuffer) {
    result = pBuffer[0];
    result = (result << 8) | pBuffer[1];
    result = (result << 8) | pBuffer[2];
    result = (result << 8) | pBuffer[3];
  }

  return result;
}

uint64_t MbedApplication::parseUint64(const uint8_t *pBuffer) {
  uint64_t result = 0;
  if (pBuffer) {
    result = pBuffer[0];
    result = (result << 8) | pBuffer[1];
    result = (result << 8) | pBuffer[2];
    result = (result << 8) | pBuffer[3];
    result = (result << 8) | pBuffer[4];
    result = (result << 8) | pBuffer[5];
    result = (result << 8) | pBuffer[6];
    result = (result << 8) | pBuffer[7];
  }

  return result;
}

uint32_t MbedApplication::crc32(const uint8_t *pBuffer, uint32_t length) {
  const uint8_t *pCurrent = pBuffer;
  uint32_t crc = 0xFFFFFFFF;

  while (length--) {
    crc ^= *pCurrent;
    pCurrent++;

    for (uint32_t counter = 0; counter < 8; counter++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      }
      else {
        crc = crc >> 1;
      }
    }
  }

  return (crc ^ 0xFFFFFFFF);
}

} // namesapce
