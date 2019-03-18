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

#include "opentx.h"
#include "pulses/pxx2.h"

uint8_t Pxx2Pulses::addFlag0(uint8_t module)
{
  uint8_t flag0 = g_model.header.modelId[module] & 0x3F;
  if (g_model.moduleData[module].failsafeMode != FAILSAFE_NOT_SET && g_model.moduleData[module].failsafeMode != FAILSAFE_RECEIVER) {
    if (moduleSettings[module].counter == 0) {
      flag0 |= PXX2_CHANNELS_FLAG0_FAILSAFE;
    }
  }
  if (moduleSettings[module].mode == MODULE_MODE_RANGECHECK) {
    flag0 |= PXX2_CHANNELS_FLAG0_RANGECHECK;
  }
  Pxx2Transport::addByte(flag0);
  return flag0;
}

void Pxx2Pulses::addFlag1(uint8_t module)
{
  uint8_t flag1 = 0;
  Pxx2Transport::addByte(flag1);
}

void Pxx2Pulses::addChannels(uint8_t module, uint8_t sendFailsafe, uint8_t firstChannel)
{
  uint16_t pulseValue = 0;
  uint16_t pulseValueLow = 0;

  for (int8_t i=0; i<8; i++) {
    uint8_t channel = firstChannel + i;
    if (sendFailsafe) {
      if (g_model.moduleData[module].failsafeMode == FAILSAFE_HOLD) {
        pulseValue = 2047;
      }
      else if (g_model.moduleData[module].failsafeMode == FAILSAFE_NOPULSES) {
        pulseValue = 0;
      }
      else {
        int16_t failsafeValue = g_model.failsafeChannels[channel];
        if (failsafeValue == FAILSAFE_CHANNEL_HOLD) {
          pulseValue = 2047;
        }
        else if (failsafeValue == FAILSAFE_CHANNEL_NOPULSE) {
          pulseValue = 0;
        }
        else {
          failsafeValue += 2*PPM_CH_CENTER(channel) - 2*PPM_CENTER;
          pulseValue = limit(1, (failsafeValue * 512 / 682) + 1024, 2046);
        }
      }
    }
    else {
      int value = channelOutputs[channel] + 2*PPM_CH_CENTER(channel) - 2*PPM_CENTER;
      pulseValue = limit(1, (value * 512 / 682) + 1024, 2046);
    }

    if (i & 1) {
      Pxx2Transport::addByte(pulseValueLow); // Low byte of channel
      Pxx2Transport::addByte(((pulseValueLow >> 8) & 0x0F) | (pulseValue << 4));  // 4 bits each from 2 channels
      Pxx2Transport::addByte(pulseValue >> 4);  // High byte of channel
    }
    else {
      pulseValueLow = pulseValue;
    }
  }
}

void Pxx2Pulses::setupChannelsFrame(uint8_t module)
{
  addFrameType(PXX2_TYPE_C_MODULE, PXX2_TYPE_ID_CHANNELS);

  // FLAG0
  uint8_t flag0 = addFlag0(module);

  // FLAG1
  addFlag1(module);

  // Channels
  uint8_t channelsCount = sentModuleChannels(module);
  addChannels(module, flag0 & PXX2_CHANNELS_FLAG0_FAILSAFE, g_model.moduleData[module].channelsStart);
  if (channelsCount > 8) {
    addChannels(module, flag0 & PXX2_CHANNELS_FLAG0_FAILSAFE, g_model.moduleData[module].channelsStart + 8);
    if (channelsCount > 16) {
      addChannels(module, flag0 & PXX2_CHANNELS_FLAG0_FAILSAFE, g_model.moduleData[module].channelsStart + 16);
    }
  }
}

void Pxx2Pulses::setupHardwareInfoFrame(uint8_t module)
{
  if (reusableBuffer.hardware.modules[module].step >= -1 && reusableBuffer.hardware.modules[module].step < PXX2_MAX_RECEIVERS_PER_MODULE) {
    if (reusableBuffer.hardware.modules[module].timeout == 0) {
      addFrameType(PXX2_TYPE_C_MODULE, PXX2_TYPE_ID_HW_INFO);
      Pxx2Transport::addByte(reusableBuffer.hardware.modules[module].step);
      reusableBuffer.hardware.modules[module].timeout = 20;
      reusableBuffer.hardware.modules[module].step++;
    }
    else {
      reusableBuffer.hardware.modules[module].timeout--;
      setupChannelsFrame(module);
    }
  }
  else {
    moduleSettings[module].mode = MODULE_MODE_NORMAL;
    setupChannelsFrame(module);
  }
}

void Pxx2Pulses::setupRegisterFrame(uint8_t module)
{
  addFrameType(PXX2_TYPE_C_MODULE, PXX2_TYPE_ID_REGISTER);

  if (reusableBuffer.moduleSetup.pxx2.registerStep == REGISTER_RX_NAME_SELECTED) {
    Pxx2Transport::addByte(0x01);
    for (uint8_t i=0; i<PXX2_LEN_RX_NAME; i++) {
      Pxx2Transport::addByte(zchar2char(reusableBuffer.moduleSetup.pxx2.registerRxName[i]));
    }
    for (uint8_t i=0; i<PXX2_LEN_REGISTRATION_ID; i++) {
      Pxx2Transport::addByte(zchar2char(g_model.modelRegistrationID[i]));
    }
    Pxx2Transport::addByte(reusableBuffer.moduleSetup.pxx2.registerModuleIndex);
  }
  else {
    Pxx2Transport::addByte(0);
  }
}

