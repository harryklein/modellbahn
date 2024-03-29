/**************************************************************************
 LocoIno - Configurable Arduino Loconet Module
 Copyright (C) 2014 Daniel Guisado Serra

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 ------------------------------------------------------------------------
 AUTHOR : Dani Guisado - http://www.clubncaldes.com - dguisado@gmail.com
 ------------------------------------------------------------------------
 DESCRIPTION:
 This software emulates the functionality of a GCA50 board from Peter
 Giling (Giling Computer Applications). This is a Loconet Interface
 with 16 I/O that can be individually configured as Input (block sensors)
 or Outputs (switches, lights,...).
 Configuration is done through SV Loconet protocol and can be configured
 from Rocrail (Programming->GCA->GCA50).
 ------------------------------------------------------------------------
 PIN ASSIGNMENT:
 0,1 -> Serial, used to debug and Loconet Monitor (uncomment DEBUG)
 2,3,4,5,6 -> Configurable I/O from 1 to 5
 7 -> Loconet TX (connected to GCA185 shield)
 8 -> Loconet RX (connected to GCA185 shield)
 9,10,11,12,13 -> Configurable I/O from 6 to 10
 A0,A1,A2,A3,A4,A5-> Configurable I/O from 11 to 16
 ------------------------------------------------------------------------
 CREDITS: 
 * Based on MRRwA Loconet libraries for Arduino - http://mrrwa.org/ and 
 * the Loconet Monitor example.
 * Inspired in GCA50 board from Peter Giling - http://www.phgiling.net/
 * Idea also inspired in LocoShield from SPCoast - http://www.scuba.net/
 * Thanks also to Rocrail group - http://www.rocrail.org
 *************************************************************************/

#include <Arduino.h>
#include <LocoNet.h>
#include <EEPROM.h>

boolean processPeerPacket();
void sendPeerPacket(uint8_t p0, uint8_t p1, uint8_t p2);

//Uncomment this line to debug through the serial monitor
#define DEBUG
#define VERSION 101

//Arduino pin assignment to each of the 16 outputs
uint8_t pinMap[16] = { 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };

//3 bytes defining a pin behavior ( http://wiki.rocrail.net/doku.php?id=loconet-io-en )
typedef struct {
	uint8_t cnfg;
	uint8_t value1;
	uint8_t value2;
} PIN_CFG;

//Memory map exchanged with SV read and write commands ( http://wiki.rocrail.net/doku.php?id=lnsv-en )
typedef struct {
	uint8_t vrsion;
	uint8_t addr_low;
	uint8_t addr_high;
	PIN_CFG pincfg[16];
} SV_TABLE;

//Union to access the data with the struct or by index
typedef union {
	SV_TABLE svt;
	uint8_t data[51];
} SV_DATA;

SV_DATA svtable;
lnMsg *LnPacket;

void setup() {
	int n;

	// First initialize the LocoNet interface
	LocoNet.init(7);

	// Configure the serial port for 57600 baud
#ifdef DEBUG
	Serial.begin(57600);
	Serial.println("LocoNet Monitor");
#endif

	//Load config from EEPROM
	for (n = 0; n < 51; n++)
		svtable.data[n] = EEPROM.read(n);

	//Check for a valid config
	if (svtable.svt.vrsion != VERSION) {
		svtable.svt.vrsion = VERSION;
		svtable.svt.addr_low = 81;
		svtable.svt.addr_high = 1;
		EEPROM.write(0, VERSION);
		EEPROM.write(1, svtable.svt.addr_low);
		EEPROM.write(2, svtable.svt.addr_high);
	} else {
		//Configure I/O
		for (n = 0; n < 16; n++) {
			if (bitRead(svtable.svt.pincfg[n].cnfg, 7))
				pinMode(pinMap[n], OUTPUT);
			else {
				pinMode(pinMap[n], INPUT_PULLUP);
				bitWrite(svtable.svt.pincfg[n].value2, 4, digitalRead(pinMap[n]));
			}
		}
	}
}

