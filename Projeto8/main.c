/*
 * Projeto 08 - Mini Gerador de Sinais (MGS)
 * Disciplina: ELE3717 - Sistemas Microcontrolados
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/************************************************************************/
/* DEFINICOES                                                           */
/************************************************************************/

#define LCD_RS PD2
#define LCD_EN PD3
#define LCD_D4 PD4
#define LCD_D5 PD5
#define LCD_D6 PD6
#define LCD_D7 PD7

#define BTN_A     PC0
#define BTN_M     PC1
#define BTN_UP    PC2
#define BTN_DOWN  PC3

#define WAVE_SQUARE 0
#define WAVE_TRI    1
#define WAVE_RAMP   2
#define WAVE_SINE   3

#define MENU_WAVE   0
#define MENU_FREQ   1
#define MENU_AMP    2
#define MENU_OFFSET 3
#define MENU_DUTY   4

/************************************************************************/
/* ESTRUTURA DE CONFIGURACAO                                            */
/************************************************************************/

typedef struct
{
    uint8_t waveform;
    uint8_t frequency;
    uint8_t duty;
    uint8_t amplitude; // Armazenado como (V * 10). Ex: 25 = 2.5V
    uint8_t offset;    // Armazenado como (V * 10). Ex: 25 = 2.5V
} CONFIG_t;

/************************************************************************/
/* EEPROM                                                               */
/************************************************************************/

CONFIG_t EEMEM ee_cfg;

/************************************************************************/
/* VARIAVEIS GLOBAIS                                                    */
/************************************************************************/

volatile CONFIG_t cfg;
volatile uint8_t output_enable = 0; // Sempre inicia em OFF
volatile uint8_t menu = MENU_WAVE;
volatile uint32_t millis_counter = 0;

// DDS de 16 bits para alta precis o de frequ ncia
volatile uint16_t phase_accumulator = 0;
volatile uint16_t phase_step = 0;

// Buffer com a forma de onda atualizada (Evita float na ISR)
volatile uint8_t wave_buffer[256];

// Tabela base para a senoide
uint8_t sine_table[256];

// Estado dos bot es
volatile uint8_t last_A = 0;
volatile uint8_t last_M = 0;
volatile uint32_t up_hold_time = 0;
volatile uint32_t down_hold_time = 0;
volatile uint16_t up_repeat_time = 0;
volatile uint16_t down_repeat_time = 0;

/************************************************************************/
/* PROTOTIPOS                                                           */
/************************************************************************/
void LCD_Init(void);
void LCD_Command(uint8_t cmd);
void LCD_Data(uint8_t data);
void LCD_String(char *str);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Clear(void);
void LCD_Update(void);

void DAC_Init(void);
void DAC_Write(uint8_t value);

void LoadDefault(void);
void LoadConfig(void);
void SaveConfig(void);

void Buttons_Init(void);
void Buttons_Process(void);
void ButtonEvents(void);
void AutoRepeatButtons(void);

void GenerateSineTable(void);
void UpdateWaveformBuffer(void);
void UpdatePhaseStep(void);

void Timer1_Init(void);
void Timer2_Init(void);

/************************************************************************/
/* DAC R-2R CORRIGIDO                                                   */
/************************************************************************/

void DAC_Init(void)
{
    // Configura os pinos do DAC como sa da conforme o esquem tico
    DDRB |= (1<<PB0) | (1<<PB1) | (1<<PB2) | (1<<PB3) | (1<<PB4) | (1<<PB5);
    DDRC |= (1<<PC4) | (1<<PC5);
}

void DAC_Write(uint8_t value)
{
    // Bit 0 -> DA0 -> PC4 (Corrigido conforme o esquem tico)[cite: 1]
    if(value & (1<<0)) PORTC |= (1<<PC4);
    else               PORTC &= ~(1<<PC4);

    // Bit 1 -> DA1 -> PC5 (Corrigido conforme o esquem tico)[cite: 1]
    if(value & (1<<1)) PORTC |= (1<<PC5);
    else               PORTC &= ~(1<<PC5);

    // Bits 2 a 7 -> DA2 a DA7 -> PB0 a PB5[cite: 1]
    if(value & (1<<2)) PORTB |= (1<<PB0);
    else               PORTB &= ~(1<<PB0);

    if(value & (1<<3)) PORTB |= (1<<PB1);
    else               PORTB &= ~(1<<PB1);

    if(value & (1<<4)) PORTB |= (1<<PB2);
    else               PORTB &= ~(1<<PB2);

    if(value & (1<<5)) PORTB |= (1<<PB3);
    else               PORTB &= ~(1<<PB3);

    if(value & (1<<6)) PORTB |= (1<<PB4);
    else               PORTB &= ~(1<<PB4);

    if(value & (1<<7)) PORTB |= (1<<PB5);
    else               PORTB &= ~(1<<PB5);
}

