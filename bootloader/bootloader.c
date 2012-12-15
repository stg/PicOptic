// ADC based serial bootloader for PIC16(L)F1826/1827
//
// Targeted at HI-TECH C
//
// Intended setup:
// 	 Light sensor powered by RA4, analog output connected to RB4/AN8
//   LED connected between RB0(cathode) and RA0(anode)
//
// Flash addresses 0x000-0x1FF protected and used by bootloader
// Downloaded programs must be offset by 0x200
// 	Reset vector:     0x200
//  Interrupt vector: 0x204
//
// License: CC BY-NC 2.0
//
// senseitg@hotmail.com

#define CLOCK    16000000 // System clock speed (hardcoded in osc_init)
#define LEVEL          42 // Analog high/low trigger level (0-255 = 0-Vdd)
#define BAUDRATE     9600 // Serial baudrate

#include <htc.h>
#include <stdint.h>
#include <stdbool.h>

// Original system pin connections
// RA0 LED col 0             RB0 LED row 0
// RA1 LED col 1             RB1 LED row 1
// RA2 LED col 2             RB2 LED row 2
// RA3 LED col 3             RB3 Button 1
// RA4 Light sensor power    RB4 Light sensor input (AN8)
// RA5 MCLR                  RB5 Button 2
// RA6 Speaker -             RB6 Tuning fork (T1OSI)
// RA7 Speaker +             RB7 Tuning fork (T1OSO)

__CONFIG(0x0FA4);
__CONFIG(0x3EFE);

interrupt redirect_interrupt(void) {
#asm
	ljmp 0x204
#endasm
}

void launch_firmware(void) {
	// Restore hardware state
	ADCON0 = 0b00000000;
	TRISA  = 0b11111111;
	TRISB  = 0b11111111;
	PORTA  = 0b00000000;
	PORTB  = 0b00000000;
	ANSELA = 0b11111111;
	ANSELB = 0b11111111;
#asm
	ljmp 0x200
#endasm
}

// Run time is exactly 10 cpu cycles/tick (not including call)
void delay(uint16_t ticks) {
	asm("nop");
	while(--ticks);
}

// Initialize oscillator
void osc_init() {
	OSCCON = 0b01111010; // Set PLL off, 16MHz HF, internal
}

// Initialize A/D-converter
void adc_init() {
	ADCON1 = 0b00100000; // Format = left justified, Clock = Fosc/32
	ADCON0 = 0b00100001; // Enable ADC @ AN8
}

// Initialize hardware environment
void env_init() {
	ANSELA = 0b00000000;
	ANSELB = 0b00010000;
	TRISA  = 0b11101110;
	TRISB  = 0b11111110;
	PORTA  = 0b00000001;
	PORTB  = 0b00000000;
}

// Initialize
void init() {
	env_init();
	osc_init();
	adc_init();
}

// Sample analog input
bool adc_sample() {
	ADGO = 1;
	while(ADGO);
	return ADRESH > LEVEL;
}

// NAK(negative acknowledge), ACK(acknowledge) ASCII values
#define NAK 0x15
#define ACK 0x06

// Transmit single byte
void tx(uint8_t tx_byte) {
	uint8_t bit_count = 8;	
	PORTA &= 0b11111110;
	while(bit_count--) {	
		delay(100 * (CLOCK / 4000) / BAUDRATE - 2);
		if(tx_byte & 1) PORTA |= 0b00000001;
		else            PORTA &= 0b11111110;
		tx_byte >>= 1;
	}
	delay(100 * (CLOCK / 4000) / BAUDRATE - 1);
	PORTA |= 0b00000001;
	delay(100 * (CLOCK / 4000) / BAUDRATE - 1);
}

// Command buffer
persistent uint8_t command[66];

// Convenience macros
#define FLASH_WR EECON2 = 0x55; EECON2 = 0xAA; WR = 1; asm("nop"); asm("nop");
#define FLASH_RD                               RD = 1; asm("nop"); asm("nop");