void loop() {
	int n;

	// Check for any received LocoNet packets
	LnPacket = LocoNet.receive();
	if (LnPacket) {
#ifdef DEBUG
		// First print out the packet in HEX
		Serial.print("RX: ");
		uint8_t msgLen = getLnMsgSize(LnPacket);
		for (uint8_t x = 0; x < msgLen; x++) {
			uint8_t val = LnPacket->data[x];
			// Print a leading 0 if less than 16 to make 2 HEX digits
			if (val < 16)
				Serial.print('0');
			Serial.print(val, HEX);
			Serial.print(' ');
		}
		Serial.println();
#endif

		// If this packet was not a Switch or Sensor Message checks por PEER packet
		if (!LocoNet.processSwitchSensorMessage(LnPacket)) {
			processPeerPacket();
		}
	}

	// Check inputs to inform
	for (n = 0; n < 16; n++) {
		if (!bitRead(svtable.svt.pincfg[n].cnfg, 7))   //Setup as an Input
				{
			//Check if state changed
			if (digitalRead(pinMap[n]) != bitRead(svtable.svt.pincfg[n].value2, 4)) {
#ifdef DEBUG
				Serial.print("INPUT ");
				Serial.print(n);
				Serial.print(" IN PIN ");
				Serial.print(pinMap[n]);
				Serial.print(" CHANGED, INFORM ");
				Serial.println(svtable.svt.pincfg[n].value1 << 1 | bitRead(svtable.svt.pincfg[n].value2, 5));
#endif

				LocoNet.send(OPC_INPUT_REP, svtable.svt.pincfg[n].value1, svtable.svt.pincfg[n].value2);
				//Update state to detect flank (use bit in value2 of SV)
				bitWrite(svtable.svt.pincfg[n].value2, 4, digitalRead(pinMap[n]));

			}
		}
	}

}

// This call-back function is called from LocoNet.processSwitchSensorMessage
// for all Sensor messages
void notifySensor(uint16_t Address, uint8_t State) {
#ifdef DEBUG
	Serial.print("Sensor: ");
	Serial.print(Address, DEC);
	Serial.print(" - ");
	Serial.println(State ? "Active" : "Inactive");
#endif
}

// This call-back function is called from LocoNet.processSwitchSensorMessage
// for all Switch Request messages
void notifySwitchRequest(uint16_t Address, uint8_t Output, uint8_t Direction) {
	int n;

	//Direction must be changed to 0 or 1, not 0 or 32
	Direction ? Direction = 1 : Direction = 0;

#ifdef DEBUG
	Serial.print("Switch Request: ");
	Serial.print(Address, DEC);
	Serial.print(':');
	Serial.print(Direction ? "Closed" : "Thrown");
	Serial.print(" - ");
	Serial.println(Output ? "On" : "Off");
#endif

	//Check if the Address is assigned, configured as output and same Direction
	for (n = 0; n < 16; n++) {
		if ((svtable.svt.pincfg[n].value1 == Address - 1) &&  //Address
				(bitRead(svtable.svt.pincfg[n].cnfg,7) == 1))   //Setup as an Output
				{

			//If pulse (always hardware reset) and Direction, only listen ON message
			if (bitRead(svtable.svt.pincfg[n].cnfg,3) == 1 && bitRead(svtable.svt.pincfg[n].value2,5) == Direction
					&& Output) {
				digitalWrite(pinMap[n], HIGH);
				delay(150);
				digitalWrite(pinMap[n], LOW);
				break;
			}
			//If continue and hardware reset and Direction
			else if (bitRead(svtable.svt.pincfg[n].cnfg,3) == 0 && bitRead(svtable.svt.pincfg[n].cnfg,2) == 1
					&& bitRead(svtable.svt.pincfg[n].value2,5) == Direction) {
				if (Output)
					digitalWrite(pinMap[n], HIGH);
				else
					digitalWrite(pinMap[n], LOW);
				break;
			}
			//If continue and software reset, one Direction ON turns on and other Direction ON turns off
			//OFF messages are not listened
			else if (bitRead(svtable.svt.pincfg[n].cnfg,3) == 0 && bitRead(svtable.svt.pincfg[n].cnfg,2) == 0
					&& Output) {
				if (!Direction)
					digitalWrite(pinMap[n], HIGH);
				else
					digitalWrite(pinMap[n], LOW);
				break;
			}
		}
	}
}

// This call-back function is called from LocoNet.processSwitchSensorMessage
// for all Switch Report messages
void notifySwitchReport(uint16_t Address, uint8_t Output, uint8_t Direction) {
#ifdef DEBUG
	Serial.print("Switch Report: ");
	Serial.print(Address, DEC);
	Serial.print(':');
	Serial.print(Direction ? "Closed" : "Thrown");
	Serial.print(" - ");
	Serial.println(Output ? "On" : "Off");
#endif
}

// This call-back function is called from LocoNet.processSwitchSensorMessage
// for all Switch State messages
void notifySwitchState(uint16_t Address, uint8_t Output, uint8_t Direction) {
#ifdef DEBUG
	Serial.print("Switch State: ");
	Serial.print(Address, DEC);
	Serial.print(':');
	Serial.print(Direction ? "Closed" : "Thrown");
	Serial.print(" - ");
	Serial.println(Output ? "On" : "Off");
#endif
}