/************************************************************************/
/* LCD COM FORMATACAO EXATA DO ENUNCIADO                                */
/************************************************************************/

void LCD_PulseEnable(void)
{
    PORTD |= (1<<LCD_EN);
    _delay_us(2);
    PORTD &= ~(1<<LCD_EN);
    _delay_us(100);
}

void LCD_SendNibble(uint8_t nibble)
{
    if(nibble & 0x01) PORTD |= (1<<LCD_D4); else PORTD &= ~(1<<LCD_D4);
    if(nibble & 0x02) PORTD |= (1<<LCD_D5); else PORTD &= ~(1<<LCD_D5);
    if(nibble & 0x04) PORTD |= (1<<LCD_D6); else PORTD &= ~(1<<LCD_D6);
    if(nibble & 0x08) PORTD |= (1<<LCD_D7); else PORTD &= ~(1<<LCD_D7);
    LCD_PulseEnable();
}

void LCD_Command(uint8_t cmd)
{
    PORTD &= ~(1<<LCD_RS);
    LCD_SendNibble(cmd >> 4);
    LCD_SendNibble(cmd & 0x0F);
    _delay_ms(2);
}

void LCD_Data(uint8_t data)
{
    PORTD |= (1<<LCD_RS);
    LCD_SendNibble(data >> 4);
    LCD_SendNibble(data & 0x0F);
    _delay_us(100);
}

