// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CUSTOM_INJECTOR_H
#define CUSTOM_INJECTOR_H

#include "avr_util.h"
#include "custom_defs.h"
#include "lin_frame.h"
#include "injector_actions.h"

// Controls signals injected into linbus framed passed between the master and the
// slave (set/reset selected data bits and adjust the checksum byte). 
//
// Like all the other custom_* files, this file should be adapted to the specific application. 
// The example provided is for a Sport Mode button press injector for 981/Cayman.
namespace custom_injector {
  
  // Private state of the injector. Do not use from other files.
  namespace private_ {
    // Target injection bits for 981CS Sport and PSE buttons
    static const uint8 kTargetedFrameId = 0x8e;
    static const uint8 kTargetedFrameDataBytes = 8;
    static const uint8 kSportByteIndex = 1;
    static const uint8 kSportBitIndex = 2;
    static const uint8 kPSEByteIndex = 1;
    static const uint8 kPSEBitIndex = 7;
    static const uint8 kASSByteIndex = 3;
    static const uint8 kASSBitIndex = 2;
    
    extern byte sport_inject_action;
    extern byte PSE_inject_action;
    extern byte ASS_inject_action;
    
    // True if the current linbus frame is transformed by the injector. Othrwise, the 
    // frame is passed as is.
    extern boolean frame_id_matches;
    
    // Used to calculate the modified frame checksum.
    extern uint16 sum;
    
    // The modified frame checksum byte. 
    extern uint8 checksum;
  }
  
  // ====== These functions should be called from main thread only ================
  
  inline void disableSportInject(void) {
    private_::sport_inject_action = injector_actions::COPY_BIT;
  }

  inline void disablePSEInject(void) {
    private_::PSE_inject_action = injector_actions::COPY_BIT;
  }

  inline void disableASSInject(void) {
    private_::ASS_inject_action = injector_actions::COPY_BIT;
  }

  inline void setSportInject(boolean on) {
    private_::sport_inject_action = on ? injector_actions::FORCE_BIT_1 : injector_actions::FORCE_BIT_0;
  }
    
  inline void setPSEInject(boolean on) {
    private_::PSE_inject_action = on ? injector_actions::FORCE_BIT_1 : injector_actions::FORCE_BIT_0;
  }

  inline void setASSInject(boolean on) {
    private_::ASS_inject_action = on ? injector_actions::FORCE_BIT_1 : injector_actions::FORCE_BIT_0;
  }  

  // ====== These function should be called from lib_processor ISR only =============

  // Called when the id byte is recieved.
  // Called from lin_processor's ISR.
  inline void onIsrFrameIdRecieved(uint8 id) {
    private_::frame_id_matches = false;

    if (id == private_::kTargetedFrameId)
      {
      if (private_::sport_inject_action != injector_actions::COPY_BIT)
         {
         private_::frame_id_matches = true;
         }
      else if (private_::PSE_inject_action != injector_actions::COPY_BIT)
         {
         private_::frame_id_matches = true;
         }
      else if (private_::ASS_inject_action != injector_actions::COPY_BIT)
         {
         private_::frame_id_matches = true;
         }   
      }
    // Linbus checksum V2 includes also the ID byte. 
    private_::sum = custom_defs::kUseLinChecksumVersion2 ? id : 0;
    private_::checksum = 0x00;
  }

  // Called when a data or checksum byte is sent (but not the sync or id bytes). 
  // The injector uses it to compute the modified frame checksum.
  // Called from lin_processor's ISR.
  inline void onIsrByteSent(uint8 byte_index, uint8 b) {
    // If this is not a frame we modify then do nothing.
    if (!private_::frame_id_matches) {
      return;
    }
    
    // Collect the sum. Used later to compute the checksum byte.
    private_::sum += b;
    
    // If we just recieved the last data byte, compute the modified frame checksum.
    if (byte_index == (private_::kTargetedFrameDataBytes - 1)) {
      // Keep adding the high and low bytes until no carry.
      for (;;) {
        const uint8 highByte = (uint8)(private_::sum >> 8);
        if (!highByte) {
          break;  
        }
        // NOTE: this can add additional carry.  
        private_::sum = (private_::sum & 0xff) + highByte; 
      }
      private_::checksum = (uint8)(~private_::sum);
    }
  }

  // Called before sending a data bit of the the data or checksum bytes to get the 
  // transfer function for it.
  // byte_index = 0 for first data byte, 1 for second data byte, ...
  // bit_index = 0 for LSB, 7 for MSB.
  // Called from lin_processor's ISR.
  inline byte onIsrNextBitAction(uint8 byte_index, uint8 bit_index) {
    if (!private_::frame_id_matches) {
      return injector_actions::COPY_BIT;
    }

    // Handle a bit of one of the data bytes.
    if (byte_index < private_::kTargetedFrameDataBytes) {
      if ((byte_index == private_::kSportByteIndex) && (bit_index == private_::kSportBitIndex))
         return private_::sport_inject_action;
      else if ((byte_index == private_::kPSEByteIndex) && (bit_index == private_::kPSEBitIndex))
         return private_::PSE_inject_action;
      else if ((byte_index == private_::kASSByteIndex) && (bit_index == private_::kASSBitIndex))
         return private_::ASS_inject_action;   
      else 
         return injector_actions::COPY_BIT;
    }
    
    // Handle a checksum bit.
    const boolean checksumBit = private_::checksum & bitMask(bit_index);
    return checksumBit ? injector_actions::FORCE_BIT_1 : injector_actions::FORCE_BIT_0;
    
    // TODO: handle the unexpected case of more than 6 + 1 bytes in the frame. For
    // now we will repeat the checksum byte blindly.
  }  
}  // namepsace custom_injector

#endif