boolean processPeerPacket() {
	//Check is a OPC_PEER_XFER message
	if (LnPacket->px.command != OPC_PEER_XFER)
		return (false);

	//Check is my destination
	if ((LnPacket->px.dst_l != 0 || LnPacket->px.d5 != 0)
			&& (LnPacket->px.dst_l != 0x7f || LnPacket->px.d5 != svtable.svt.addr_high)
			&& (LnPacket->px.dst_l != svtable.svt.addr_low || LnPacket->px.d5 != svtable.svt.addr_high))
		return (false);

	//Set high bits in right position
	bitWrite(LnPacket->px.d1, 7, bitRead(LnPacket->px.pxct1,0));
	bitWrite(LnPacket->px.d2, 7, bitRead(LnPacket->px.pxct1,1));
	bitWrite(LnPacket->px.d3, 7, bitRead(LnPacket->px.pxct1,2));
	bitWrite(LnPacket->px.d4, 7, bitRead(LnPacket->px.pxct1,3));

	bitWrite(LnPacket->px.d5, 7, bitRead(LnPacket->px.pxct2,0));
	bitWrite(LnPacket->px.d6, 7, bitRead(LnPacket->px.pxct2,1));
	bitWrite(LnPacket->px.d7, 7, bitRead(LnPacket->px.pxct2,2));
	bitWrite(LnPacket->px.d8, 7, bitRead(LnPacket->px.pxct2,3));

	//OPC_PEER_XFER D1 -> Command (1 SV write, 2 SV read)
	//OPC_PEER_XFER D2 -> Register to read or write
	if (LnPacket->px.d1 == 2) {
		sendPeerPacket(svtable.data[LnPacket->px.d2], svtable.data[LnPacket->px.d2 + 1],
				svtable.data[LnPacket->px.d2 + 2]);
		return (true);
	}

	//Write command
	if (LnPacket->px.d1 == 1) {
		//SV 0 contains the program version (write SV0 == RESET? )
		if (LnPacket->px.d2 > 0) {
			//Store data
			svtable.data[LnPacket->px.d2] = LnPacket->px.d4;
			EEPROM.write(LnPacket->px.d2, LnPacket->px.d4);

#ifdef DEBUG
			Serial.print("ESCRITURA ");
			Serial.print(LnPacket->px.d2);
			Serial.print(" <== ");
			Serial.print(LnPacket->px.d4);
			Serial.print(" | ");
			Serial.print(LnPacket->px.d4, HEX);
			Serial.print(" | ");
			Serial.println(LnPacket->px.d4, BIN);
#endif
		}

		//Answer packet
		sendPeerPacket(0x00, 0x00, LnPacket->px.d4);
		return (true);
	}

	return (false);

}

void sendPeerPacket(uint8_t p0, uint8_t p1, uint8_t p2) {
	lnMsg txPacket;

	txPacket.px.command = OPC_PEER_XFER;
	txPacket.px.mesg_size = 0x10;
	txPacket.px.src = svtable.svt.addr_low;
	txPacket.px.dst_l = LnPacket->px.src;
	txPacket.px.dst_h = LnPacket->px.dst_h;
	txPacket.px.pxct1 = 0x00;
	txPacket.px.d1 = LnPacket->px.d1;  //Original command
	txPacket.px.d2 = LnPacket->px.d2;  //SV requested
	txPacket.px.d3 = svtable.svt.vrsion;
	txPacket.px.d4 = 0x00;
	txPacket.px.pxct2 = 0x00;
	txPacket.px.d5 = svtable.svt.addr_high; //SOURCE high address
	txPacket.px.d6 = p0;
	txPacket.px.d7 = p1;
	txPacket.px.d8 = p2;

	//Set high bits in right position
	bitWrite(txPacket.px.pxct1, 0, bitRead(txPacket.px.d1,7));
	bitClear(txPacket.px.d1, 7);
	bitWrite(txPacket.px.pxct1, 1, bitRead(txPacket.px.d2,7));
	bitClear(txPacket.px.d2, 7);
	bitWrite(txPacket.px.pxct1, 2, bitRead(txPacket.px.d3,7));
	bitClear(txPacket.px.d3, 7);
	bitWrite(txPacket.px.pxct1, 3, bitRead(txPacket.px.d4,7));
	bitClear(txPacket.px.d4, 7);
	bitWrite(txPacket.px.pxct2, 0, bitRead(txPacket.px.d5,7));
	bitClear(txPacket.px.d5, 7);
	bitWrite(txPacket.px.pxct2, 1, bitRead(txPacket.px.d6,7));
	bitClear(txPacket.px.d6, 7);
	bitWrite(txPacket.px.pxct2, 2, bitRead(txPacket.px.d7,7));
	bitClear(txPacket.px.d7, 7);
	bitWrite(txPacket.px.pxct2, 3, bitRead(txPacket.px.d8,7));
	bitClear(txPacket.px.d8, 7);

	LocoNet.send(&txPacket);

#ifdef DEBUG
	Serial.println("Packet sent!");
#endif
}