void LCD_Clear(void)
{
    LCD_Command(0x01);
    _delay_ms(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? (0x00 + col) : (0x40 + col);
    LCD_Command(0x80 | addr);
}

void LCD_String(char *str)
{
    while(*str) LCD_Data(*str++);
}

void LCD_Init(void)
{
    DDRD |= (1<<LCD_RS) | (1<<LCD_EN) | (1<<LCD_D4) | (1<<LCD_D5) | (1<<LCD_D6) | (1<<LCD_D7);
    _delay_ms(50);
    PORTD &= ~(1<<LCD_RS);

    LCD_SendNibble(0x03); _delay_ms(5);
    LCD_SendNibble(0x03); _delay_ms(5);
    LCD_SendNibble(0x03); _delay_ms(5);
    LCD_SendNibble(0x02);

    LCD_Command(0x28);
    LCD_Command(0x0C);
    LCD_Command(0x06);
    LCD_Clear();
}

void LCD_Update(void)
{
    char linha1[17];
    char linha2[17];
    char nome_onda[4];

    switch(cfg.waveform)
    {
        case WAVE_SQUARE: sprintf(nome_onda, "QUA"); break;
        case WAVE_TRI:    sprintf(nome_onda, "TRI"); break;
        case WAVE_RAMP:   sprintf(nome_onda, "RAM"); break;
        default:          sprintf(nome_onda, "SEN"); break;
    }

    // Formata  o da Linha 1 id ntica   imagem fornecida[cite: 1]
    if(cfg.waveform == WAVE_SQUARE || cfg.waveform == WAVE_TRI)
    {
        sprintf(linha1, "T:%s D:%02d%%  %s", nome_onda, cfg.duty, output_enable ? "ON " : "OFF");
    }
    else
    {
        sprintf(linha1, "T:%s       %s", nome_onda, output_enable ? "ON " : "OFF");
    }

    // Formata  o da Linha 2 com v rgula decimal e sufixo 'V'[cite: 1]
    sprintf(linha2, "%03dHz %d,%dV %d,%dV",
            cfg.frequency,
            cfg.amplitude / 10, cfg.amplitude % 10,
            cfg.offset / 10, cfg.offset % 10);

    LCD_SetCursor(0,0);
    LCD_String(linha1);
    LCD_SetCursor(1,0);
    LCD_String(linha2);
}

/************************************************************************/
/* PRE-PROCESSAMENTO DA ONDA (RODA FORA DA ISR)                         */
/************************************************************************/

void UpdateWaveformBuffer(void)
{
    float amp_f = cfg.amplitude / 10.0;
    float offset_f = cfg.offset / 10.0;
    uint16_t duty_limit = (uint32_t)cfg.duty * 256UL / 100UL;

    for (uint16_t i = 0; i < 256; i++)
    {
        float raw_sample = 0;

        switch (cfg.waveform)
        {
            case WAVE_SQUARE:
                raw_sample = (i < duty_limit) ? 1.0 : -1.0;
                break;

            case WAVE_TRI:
                if (duty_limit == 0) duty_limit = 1;
                if (duty_limit == 256) duty_limit = 255;
                if (i < duty_limit) {
                    raw_sample = -1.0 + 2.0 * ((float)i / duty_limit);
                } else {
                    raw_sample = 1.0 - 2.0 * ((float)(i - duty_limit) / (256 - duty_limit));
                }
                break;

            case WAVE_RAMP:
                raw_sample = -1.0 + 2.0 * ((float)i / 255.0);
                break;

            case WAVE_SINE:
                raw_sample = -1.0 + 2.0 * ((float)sine_table[i] / 255.0);
                break;
        }

        // Aplica Amplitude Pico a Pico e Offset Vpp / 2
        float voltage = offset_f + (raw_sample * (amp_f / 2.0));

        // Satura  o r gida por seguran a entre 0V e 5V[cite: 1]
        if (voltage < 0.0) voltage = 0.0;
        if (voltage > 5.0) voltage = 5.0;

        wave_buffer[i] = (uint8_t)((voltage / 5.0) * 255.0);
    }
}

void UpdatePhaseStep(void)
{
    // F_amostragem = 16000 Hz. Resolu  o DDS = 65536 passos (16 bits)
    // Passo = (F_desejada * 65536) / 16000 => F_desejada * 4.096
    phase_step = (uint16_t)(((uint32_t)cfg.frequency * 65536UL) / 16000UL);
}

void GenerateSineTable(void)
{
    for(uint16_t i=0; i<256; i++)
    {
        double angle = (2.0 * M_PI * i) / 256.0;
        sine_table[i] = (uint8_t)(127.5 + 127.5 * sin(angle));
    }
}

/************************************************************************/
/* EEPROM                                                               */
/************************************************************************/

void LoadDefault(void)
{
    cfg.waveform  = WAVE_SQUARE;
    cfg.frequency = 100;
    cfg.duty      = 50;
    cfg.amplitude = 20; // 2.0 Vpp por padr o de f brica[cite: 1]
    cfg.offset    = 25; // 2.5 V de offset por padr o[cite: 1]
}

void SaveConfig(void)
{
    eeprom_update_block((const void*)&cfg, (void*)&ee_cfg, sizeof(CONFIG_t));
}

void LoadConfig(void)
{
    eeprom_read_block((void*)&cfg, (const void*)&ee_cfg, sizeof(CONFIG_t));
    if(cfg.frequency < 1 || cfg.frequency > 100 || cfg.duty < 1 || cfg.duty > 99)
    {
        LoadDefault();
        SaveConfig();
    }
}

/************************************************************************/
/* BOTOES                                                               */
/************************************************************************/

void Buttons_Init(void)
{
    DDRC &= ~((1<<BTN_A) | (1<<BTN_M) | (1<<BTN_UP) | (1<<BTN_DOWN));
    PORTC |= (1<<BTN_A) | (1<<BTN_M) | (1<<BTN_UP) | (1<<BTN_DOWN); // Pull-ups ativos[cite: 1]
}

uint8_t Read_A(void)    { return !(PINC & (1<<BTN_A)); }
uint8_t Read_M(void)    { return !(PINC & (1<<BTN_M)); }
uint8_t Read_UP(void)   { return !(PINC & (1<<BTN_UP)); }
uint8_t Read_DOWN(void) { return !(PINC & (1<<BTN_DOWN)); }

void IncrementParameter(void)
{
    switch(menu)
    {
        case MENU_WAVE:   cfg.waveform = (cfg.waveform + 1) > WAVE_SINE ? WAVE_SQUARE : cfg.waveform + 1; break;
        case MENU_FREQ:   if(cfg.frequency < 100) cfg.frequency++; break;
        case MENU_AMP:    if(cfg.amplitude < 50)  cfg.amplitude++; break;
        case MENU_OFFSET: if(cfg.offset < 50)     cfg.offset++; break;
        case MENU_DUTY:   if(cfg.duty < 99)       cfg.duty++; break;
    }
    UpdateWaveformBuffer();
    UpdatePhaseStep();
    SaveConfig();
}

void DecrementParameter(void)
{
    switch(menu)
    {
        case MENU_WAVE:   cfg.waveform = (cfg.waveform == WAVE_SQUARE) ? WAVE_SINE : cfg.waveform - 1; break;
        case MENU_FREQ:   if(cfg.frequency > 1)   cfg.frequency--; break;
        case MENU_AMP:    if(cfg.amplitude > 0)   cfg.amplitude--; break;
        case MENU_OFFSET: if(cfg.offset > 0)      cfg.offset--; break;
        case MENU_DUTY:   if(cfg.duty > 1)        cfg.duty--; break;
    }
    UpdateWaveformBuffer();
    UpdatePhaseStep();
    SaveConfig();
}

void Buttons_Process(void)
{
    if(Read_A() && !last_A)
    {
        menu++;
        if(menu > MENU_DUTY) menu = MENU_WAVE;
    }
    last_A = Read_A();

    if(Read_M() && !last_M)
    {
        output_enable ^= 1;
    }
    last_M = Read_M();

    ButtonEvents();
}

void ButtonEvents(void)
{
    static uint8_t old_up = 0;
    static uint8_t old_down = 0;

    uint8_t up = Read_UP();
    uint8_t down = Read_DOWN();

    if(up && !old_up)     IncrementParameter();
    if(down && !old_down) DecrementParameter();

    old_up = up;
    old_down = down;
}

void AutoRepeatButtons(void)
{
    // UP Auto Repeat: 1Hz (1000ms) inicialmente, passa para 10Hz (100ms) ap s 5 segundos[cite: 1]
    if(Read_UP())
    {
        up_hold_time++;
        uint16_t target_delay = (up_hold_time < 5000) ? 1000 : 100; // 1Hz ou 10Hz[cite: 1]
        if(++up_repeat_time >= target_delay)
        {
            up_repeat_time = 0;
            IncrementParameter();
        }
    }
    else
    {
        up_hold_time = 0;
        up_repeat_time = 0;
    }

    // DOWN Auto Repeat[cite: 1]
    if(Read_DOWN())
    {
        down_hold_time++;
        uint16_t target_delay = (down_hold_time < 5000) ? 1000 : 100;
        if(++down_repeat_time >= target_delay)
        {
            down_repeat_time = 0;
            DecrementParameter();
        }
    }
    else
    {
        down_hold_time = 0;
        down_repeat_time = 0;
    }
}

/************************************************************************/
/* TIMER 1 - DDS SUPER VELOZ E SEM FLOATS                               */
/************************************************************************/

void Timer1_Init(void)
{
    TCCR1A = 0;
    TCCR1B = 0;
    TCCR1B |= (1<<WGM12);  // Modo CTC
    OCR1A = 124;           // F_amostragem = 16MHz / (8 * (1 + 124)) = 16000 Hz
    TIMSK1 |= (1<<OCIE1A); // Ativa Interrup  o
    TCCR1B |= (1<<CS11);   // Prescaler = 8
}

ISR(TIMER1_COMPA_vect)
{
    // Acumulador de fase de 16 bits
    phase_accumulator += phase_step;

    if(output_enable)
    {
        // Pega os 8 bits superiores como  ndice da tabela (DDS cl ssico)
        uint8_t index = (phase_accumulator >> 8) & 0xFF;
        DAC_Write(wave_buffer[index]);
    }
    else
    {
        DAC_Write(0);
    }
}

/************************************************************************/
/* TIMER 2 - BASE DE TEMPO (1 ms)                                       */
/************************************************************************/

void Timer2_Init(void)
{
    TCCR2A = 0;
    TCCR2B = 0;
    TCCR2A |= (1<<WGM21);  // Modo CTC
    OCR2A = 249;           // Base de tempo de 1ms
    TIMSK2 |= (1<<OCIE2A);
    TCCR2B |= (1<<CS22);   // Prescaler = 64
}

ISR(TIMER2_COMPA_vect)
{
    millis_counter++;
}

/************************************************************************/
/* MAIN LOOP                                                            */
/************************************************************************/

int main(void)
{
    uint32_t last_lcd_update = 0;
    uint32_t last_button_scan = 0;

    LCD_Init();
    DAC_Init();
    Buttons_Init();
    GenerateSineTable();
    LoadConfig();
    
    // For a a primeira carga dos buffers em RAM
    UpdateWaveformBuffer();
    UpdatePhaseStep();

    Timer1_Init();
    Timer2_Init();
    sei();

    LCD_Update();

    while(1)
    {
        // Varredura dos bot es a cada 1ms
        if((millis_counter - last_button_scan) >= 1)
        {
            last_button_scan = millis_counter;
            Buttons_Process();
            AutoRepeatButtons();
        }

        // Atualiza  o visual a cada 200ms
        if((millis_counter - last_lcd_update) >= 200)
        {
            last_lcd_update = millis_counter;
            LCD_Update();
        }
    }
}
