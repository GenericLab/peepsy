/* ---------------------------------------------------------------------------------------


	PEEPSY 
	
	by Michael Egger [ a n y m a ]
	 
	based on Continuity Tester by David Johnson-Davies - www.technoblogy.com - 18th November 2017
	ATtiny85 @ 1 MHz (internal oscillator; BOD disabled)
	 
	CC BY 4.0
	Licensed under a Creative Commons Attribution 4.0 International license: 
	http://creativecommons.org/licenses/by/4.0/

	Bytebeat mod: viznut-style algorithmic sound instead of boring beep.
	Each probe touch cycles to a different formula.

--------------------------------------------------------------------------------------- */

#include <avr/sleep.h>
#include <avr/power.h>

const int				Pin_Led_Wake	= 5;
const int				Pin_Led_Sense	= 2;
const int				Pin_Reference	= 0;		// AIN0
const int				Pin_Probe		= 1;		// AIN1
const int				Pin_Speaker_A	= 4;		// OC1B
const int				Pin_Speaker_B	= 3;		// ~OC1B (complementary)

const unsigned long		Timeout = (unsigned long)60*1000; // One minute
volatile unsigned long	Time;

// Bytebeat state
static uint16_t t = 0;
static uint8_t  formula = 0;
static bool     wasSensing = false;

// Speaker pin bitmask for atomic PORTB writes
#define SPK_MASK ((1 << Pin_Speaker_A) | (1 << Pin_Speaker_B))

// Number of bytebeat formulas available
#define NUM_FORMULAS 4

// ----------------------------------------------------------------------------------------
// Pin change interrupt service routine - resets sleep timer
ISR (PCINT0_vect) {
	Time = millis();
}

// ----------------------------------------------------------------------------------------
// Viznut-style bytebeat: returns 8-bit sample
// All formulas chosen to be cheap on ATtiny85 (no HW multiply)
static uint8_t bytebeat(uint16_t t) {
	switch (formula) {
		case 0:  return t * ((t >> 5) | (t >> 8));                              // classic viznut
		case 1:  return (t * 5 & (t >> 7)) | (t * 3 & (t >> 10));              // shift-only harmonics
		case 2:  return t * ((t >> 11) & (t >> 8) & 123 & (t >> 3));           // rhythmic crunch
		case 3:  return (t | (t >> 9) | (t >> 7)) * 10 + 4 * (t & (t >> 13)); // glitchy melody
		default: return 0;
	}
}

// ----------------------------------------------------------------------------------------
void setup () {
	pinMode(Pin_Reference,	INPUT_PULLUP);
	pinMode(Pin_Probe,		INPUT_PULLUP);
	pinMode(Pin_Led_Sense,	OUTPUT);
	pinMode(Pin_Led_Wake,	OUTPUT);
	pinMode(Pin_Speaker_A,	OUTPUT);
	pinMode(Pin_Speaker_B,	OUTPUT);
	
	// Ensure analog comparator is on, not connected to timer, no bandgap ref
	ACSR = 0;

	// No Timer1 for sound — we bitbang the piezo directly
	TCCR1 = 0;
	GTCCR = 0;

	//Pin-change interrupt
	PCMSK = 1 << Pin_Probe;
	GIMSK = GIMSK | 1 << PCIE;
	
	// Power saving
	ADCSRA &= ~(1 << ADEN);
	PRR = 1 << PRUSI | 1 << PRADC;
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	
	// Start running
	//digitalWrite(Pin_Led_Wake, HIGH);
	Time = millis();
}

// ----------------------------------------------------------------------------------------
void loop() {
	// Quiet the speaker before reading comparator — avoids noise coupling
	uint8_t pb = PORTB & ~SPK_MASK;          // both speaker pins LOW
	PORTB = pb;
	delayMicroseconds(10);                    // let comparator settle

	bool Sense = ACSR >> ACO & 1;

	if (Sense) {
		// New touch? Cycle to next formula
		if (!wasSensing) {
			formula = (formula + 1) % NUM_FORMULAS;
			wasSensing = true;
		}

		// Compute bytebeat sample, 1-bit threshold → push-pull piezo
		uint8_t val = bytebeat(t);
		if (val & 0x80) {
			PORTB = (pb | (1 << Pin_Speaker_A));    // PB4=H, PB3=L (atomic)
		} else {
			PORTB = (pb | (1 << Pin_Speaker_B));    // PB4=L, PB3=H (atomic)
		}
		t++;
		PORTB |= (1 << Pin_Led_Sense);

		// Pad to ~8kHz sample rate (minus overhead for bytebeat + comparator settle)
		delayMicroseconds(40);

	} else {
		if (wasSensing) {
			wasSensing = false;
		}
		// Speaker already silenced above
		PORTB &= ~(1 << Pin_Led_Sense);
	}
	
	// Go to sleep?
	if (millis() - Time > Timeout) {
		PORTB &= ~(SPK_MASK | (1 << Pin_Led_Sense));
		digitalWrite(Pin_Led_Wake, 	LOW);
		pinMode(Pin_Reference, 		INPUT);				// Turn off pullup to save power

		sleep_enable();
		sleep_cpu();
		// Carry on here when we wake up

		pinMode(Pin_Reference, INPUT_PULLUP);
		digitalWrite(Pin_Led_Wake, 	HIGH);
	}
}
