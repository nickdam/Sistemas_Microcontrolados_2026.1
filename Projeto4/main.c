/*
 * main.c
 *
 * Created: 4/27/2026 5:32:13 PM
 *  Author: Nicolas
 */ 

/*#include <xc.h>

int main(void)
{
    while(1)
    {
        //TODO:: Please write your application code 
    }
}*/

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

// ================= ENUMERADORES =================
typedef enum {
	NONE = 0,
	RED,
	GREEN,
	BLUE
} mode_t;

// ================= GLOBAIS =================
volatile uint8_t red, green, blue;
volatile mode_t mode = NONE;

volatile uint16_t hold_up = 0;
volatile uint16_t hold_down = 0;
volatile uint8_t m_pressed = 0;

volatile uint8_t flag_update_eeprom = 0;
volatile uint8_t flag_update_lcd = 1;

// ================= EEPROM =================
uint8_t EEMEM e_red;
uint8_t EEMEM e_green;
uint8_t EEMEM e_blue;

// ================= LCD (PORTD: D2 a D7) =================
#define RS PD2
#define EN PD3

void lcd_pulse() {
	PORTD |= (1<<EN);
	_delay_us(2);
	PORTD &= ~(1<<EN);
	_delay_us(100);
}

void lcd_send4(uint8_t nibble) {
	// Injeta os 4 bits (de 0x00 a 0x0F) nos pinos D4 a D7
	PORTD = (PORTD & 0x0F) | ((nibble & 0x0F) << 4);
	_delay_us(1); // Tempo crucial para o sinal estabilizar (Setup Time)
	lcd_pulse();
}

void lcd_cmd(uint8_t cmd) {
	PORTD &= ~(1<<RS);
	_delay_us(1);
	lcd_send4(cmd >> 4);   // Envia os 4 bits mais significativos
	lcd_send4(cmd & 0x0F); // Envia os 4 bits menos significativos
	_delay_ms(2);
}

void lcd_data(uint8_t data) {
	PORTD |= (1<<RS);
	_delay_us(1);
	lcd_send4(data >> 4);
	lcd_send4(data & 0x0F);
	_delay_us(50);
}

void lcd_init() {
	DDRD |= 0xFC;  // Configura PD2 a PD7 como saída
	_delay_ms(50); // Tempo para o display ligar (VCC subir)
	
	PORTD &= ~(1<<RS);
	PORTD &= ~(1<<EN);
	
	// Sequência oficial de reset do HD44780
	lcd_send4(0x03); _delay_ms(5);
	lcd_send4(0x03); _delay_ms(1);
	lcd_send4(0x03); _delay_ms(1);
	
	lcd_send4(0x02); // Entra em modo 4 bits
	_delay_ms(2);
	
	lcd_cmd(0x28); // 2 linhas, matriz 5x8
	lcd_cmd(0x08); // Desliga display
	lcd_cmd(0x01); // Limpa display
	_delay_ms(5);
	lcd_cmd(0x06); // Incrementa cursor
	lcd_cmd(0x0C); // Liga display, cursor off
}

void lcd_set(uint8_t l, uint8_t c) {
	lcd_cmd((l == 0 ? 0x80 : 0xC0) + c);
}

void lcd_print(const char *s) {
	while(*s) lcd_data(*s++);
}

// ================= FORMATAÇÃO (Otimizada para RAM) =================
void lcd_print_number(uint8_t val, uint8_t is_selected) {
	char str[5];
	uint8_t pos = 0;
	
	// Formata o valor com espaços em branco à esquerda se necessário
	if (val >= 100) {
		str[pos++] = '0' + (val / 100);
		str[pos++] = '0' + ((val / 10) % 10);
		str[pos++] = '0' + (val % 10);
		} else if (val >= 10) {
		str[pos++] = ' ';
		str[pos++] = '0' + (val / 10);
		str[pos++] = '0' + (val % 10);
		} else {
		str[pos++] = ' ';
		str[pos++] = ' ';
		str[pos++] = '0' + val;
	}
	
	str[pos++] = is_selected ? '*' : ' ';
	str[pos] = '\0';
	
	lcd_print(str);
}

