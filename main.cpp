/* File: main.cpp
 * Contains base main function and usually all the other stuff that avr does...
 */
/* Copyright (c) 2012-2013 Domen Ipavec (domen.ipavec@z-v.si)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

 #define F_CPU 8000000UL  // 8 MHz
//#include <util/delay.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
//#include <avr/eeprom.h>

#include <stdint.h>

#include "bitop.h"
#include "pwm.h"
#include "random32.h"
#include "exponential.h"

#define ADDRESS 0x50

#define ANIME_PWM 20
#define PWM_FREQ 5 // delitelj od 488Hz
#define DEBOUNCE_TIME 1000 // in ms

avr_cpp_lib::pwm_channel pwm_data[15] = {
		// tuki so ledice od neba
		{&DDRB, &PORTB, PB1, 0},
		{&DDRB, &PORTB, PB0, 0},
		{&DDRD, &PORTD, PD7, 0},
		{&DDRD, &PORTD, PD6, 0},
		{&DDRD, &PORTD, PD5, 0},
		{&DDRB, &PORTB, PB7, 0},
		{&DDRB, &PORTB, PB6, 0},
		{&DDRA, &PORTA, PA3, 0},
		{&DDRA, &PORTA, PA2, 0},
		{&DDRD, &PORTD, PD4, 0},

		// mosfeti
		{&DDRD, &PORTD, PD0, 0xff},
		{&DDRD, &PORTD, PD1, 0xff},
		{&DDRD, &PORTD, PD2, 0xff},
		{&DDRD, &PORTD, PD3, 0xff},
		PWM_CHANNEL_END
	};

#define CONST_MODES 4
uint8_t const mode0_data[] PROGMEM = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
uint8_t const mode1_data[] PROGMEM = { 255, 64, 16, 8, 2, 1, 2, 1, 2, 1 };
uint8_t const mode2_data[] PROGMEM = { 255, 64, 16, 8, 0, 0, 0, 0, 0, 0 };
uint8_t const mode3_data[] PROGMEM = { ANIME_PWM, ANIME_PWM, ANIME_PWM, ANIME_PWM, ANIME_PWM, ANIME_PWM, ANIME_PWM, ANIME_PWM, ANIME_PWM, ANIME_PWM };

uint8_t const * const mode_data[] = {mode0_data, mode1_data, mode2_data, mode3_data};
// mode 3 - 1 random
// mode 4 - many random

avr_cpp_lib::pwm_worker pwm(pwm_data);

uint16_t volatile t(0); // cas v ms
uint16_t volatile prenos_casa_anime(0);
uint8_t volatile mode(0);
uint16_t volatile v(1000);
bool volatile sprememba(true);

static inline void manyrandom() {
	for (uint8_t x = 0; x < 10; x++) {
		pwm_data[x].value = get_random32(2)*ANIME_PWM;
	}
}

static inline void onerandom() {
	for (uint8_t x = 0; x < 10; x++) {
		pwm_data[x].value = 0;
	}
	pwm_data[get_random32(10)].value = ANIME_PWM;
}

static inline void anime(uint16_t dt) {
	prenos_casa_anime += dt;
	if (sprememba) {
		sprememba = false;
		if (mode < CONST_MODES) {
			for (uint8_t x = 0; x < 10; x++) {
				pwm_data[x].value = pgm_read_byte(&(mode_data[mode][x]));
			}
		}
	}
	if (mode >= CONST_MODES) {
		if (prenos_casa_anime >= v) {
			if (mode == CONST_MODES) {
				onerandom();
			} else {
				manyrandom();
			}
			prenos_casa_anime -= v;
		}
	}
}

int main() {
	// init
	// set mosfets to off default
    SETBITS(DDRD, 0b01111);
    SETBITS(PORTD, 0b01111);

	// timer 0 for pwm
	OCR0A = PWM_FREQ;
	TIMSK0 = BIT(OCIE0A);
	TCCR0A = 0b01011;

	// timer 1 za merjenje casa
	OCR1AL = 125; //(to da interrupt na priblizno 4 ms)
	TIMSK1 = BIT(OCIE1A);
	TCCR1B = 0b01011;

	// i2c
	TWCR = 0b01000101;
	TWAR = ADDRESS<<1;

	// enable interrupts
	sei();

	for (;;) {
		uint16_t dt = t;
		t -= dt;

		anime(dt);
	}
}

ISR(TIMER0_COMPA_vect) {
	static uint8_t i = 0;
	pwm.cycle(i);
	i++;
}

ISR(TIMER1_COMPA_vect) {
	t += 4;
}

ISR(TWI_vect) {
	static uint8_t state = 0;
	static uint8_t command = 0;
	switch (TWSR) {
		case 0x60: // SLA+W
			state = 0;
			break;
		case 0x80: // SLA+W + DATA
			switch (state) {
				case 0:
					command = TWDR;
					break;
				case 1:
					switch (command) {
						case 0:
							mode = TWDR;
							sprememba = true;
							break;
						case 1:
							prenos_casa_anime = 0;
							v = TWDR;
							v++;
							v *= 50;
							break;
						case 2:
						case 3:
						case 4:
						case 5:
							pwm_data[command+8].value = 0xff - avr_cpp_lib::exponential(TWDR);
							break;
					}
					break;
			}
			state++;
			break;
	}
	SETBIT(TWCR, TWINT);
}
