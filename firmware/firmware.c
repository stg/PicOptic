#include <stdint.h>
#include <stdbool.h>
#include <htc.h>

// Run time is exactly 10 cpu cycles/tick (not including call)
void delay(uint16_t ticks) {
	asm("nop");
	while(--ticks);
}

#define DLY delay(65500);

void func(uint8_t val, uint8_t ring) {
  uint8_t rb, ra;
  rb = 1 << (val >> 2);
  ra = 1 << (3 - (val &  3));

  PORTA &= 0xF0;
  PORTB &= 0xF0;

  TRISA = (TRISA & 0xF0) | (ra ^ 0xFF);
  TRISB = (TRISB & 0xF0) | (rb ^ 0xFF);

  if(ring) {
	PORTA = PORTA | ra;
	PORTB = PORTB & ~rb;
  } else {
	PORTA = PORTA & ~ra;
	PORTB = PORTB | rb;
  }


}


void main(void) {
	TRISA = 0b11111111;
	TRISB = 0b11111111;


	T1CON = 0b10001101;

	//TRISA = 0b11111110;
	//TRISB = 0b11111110;
	uint8_t val1 = 0,val2=0,exp1,exp2;
	while(1) {
		if(TMR1H == exp1) {
			exp1 += 19;
			val1 = (val1 + 1) % 12;
		}

		func(val1, 0);

		if(TMR1H == exp2) {
			exp2 += 7;
			val2 = (val2 + 1) % 12;
		}

		func(11 - val2, 1);
		

		/*if(TMR1H & 0x80) {
			PORTA = 0b00000000;
			PORTB = 0b00000001;
		} else {
			PORTA = 0b00000001;
			PORTB = 0b00000000;
		}*/
	}


	while(1) {
		TRISA = 0b11111110;
		TRISB = 0b11111110;
		PORTA = 0b00000000;
		PORTB = 0b00000001;
		DLY;
		PORTA = 0b00000001;
		PORTB = 0b00000000;
		DLY;
		TRISA = 0b11111110;
		TRISB = 0b11111101;
		PORTA = 0b00000000;
		PORTB = 0b00000010;
		DLY;
		PORTA = 0b00000001;
		PORTB = 0b00000000;
		DLY;
		TRISA = 0b11111101;
		TRISB = 0b11111110;
		PORTA = 0b00000000;
		PORTB = 0b00000001;
		DLY;
		PORTA = 0b00000010;
		PORTB = 0b00000000;
		DLY;
		TRISA = 0b11111101;
		TRISB = 0b11111101;
		PORTA = 0b00000000;
		PORTB = 0b00000010;
		DLY;
		PORTA = 0b00000010;
		PORTB = 0b00000000;
		DLY;
	}
}