// ================= PWM (LED RGB) =================
void pwm_init() {
	DDRB |= (1<<PB1) | (1<<PB2) | (1<<PB3);
	
	TCCR1A = (1<<COM1A1) | (1<<COM1B1) | (1<<WGM10);
	TCCR1B = (1<<WGM12) | (1<<CS11);
	
	TCCR2A = (1<<COM2A1) | (1<<WGM21) | (1<<WGM20);
	TCCR2B = (1<<CS21);
}

// ================= TIMER0 =================
void timer_init() {
	TCCR0A = 0;
	TCCR0B = (1<<CS02); // Prescaler 256
	TIMSK0 |= (1<<TOIE0);
}

// ================= BOTÕES =================
void buttons_init() {
	DDRC &= ~((1<<PC1) | (1<<PC2) | (1<<PC3));
}

// ================= EEPROM =================
void load_eeprom() {
	red = eeprom_read_byte(&e_red);
	green = eeprom_read_byte(&e_green);
	blue = eeprom_read_byte(&e_blue);
	
	if(red == 0xFF) red = 0;
	if(green == 0xFF) green = 0;
	if(blue == 0xFF) blue = 0;
}

void save_eeprom() {
	eeprom_update_byte(&e_red, red);
	eeprom_update_byte(&e_green, green);
	eeprom_update_byte(&e_blue, blue);
}

// ================= INTERRUPÇÃO =================
ISR(TIMER0_OVF_vect) {
	// Botão M
	if (!(PINC & (1<<PC1))) {
		if (!m_pressed) {
			m_pressed = 1;
			mode++;
			if (mode > BLUE) mode = NONE;
			flag_update_lcd = 1;
		}
		} else {
		m_pressed = 0;
	}

	// Botão UP
	if (!(PINC & (1<<PC2))) {
		hold_up++;
		uint8_t step = (hold_up > 1220) ? 5 : 1;
		
		if (hold_up % 12 == 0) {
			if (mode == RED && red <= 255 - step) red += step;
			if (mode == GREEN && green <= 255 - step) green += step;
			if (mode == BLUE && blue <= 255 - step) blue += step;
			flag_update_eeprom = 1;
			flag_update_lcd = 1;
		}
		} else {
		hold_up = 0;
	}

	// Botão DOWN
	if (!(PINC & (1<<PC3))) {
		hold_down++;
		uint8_t step = (hold_down > 1220) ? 5 : 1;
		
		if (hold_down % 12 == 0) {
			if (mode == RED && red >= step) red -= step;
			if (mode == GREEN && green >= step) green -= step;
			if (mode == BLUE && blue >= step) blue -= step;
			flag_update_eeprom = 1;
			flag_update_lcd = 1;
		}
		} else {
		hold_down = 0;
	}
}

// ================= MAIN =================
int main() {
	pwm_init();
	lcd_init();
	buttons_init();
	timer_init();
	load_eeprom();

	sei();

	_delay_ms(5); // Pausa estratégica antes de escrever no LCD pela 1ª vez
	
	lcd_set(0, 0);
	lcd_print(" RED GREEN BLUE "); // Textos reescritos do zero (sem espaços "fantasmas")

	while (1) {
		OCR1A = blue;
		OCR1B = green;
		OCR2A = red;

		if (flag_update_lcd) {
			lcd_set(1, 0);
			
			lcd_print_number(red, mode == RED);
			lcd_print(" ");
			
			lcd_print_number(green, mode == GREEN);
			lcd_print("  ");
			
			lcd_print_number(blue, mode == BLUE);
			
			flag_update_lcd = 0;
		}

		if (flag_update_eeprom && hold_up == 0 && hold_down == 0) {
			save_eeprom();
			flag_update_eeprom = 0;
		}
	}
}
