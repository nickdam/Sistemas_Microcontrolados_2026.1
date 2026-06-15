/*
 * Atvd05.c
 *
 * Created: 18/05/2026 17:55:43
 * Author : Nicolas
 */ 

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdio.h>

// ===================== DEFINIÇÕES =====================
#define MAX_LEN 8

#define UP    0
#define DOWN  1
#define LEFT  2
#define RIGHT 3

// MAX7219
#define CS PB2
#define MOSI PB3
#define SCK PB5

// LCD (ajuste conforme seu hardware)
#define RS PD2
#define EN PD3
#define D4 PD4
#define D5 PD5
#define D6 PD6
#define D7 PD7

// ===================== VARIÁVEIS =====================
uint8_t snake_x[MAX_LEN];
uint8_t snake_y[MAX_LEN];
uint8_t length = 1;

uint8_t dir = RIGHT;

//uint16_t delay_ms = 500;
uint16_t tempo_variavel =500;
uint32_t tempo_total = 0;

// ===================== SPI / MAX7219 =====================
void SPI_init() {
    DDRB |= (1<<MOSI) | (1<<SCK) | (1<<CS);
    SPCR = (1<<SPE) | (1<<MSTR) | (1<<SPR0);
}

void MAX7219_send(uint8_t reg, uint8_t data) {
    PORTB &= ~(1<<CS);
    SPDR = reg;
    while(!(SPSR & (1<<SPIF)));
    SPDR = data;
    while(!(SPSR & (1<<SPIF)));
    PORTB |= (1<<CS);
}

void MAX7219_init() {
    MAX7219_send(0x0C, 0x01);
    MAX7219_send(0x0B, 0x07);
    MAX7219_send(0x0A, 0x08);
    MAX7219_send(0x09, 0x00);
    MAX7219_send(0x0F, 0x00);
}

// ===================== LCD =====================
void lcd_pulse() {
    PORTD |= (1<<EN);
    _delay_us(2);
    PORTD &= ~(1<<EN);
	_delay_us(100);
}

void lcd_send4(uint8_t data) {
    PORTD = (PORTD & 0x0F) | ((data & 0x0F) << 4);
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
    DDRD |= 0xFC;
    _delay_ms(50);
	
	PORTD &= ~(1<<RS);
	PORTD &= ~(1<<EN);
	
    /*lcd_cmd(0x28);
    lcd_cmd(0x0C);
    lcd_cmd(0x06);
    lcd_cmd(0x01);*/
	// Sequ?ncia oficial de reset do HD44780
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

void lcd_print(char *str) {
    while(*str) lcd_data(*str++);
}

// ===================== ADC =====================
void ADC_init() {
    ADMUX = (1<<REFS0);
    ADCSRA = (1<<ADEN) | (1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0);
}

uint16_t ADC_read(uint8_t ch) {
    ADMUX = (ADMUX & 0xF0) | ch;
    ADCSRA |= (1<<ADSC);
    while(ADCSRA & (1<<ADSC));
    return ADC;
}

// ===================== JOGO =====================
void clear_matrix() {
    for(uint8_t i=1;i<=8;i++)
        MAX7219_send(i, 0x00);
}

void draw() {
    clear_matrix();
	
	uint8_t buffer_linhas[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for(uint8_t i=0;i<length;i++) {
		buffer_linhas[snake_y[i]] |= (1<< snake_x[i]);
        //uint8_t row = snake_y[i] + 1;
        //uint8_t col = snake_x[i];
	}
	for(uint8_t i=0; i<8; i++){
		if(buffer_linhas[i] != 0){
        MAX7219_send(i+1, buffer_linhas[i]);
		}
    }
}

void reset_game() {
    length = 1;
    snake_x[0] = 3;
    snake_y[0] = 3;
    dir = RIGHT;
    tempo_variavel = 500;
    tempo_total = 0;

    // animação simples
    for(uint8_t i=1;i<=8;i++) {
        MAX7219_send(i, 0xFF);
        _delay_ms(50);
    }
    clear_matrix();
}

void update_direction() {
    uint16_t y = ADC_read(4);
    uint16_t x = ADC_read(5);

    if(x > 800) dir = RIGHT;
    else if(x < 200) dir = LEFT;
    else if(y > 800) dir = UP;
    else if(y < 200) dir = DOWN;
}

uint8_t check_collision(uint8_t nx, uint8_t ny) {
    if(nx > 7 || ny > 7) return 1;

    for(uint8_t i=0;i<length;i++)
        if(snake_x[i]==nx && snake_y[i]==ny)
            return 1;

    return 0;
}

void move_snake() {
    uint8_t nx = snake_x[0];
    uint8_t ny = snake_y[0];

    if(dir == RIGHT) nx++;
    if(dir == LEFT)  nx--;
    if(dir == UP)    ny--;
    if(dir == DOWN)  ny++;

    if(check_collision(nx, ny)) {
        reset_game();
        return;
    }

    for(int i=length-1;i>0;i--) {
        snake_x[i] = snake_x[i-1];
        snake_y[i] = snake_y[i-1];
    }

    snake_x[0] = nx;
    snake_y[0] = ny;
}

// ===================== LCD DEBUG =====================
void lcd_show_positions() {
    char buffer[2];

    lcd_cmd(0x80);
	lcd_print("X:");
    for(uint8_t i=0;i<7;i++) {
        if(i < length)sprintf(buffer,"%d",snake_x[i]);
		else sprintf(buffer, "0");
        lcd_print(buffer);
    }

    lcd_cmd(0xC0);
	lcd_print("Y:");
    for(uint8_t i=0;i<7;i++) {
        if(i < length) sprintf(buffer,"%d",snake_y[i]);
		else sprintf(buffer, "0");
        lcd_print(buffer);
    }
}

// ===================== MAIN =====================
int main() {
    SPI_init();
    MAX7219_init();
    ADC_init();
    lcd_init();

    reset_game();

    while(1) {
        //update_direction();
        move_snake();
        draw();
        lcd_show_positions();

        for(int i =0; i < tempo_variavel; i++){
			update_direction();
			_delay_ms(1);
		}

        tempo_total += tempo_variavel;

        // aumento de dificuldade
        if(tempo_total >= 60000) {
            tempo_total = 0;

            if(tempo_variavel > 100){
                tempo_variavel -= 50;
			}

            if(length < MAX_LEN){
                length++;
			}
        }
    }
}

