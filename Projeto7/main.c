#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>

typedef enum {MOD_AM,MOD_FM,MOD_ASK,MOD_FSK} mod_t;

volatile mod_t mod = MOD_AM;
volatile uint16_t carrier_freq = 100;
volatile uint8_t bit_rate = 32;
volatile uint8_t adc_sample = 0;
volatile uint8_t tx_byte = 0;
volatile uint8_t dac_value = 128;

/* ---------- DAC R2R ---------- */
static inline void dac_write(uint8_t v)
{
	if(v & 0x01) PORTC |= (1<<PC4); else PORTC &= ~(1<<PC4);
	if(v & 0x02) PORTC |= (1<<PC5); else PORTC &= ~(1<<PC5);

	uint8_t pb = (v >> 2) & 0x3F;
	PORTB = (PORTB & 0x03) | pb;//(pb << 2);
}

/* ---------- ADC ---------- */
void adc_init(void)
{
	ADMUX = (1<<REFS0) | (1<<ADLAR);
	ADCSRA = (1<<ADEN)|(1<<ADPS2)|(1<<ADPS1);
}

uint8_t adc_read8(void)
{
	ADCSRA |= (1<<ADSC);
	while(ADCSRA & (1<<ADSC));
	return ADCH;
}

/* ---------- LCD (implementar conforme biblioteca usada) ---------- */
void lcd_init(void){}
void lcd_clear(void){}
void lcd_goto(uint8_t l,uint8_t c){}
void lcd_print(char *s){}

/* ---------- DDS ---------- */
volatile uint32_t phase_acc = 0;
volatile uint32_t phase_step = 0;

const uint8_t sine_lut[64] = {
	128,140,153,165,177,188,199,208,
	217,224,231,236,240,243,245,246,
	245,243,240,236,231,224,217,208,
	199,188,177,165,153,140,128,116,
	103,91,79,68,57,48,39,32,
	25,20,15,12,8,5,3,2,
	3,5,8,12,15,20,25,32,
	39,48,57,68,79,91,103,116
};

ISR(TIMER1_COMPA_vect)
{
	adc_sample = adc_read8();
	tx_byte = adc_sample;

	phase_step = ((uint32_t)carrier_freq << 16) / 8000UL;
	phase_acc += phase_step;

	uint8_t carrier = sine_lut[(phase_acc >> 10) & 0x3F];

	switch(mod)
	{
		case MOD_AM:
		{
			
			//dac_value = ((uint16_t)carrier * adc_sample) >> 8;
			int16_t carrier_centered = (int16_t)carrier - 128;
			int16_t am_signal = (carrier_centered * adc_sample) >> 8;
			dac_value = am_signal +128;
		
		}
		break;

		case MOD_FM:
		{
			uint16_t f = carrier_freq + (adc_sample >> 1);
			uint32_t st = ((uint32_t)f << 16) / 8000UL;
			phase_acc += st;
			carrier = sine_lut[(phase_acc >> 10) & 0x3F];
			dac_value = carrier;
		}
		break;

		case MOD_ASK:
		dac_value = (adc_sample > 127) ? carrier : 128;
		break;

		case MOD_FSK:
		{
			uint16_t f = (adc_sample > 127) ? carrier_freq+100 : carrier_freq-100;
			uint32_t st = ((uint32_t)f << 16) / 8000UL;
			phase_acc += st;
			carrier = sine_lut[(phase_acc >> 10) & 0x3F];
			dac_value = carrier;
		}
		break;
	}

	dac_write(dac_value);
}

void timer1_init(void)
{
	TCCR1A = 0;
	TCCR1B = (1<<WGM12)|(1<<CS10);
	OCR1A = 1999; /* 8 kHz */
	TIMSK1 = (1<<OCIE1A);
}

void byte_to_bin(uint8_t v, char *s)
{
	for(int i=0;i<8;i++)
	s[i] = (v & (1<<(7-i))) ? '1':'0';
	s[8]=0;
}

int main(void)
{
	DDRB |= 0b11111100;
	DDRC |= (1<<PC4)|(1<<PC5);

	adc_init();
	timer1_init();
	lcd_init();

	sei();

	char l1[17], l2[17], bits[9];

	while(1)
	{
		byte_to_bin(tx_byte,bits);

		switch(mod)
		{
			case MOD_AM:
			sprintf(l1,"Mod: AM F:%3u",carrier_freq);
			sprintf(l2,"Msg:%3u",adc_sample);
			break;

			case MOD_FM:
			sprintf(l1,"Mod: FM F:%3u",carrier_freq);
			sprintf(l2,"Msg:%3u",adc_sample);
			break;

			case MOD_ASK:
			sprintf(l1,"Mod:ASK T:%2ub",bit_rate);
			sprintf(l2,"Msg:%s",bits);
			break;

			default:
			sprintf(l1,"Mod:FSK T:%2ub",bit_rate);
			sprintf(l2,"Msg:%s",bits);
			break;
		}

		lcd_goto(0,0); lcd_print(l1);
		lcd_goto(1,0); lcd_print(l2);

		_delay_ms(100);
	}
}
