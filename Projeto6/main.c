#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdint.h>

#define FIR_TAPS 16
#define COEFF_MAX 32767
#define COEFF_MIN -32768

volatile int16_t coeff[FIR_TAPS] =
{
	-114,-159,-139,291,
	1450,3284,5246,6524,
	6524,5246,3284,1450,
	291,-139,-159,-114
};

int16_t EEMEM ee_coeff[FIR_TAPS];

volatile int16_t samples[FIR_TAPS] = {0};
volatile uint8_t coef_atual = 0;
volatile uint8_t tela = 0;
volatile uint16_t adc_value = 0;
volatile uint8_t dac_value = 0;

// Flag para atualizar o LCD apenas quando houver mudanças
volatile uint8_t flag_update_lcd = 1;

/* LCD: RS=D2 E=D3 D4..D7=D4..D7 */

static void lcd_pulse(void){
	PORTD |= (1<<PD3);
	_delay_us(1);
	PORTD &= ~(1<<PD3);
}

static void lcd_send4(uint8_t n){
	PORTD &= ~((1<<PD4)|(1<<PD5)|(1<<PD6)|(1<<PD7));
	PORTD |= ((n & 0x0F) << 4);
	lcd_pulse();
}

static void lcd_cmd(uint8_t c){
	PORTD &= ~(1<<PD2);
	lcd_send4(c>>4);
	lcd_send4(c);
	_delay_ms(2);
}

static void lcd_data(uint8_t c){
	PORTD |= (1<<PD2);
	lcd_send4(c>>4);
	lcd_send4(c);
}

static void lcd_init(void){
	DDRD |= 0xFC;
	_delay_ms(20);
	lcd_send4(0x03);
	_delay_ms(5);
	lcd_send4(0x03);
	lcd_send4(0x03);
	lcd_send4(0x02);
	lcd_cmd(0x28);
	lcd_cmd(0x0C);
	lcd_cmd(0x06);
	lcd_cmd(0x01);
}

static void lcd_goto(uint8_t l,uint8_t c){
	lcd_cmd((l==0?0x80:0xC0)+c);
}

static void lcd_print(char *s){
	while(*s) lcd_data(*s++);
}

static void adc_init(void){
	ADMUX = (1<<REFS0);
	ADCSRA=(1<<ADEN)|(1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0);
}

static uint16_t adc_read(void){
	ADCSRA|=(1<<ADSC);
	while(ADCSRA&(1<<ADSC));
	return ADC;
}

static void dac_init(void){
	DDRB |= 0x3F;
	DDRC |= (1<<PC4)|(1<<PC5);
}

static void dac_write(uint8_t v){
	if(v&0x01) PORTC|=(1<<PC4); else PORTC&=~(1<<PC4);
	if(v&0x02) PORTC|=(1<<PC5); else PORTC&=~(1<<PC5);

	PORTB &= 0xC0;
	PORTB |= (v>>2);
}

static int16_t fir_filter(int16_t x){
	int32_t acc=0;
	uint8_t i;

	for(i=FIR_TAPS-1;i>0;i--)
	samples[i]=samples[i-1];

	samples[0]=x;

	for(i=0;i<FIR_TAPS;i++)
	acc += (int32_t)samples[i]*coeff[i];

	return (int16_t)(acc>>15);
}

static void timer1_init(void){
	TCCR1B |= (1<<WGM12);
	OCR1A = 2499;
	TIMSK1 |= (1<<OCIE1A);
	TCCR1B |= (1<<CS11)|(1<<CS10);
}

ISR(TIMER1_COMPA_vect){
	adc_value = adc_read();

	int32_t y = fir_filter((int16_t)adc_value);
	
	// Ajuste correto de escala: Converte de 10 bits (0-1023) para 8 bits (0-255)
	y = y >> 2;
	
	// Saturação de segurança para garantir que fique estritamente dentro de 8 bits
	if(y < 0) y = 0;
	if(y > 255) y = 255;
	
	dac_value = (uint8_t)y;
	dac_write(dac_value);
}

static void buttons_init(void){
	DDRC &= ~((1<<PC1)|(1<<PC2)|(1<<PC3));
	PORTC |= (1<<PC1)|(1<<PC2)|(1<<PC3);
}

static void save_coeff(uint8_t i){
	eeprom_update_word((uint16_t*)&ee_coeff[i], coeff[i]);
}

static void load_coeff(void){
	uint8_t i;
	for(i=0;i<FIR_TAPS;i++){
		int16_t v = eeprom_read_word((uint16_t*)&ee_coeff[i]);
		if(v != 0xFFFF) coeff[i]=v;
	}
}

int main(void){
	char buf[17];

	lcd_init();
	adc_init();
	dac_init();
	buttons_init();
	load_coeff();
	timer1_init();

	sei();

	while(1){

		// Botão M (Mudar de tela / Selecionar coeficiente)
		if(!(PINC&(1<<PC2))){
			_delay_ms(150); // Debounce
			tela++;
			if(tela>16) tela=0;

			if(tela>0) coef_atual=tela-1;
			flag_update_lcd = 1; // Solicita atualização do LCD
		}

		if(tela>0){
			// Botão UP (Incrementar coeficiente)
			if(!(PINC&(1<<PC3))){
				_delay_ms(100); // Debounce
				if(coeff[coef_atual] <= (COEFF_MAX - 10)){
					coeff[coef_atual]+=10;
					save_coeff(coef_atual);
					flag_update_lcd = 1; // Solicita atualização do LCD
				}
			}

			// Botão DOWN (Decrementar coeficiente)
			if(!(PINC&(1<<PC1))){
				_delay_ms(100); // Debounce
				if(coeff[coef_atual] >= (COEFF_MIN + 10)){
					coeff[coef_atual]-=10;
					save_coeff(coef_atual);
					flag_update_lcd = 1; // Solicita atualização do LCD
				}
			}
		}

		// Atualiza o LCD de forma limpa e estável apenas se houver mudanças nas variáveis
		if(flag_update_lcd){
			lcd_cmd(0x01); // Limpa o LCD de forma controlada

			if(tela==0){
				lcd_goto(0,0);
				lcd_print("   ELE-3717   ");
				lcd_goto(1,0);
				lcd_print(" FILTRO FIR ");
				}else{
				lcd_goto(0,0);
				sprintf(buf,"Coef C%02u",coef_atual);
				lcd_print(buf);

				lcd_goto(1,0);
				sprintf(buf,"%d",coeff[coef_atual]);
				lcd_print(buf);
			}
			flag_update_lcd = 0; // Reseta a flag de atualização
		}

		_delay_ms(20); // Delay suave em background para estabilidade geral
	}
}
