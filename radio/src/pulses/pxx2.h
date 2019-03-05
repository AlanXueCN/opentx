/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _PULSES_PXX2_H_
#define _PULSES_PXX2_H_

#include "fifo.h"
#include "io/frsky_pxx2.h"
#include "./pxx.h"

#define PXX2_TYPE_C_MODULE          0x01
  #define PXX2_TYPE_ID_REGISTER     0x01
  #define PXX2_TYPE_ID_BIND         0x02
  #define PXX2_TYPE_ID_CHANNELS     0x03
  #define PXX2_TYPE_ID_RX_SETUP     0x05
  #define PXX2_TYPE_ID_TELEMETRY    0xFE

#define PXX2_TYPE_C_POWER_METER     0x02
  #define PXX2_TYPE_ID_POWER_METER  0x01
  #define PXX2_TYPE_ID_SPECTRUM     0x02

#define PXX2_TYPE_C_OTA             0xFE

#define PXX2_FLAG0_FAILSAFE         (1 << 6)

const uint8_t DEFAULT_CHANNEL_MAPPING[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
const uint8_t CH9TO16_CHANNEL_MAPPING[] = {0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67};

enum PXX2RegisterSteps {
  REGISTER_START,
  REGISTER_RX_NAME_RECEIVED,
  REGISTER_RX_NAME_SELECTED,
  REGISTER_OK
};

enum PXX2BindSteps {
    BIND_START,
    BIND_RX_NAME_RECEIVED,
    BIND_RX_NAME_SELECTED,
    BIND_WAIT,
    BIND_OK
};

extern ModuleFifo intmoduleFifo;
extern ModuleFifo extmoduleFifo;

class Pxx2CrcMixin {
  protected:
    void initCrc()
    {
      crc = 0xFFFF;
    }

    void addToCrc(uint8_t byte)
    {
      crc -= byte;
    }

    uint16_t crc;
};

class Pxx2Transport: public DataBuffer<uint8_t, 64>, public Pxx2CrcMixin {
  protected:
    void addWord(uint32_t word)
    {
      addByte(word);
      addByte(word >> 8);
      addByte(word >> 16);
      addByte(word >> 24);
    }

    void addByte(uint8_t byte)
    {
      Pxx2CrcMixin::addToCrc(byte);
      addByteWithoutCrc(byte);
    };

    void addByteWithoutCrc(uint8_t byte)
    {
      *ptr++ = byte;
    }
};

class Pxx2Pulses: public PxxPulses<Pxx2Transport> {
  public:
    void setupFrame(uint8_t module);

  protected:
    void setupRegisterFrame(uint8_t module);

    void setupBindFrame(uint8_t module);

    void setupShareMode(uint8_t module);

    void setupChannelsFrame(uint8_t module);

    void setupSpectrumAnalyser(uint8_t module);

    void addHead()
    {
      // send 7E, do not CRC
      Pxx2Transport::addByteWithoutCrc(0x7E);

      // reserve 1 byte for LEN
      Pxx2Transport::addByteWithoutCrc(0x00);
    }

    void addFrameType(uint8_t type_c, uint8_t type_id)
    {
      // TYPE_C + TYPE_ID
      // TODO optimization ? Pxx2Transport::addByte(0x26); // This one is CRC-ed on purpose

      Pxx2Transport::addByte(type_c);
      Pxx2Transport::addByte(type_id);
    }

    uint8_t addFlag0(uint8_t module);

    void addFlag1(uint8_t module);

    void addChannels(uint8_t module, uint8_t sendFailsafe, uint8_t firstChannel);

    void addCrc()
    {
      Pxx2Transport::addByteWithoutCrc(Pxx2CrcMixin::crc >> 8);
      Pxx2Transport::addByteWithoutCrc(Pxx2CrcMixin::crc);
    }

    void initFrame()
    {
      // init the CRC counter
      initCrc();

      // reset the frame pointer
      Pxx2Transport::initBuffer();

      // add the frame head
      addHead();
    }

    void endFrame()
    {
      uint8_t size = getSize() - 2;

      if (size > 0) {
        // update the frame LEN = frame length minus the 2 first bytes
        data[1] = getSize() - 2;

        // now add the CRC
        addCrc();
      }
      else {
        Pxx2Transport::initBuffer();
      }
    }
};

#endif