// Command executer
void execute() {
	uint16_t addr;
	uint8_t n;
	uint8_t csum;
	if(command[0] == 'W') {
		// Verify checksum
		csum = 0;
		for(n = 1; n < 66; n++) {
			csum += command[n];
		}
		if(csum != command[n]) {
			tx(NAK);
			tx(csum);
		} else {
			addr = command[1] << 5;
			// Enable writes
			WREN   = 1;
			// Erase flash page
			EEADRL = addr;         // Load address
			EEADRH = addr >> 8;
			CFGS   = 0;            // Target flash
			EEPGD  = 1;
			FREE   = 1;            // Specify "erase" operation
			FLASH_WR;              // Execute!
			while(FREE);		   // Wait for finish (not really required?)
			// Load write latches
			for(n = 2; n < 66; ) {
				EEDATH = command[n++]; // Load data
				EEDATL = command[n++];
				LWLO = ((n & 7) != 2); // Specify "load write latch" / actual "write"
                FLASH_WR;              // Execute
				EEADRL++;              // Increase address
			}
			// Disable writes
			WREN = 0;
			// Respond with ACK=success
			// Note: while unlikely, it is possible for flash writes to fail and
			//       it could be considered good practice to verify the data,
			//       which can easily be done by using the R command - see below
			tx(ACK);
		}
	} else if(command[0] == 'R') {
		// R(ead) - respond with ACK=successful
		tx(ACK);
		addr = command[1] << 5;
		csum = command[1];
		// Read data from flash
		EEADRL = addr;             // Load address
		EEADRH = addr >> 8;
		CFGS   = 0;                // Target flash
		EEPGD  = 1;
		for(n = 0; n < 32; n++) {
			FLASH_RD;                  // Execute
			// Load and send data, calculate checksum
			tx(EEDATH); csum += EEDATH;
			tx(EEDATL); csum += EEDATL;
			EEADRL++;                  // Increment address
		}
		// Send checksum
		tx(csum);
	} else if(command[0] == 'B') {
		// Respond with ACK=success
		tx(ACK);
		FVRCON = 0b10000001; // Enable FVR = 1.024V
		ADCON0 = 0b01111101; // Enable ADC @ FVR
		while(!FVRRDY);		 // Wait for FVR to stabilize
		delay(1000);	     // Delay some more (yup, it wobbled)
		ADGO = 1;			 // Sample FVR
		while(ADGO);		 // Wait for sampling
		tx(ADRESH);			 // Send results
		tx(ADRESL);
		FVRCON = 0b00000000; // Disable FVR
		adc_init();			 // Reset ADC
	} else if(command[0] == 'X') {
		// Respond with ACK=success
		tx(ACK);
		launch_firmware();
	} else if(command[0] == 'S') {
		tx(ACK);
		TRISA = 0b00101110;
		for(uint16_t n = 0; n < command[2]<<8; n++) {
			delay(command[1]);
			PORTA = 0b10010001;
			delay(command[1]);
			PORTA = 0b01010001;
		}
		TRISA = 0b11101110;
		tx(ACK);
	} else {
		// Unknown command, respond with NAK=unsuccessful
		tx(NAK);
	}
}

void main() {
	init();
	uint8_t rx_byte;
	uint8_t length = 0;
	uint8_t bit_count;
	uint8_t index;
	uint24_t countdown = 100000; // a couple of secs
	bool wait_mark = true;
	while(1) {
		if(countdown) {
			if(--countdown == 0) launch_firmware();
		}
		if(wait_mark) {
			// Wait for mark;
			if(adc_sample()) wait_mark = false;
		} else {
			// Wait for start-bit
			if(!adc_sample()) {
				// Got start-bit, delay 1.3 bit times (too much latency for 1.5)
				delay(130 * (CLOCK / 4000) / BAUDRATE - 2);
				// Sample 8 bits
				bit_count = 8;
				while(bit_count--) {
					rx_byte = (rx_byte >> 1) | (adc_sample() ? 0x80 : 0x00);
					delay(100 * (CLOCK / 4000) / BAUDRATE - 13);
				}
				// Check stop bit
				if(adc_sample()) {
					if(length) {
						// Expecting data
						command[index++] = rx_byte;
						if(!--length) {
							execute();     // execute command
							countdown = 0; // disable countdown
						}
					} else {
						// Expecting command identifier
						command[0] = rx_byte;
						index = 1;
						switch(rx_byte) {
							case 'W':
								// Write flash, needs 1 page, 64 data, 1 checksum
								length = 67;
								break;
							case 'R':
								// Read flash, needs 1 page
								length = 2;
								break;
							case 'B':
								// Battery voltage readout
								length = 1;
								break;
							case 'X':
								// Execute downloaded program
								length = 1;
								break;
							case 'S':
								// Speaker, expect frequency
								length = 3;
								break;
						}
						// Send number of bytes expected
						tx(length);
						if(length) if(!--length) {
							execute();
							countdown = 0;
						}
					}
				} else {
					// Framing error, wait for mark
					wait_mark = true;
				}
			}
		}
	}
}