void Pxx2Pulses::setupReceiverSettingsFrame(uint8_t module)
{
  if (get_tmr10ms() > reusableBuffer.receiverSetup.timeout) {
    addFrameType(PXX2_TYPE_C_MODULE, PXX2_TYPE_ID_RX_SETTINGS);
    uint8_t flag0 = reusableBuffer.receiverSetup.receiverId;
    if (reusableBuffer.receiverSetup.state == RECEIVER_SETTINGS_WRITE)
      flag0 |= PXX2_RX_SETTINGS_FLAG0_WRITE;
    Pxx2Transport::addByte(flag0);
    uint8_t flag1 = 0;
    if (reusableBuffer.receiverSetup.telemetryDisabled)
      flag1 |= PXX2_RX_SETTINGS_FLAG1_TELEMETRY_DISABLED;
    if (reusableBuffer.receiverSetup.pwmRate)
      flag1 |= PXX2_RX_SETTINGS_FLAG1_FASTPWM;
    Pxx2Transport::addByte(flag1);
    uint8_t channelsCount = sentModuleChannels(module);
    for (int i = 0; i < channelsCount; i++) {
      Pxx2Transport::addByte(reusableBuffer.receiverSetup.channelMapping[i]);
    }
    reusableBuffer.receiverSetup.timeout = get_tmr10ms() + 200/*next try in 2s*/;
  }
  else {
    setupChannelsFrame(module);
  }
}

void Pxx2Pulses::setupBindFrame(uint8_t module)
{
  if (reusableBuffer.moduleSetup.pxx2.bindStep == BIND_WAIT) {
    if (get_tmr10ms() > reusableBuffer.moduleSetup.pxx2.bindWaitTimeout) {
      moduleSettings[module].mode = MODULE_MODE_NORMAL;
      reusableBuffer.moduleSetup.pxx2.bindStep = BIND_OK;
      POPUP_INFORMATION(STR_BIND_OK);
    }
    return;
  }

  addFrameType(PXX2_TYPE_C_MODULE, PXX2_TYPE_ID_BIND);

  if (reusableBuffer.moduleSetup.pxx2.bindStep == BIND_RX_NAME_SELECTED) {
    Pxx2Transport::addByte(0x01);
    for (uint8_t i=0; i<PXX2_LEN_RX_NAME; i++) {
      Pxx2Transport::addByte(reusableBuffer.moduleSetup.pxx2.bindCandidateReceiversNames[reusableBuffer.moduleSetup.pxx2.bindSelectedReceiverIndex][i]);
    }
    Pxx2Transport::addByte(reusableBuffer.moduleSetup.pxx2.bindReceiverId); // RX_UID is the slot index (which is unique and never moved)
    Pxx2Transport::addByte(g_model.header.modelId[module]);
  }
  else {
    Pxx2Transport::addByte(0x00);
    for (uint8_t i=0; i<PXX2_LEN_REGISTRATION_ID; i++) {
      Pxx2Transport::addByte(zchar2char(g_model.modelRegistrationID[i]));
    }
  }
}

void Pxx2Pulses::setupSpectrumAnalyser(uint8_t module)
{
  if (moduleSettings[module].counter > 1000) {
    moduleSettings[module].counter = 1002;
    return;
  }

  moduleSettings[module].counter = 1002;

  addFrameType(PXX2_TYPE_C_POWER_METER, PXX2_TYPE_ID_SPECTRUM);
  Pxx2Transport::addByte(0x00);

  reusableBuffer.spectrum.fq = 2440000000;  // 2440MHz
  Pxx2Transport::addWord(reusableBuffer.spectrum.fq);

  reusableBuffer.spectrum.span = 40000000;  // 40MHz
  Pxx2Transport::addWord(reusableBuffer.spectrum.span);

  reusableBuffer.spectrum.step = 100000;  // 100KHz
  Pxx2Transport::addWord(reusableBuffer.spectrum.step);
}

void Pxx2Pulses::setupShareMode(uint8_t module)
{
  addFrameType(PXX2_TYPE_C_MODULE, PXX2_TYPE_ID_SHARE);
  Pxx2Transport::addByte(reusableBuffer.moduleSetup.pxx2.shareReceiverId);
}

void Pxx2Pulses::setupFrame(uint8_t module)
{
  initFrame();

  switch (moduleSettings[module].mode) {
    case MODULE_MODE_GET_HARDWARE_INFO:
      setupHardwareInfoFrame(module);
      break;
    case MODULE_MODE_RECEIVER_SETTINGS:
      setupReceiverSettingsFrame(module);
      break;
    case MODULE_MODE_REGISTER:
      setupRegisterFrame(module);
      break;
    case MODULE_MODE_BIND:
      setupBindFrame(module);
      break;
    case MODULE_MODE_SPECTRUM_ANALYSER:
      setupSpectrumAnalyser(module);
      break;
    case MODULE_MODE_SHARE:
      setupShareMode(module);
      break;
    default:
      setupChannelsFrame(module);
      break;
  }

  if (moduleSettings[module].counter-- == 0) {
    moduleSettings[module].counter = 1000;
  }

  endFrame();
}

template class PxxPulses<Pxx2Transport>;
