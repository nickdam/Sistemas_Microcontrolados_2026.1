#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <math.h>
#include <util/delay.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// --- FUNÇÃO DO SISTEMA OPERACIONAL ---
void vApplicationIdleHook(void) {
	// Mantida vazia para o FreeRTOS compilar corretamente
}

// --- MAPEAMENTO DE HARDWARE ---
#define BUTTON_PORT PORTC
#define BUTTON_PIN  PINC
#define S0 PC0
#define S1 PC1
#define S2 PC2
#define S3 PC3

#define RGB_PORT PORTB
#define LED_R   PB3  // D11
#define LED_G   PB2  // D10
#define LED_B   PB1  // D9

#define LCD_PORT PORTD
#define LCD_RS   PD2
#define LCD_EN   PD3
#define LCD_D4   PD4
#define LCD_D5   PD5
#define LCD_D6   PD6
#define LCD_D7   PD7

// --- VARIÁVEIS GLOBAIS E ESTRUTURAS ---
typedef enum { TELA_INIT, TELA_ACC, TELA_GIR, TELA_EULER } TelaAtiva;
volatile TelaAtiva tela_atual = TELA_INIT;

typedef struct {
	int16_t ax, ay, az;
	int16_t gx, gy, gz;
	float roll, pitch, yaw;
	float temp;
} DadosSensor;

QueueHandle_t xQueueDados;

// --- PROTÓTIPOS DAS FUNÇÕES ---
void UART_init(uint16_t ubrr);
void UART_send_string(const char* str);
void I2C_init(void);
void MPU6050_init(void);
void MPU6050_read(DadosSensor *dados);

void LCD_init(void);
void LCD_Command(uint8_t cmd);
void LCD_Write(uint8_t data);
void LCD_Print(const char* str);
void LCD_set_cursor(uint8_t row, uint8_t col);
void LCD_SendNibble(uint8_t nibble);
void LCD_PulseEnable(void);
void LCD_Clear(void);

void task_sensores(void *pvParameters);
void task_interface(void *pvParameters);
void task_botoes(void *pvParameters);

// --- MAIN ---
int main(void) {
	DDRB |= (1 << LED_R) | (1 << LED_G) | (1 << LED_B);
	RGB_PORT |= (1 << LED_R) | (1 << LED_G) | (1 << LED_B);

	DDRC &= ~((1 << S0) | (1 << S1) | (1 << S2) | (1 << S3));

	UART_init(103); // 9600 bps
	I2C_init();
	MPU6050_init();
	LCD_init();

	xQueueDados = xQueueCreate(1, sizeof(DadosSensor));

	if (xQueueDados != NULL) {
		xTaskCreate(task_sensores,  "SENSORES",  256, NULL, 3, NULL);
		xTaskCreate(task_interface, "INTERFACE", 256, NULL, 2, NULL);
		xTaskCreate(task_botoes,    "BOTOES",    128, NULL, 1, NULL);

		vTaskStartScheduler();
	}

	while (1);
	return 0;
}

// --- TASK: LEITURA DOS SENSORES ---
void task_sensores(void *pvParameters) {
	DadosSensor dados;
	TickType_t xLastWakeTime = xTaskGetTickCount();
	const TickType_t xPeriodo = pdMS_TO_TICKS(200);

	while (1) {
		MPU6050_read(&dados);
		
		dados.roll  = atan2f((float)dados.ay, (float)dados.az) * 57.29578f;
		dados.pitch = atan2f(-(float)dados.ax, sqrtf((float)dados.ay * dados.ay + (float)dados.az * dados.az)) * 57.29578f;
		dados.yaw   = 0;

		xQueueOverwrite(xQueueDados, &dados);

		char tx_buffer[128];
		sprintf(tx_buffer, "[acc(g): %d; %d; %d; gir(d/s): %d; %d; %d; eul(d): %.0f; %.0f; %.0f; temp: %.1f]\r\n",
		dados.ax/16384, dados.ay/16384, dados.az/16384,
		dados.gx/131,   dados.gy/131,   dados.gz/131,
		dados.roll, dados.pitch, dados.yaw, dados.temp);
		UART_send_string(tx_buffer);

		float mod_euler = sqrtf(dados.roll * dados.roll + dados.pitch * dados.pitch);
		if (mod_euler < 5.0f) {
			RGB_PORT |= (1 << LED_R) | (1 << LED_G) | (1 << LED_B);
			} else if (mod_euler >= 5.0f && mod_euler < 25.0f) {
			RGB_PORT &= ~(1 << LED_G);
			RGB_PORT |= (1 << LED_R) | (1 << LED_B);
			} else {
			RGB_PORT &= ~(1 << LED_R);
			RGB_PORT |= (1 << LED_G) | (1 << LED_B);
		}

		vTaskDelayUntil(&xLastWakeTime, xPeriodo);
	}
}

// --- TASK: INTERFACE IHM (LCD) ---
void task_interface(void *pvParameters) {
	DadosSensor dados_lcd;
	char buffer_lin1[17];
	char buffer_lin2[17];
	
	LCD_set_cursor(0, 0);
	LCD_Print("MIA HC-05 INIT ");
	LCD_set_cursor(1, 0);
	LCD_Print("Addr: 98:D3:XX ");
	vTaskDelay(pdMS_TO_TICKS(10000)); // 10s obrigatórios

	tela_atual = TELA_EULER;

	while (1) {
		if (xQueueReceive(xQueueDados, &dados_lcd, pdMS_TO_TICKS(100)) == pdTRUE) {
			LCD_Clear();
			
			switch (tela_atual) {
				case TELA_ACC:
				sprintf(buffer_lin1, "x(g)  y(g)  z(g)");
				sprintf(buffer_lin2, "%4d  %4d  %4d", dados_lcd.ax/16384, dados_lcd.ay/16384, dados_lcd.az/16384);
				break;
				case TELA_GIR:
				sprintf(buffer_lin1, "x(d)  y(d)  z(d)");
				sprintf(buffer_lin2, "%4d  %4d  %4d", dados_lcd.gx/131, dados_lcd.gy/131, dados_lcd.gz/131);
				break;
				case TELA_EULER:
				default:
				sprintf(buffer_lin1, "x    y    z  temp");
				sprintf(buffer_lin2, "%3.0f  %3.0f  %3.0f %2.1f", dados_lcd.roll, dados_lcd.pitch, dados_lcd.yaw, dados_lcd.temp);
				break;
			}
			LCD_set_cursor(0, 0);
			LCD_Print(buffer_lin1);
			LCD_set_cursor(1, 0);
			LCD_Print(buffer_lin2);
		}
		vTaskDelay(pdMS_TO_TICKS(250));
	}
}

// --- TASK: BOTÕES ---
void task_botoes(void *pvParameters) {
	uint8_t travou_s1 = 0, travou_s2 = 0, travou_s3 = 0;

	while (1) {
		if (!(BUTTON_PIN & (1 << S1))) {
			if (!travou_s1) { tela_atual = TELA_ACC; travou_s1 = 1; }
			} else { travou_s1 = 0; }

			if (!(BUTTON_PIN & (1 << S2))) {
				if (!travou_s2) { tela_atual = TELA_GIR; travou_s2 = 1; }
				} else { travou_s2 = 0; }

				if (!(BUTTON_PIN & (1 << S3))) {
					if (!travou_s3) { tela_atual = TELA_EULER; travou_s3 = 1; }
					} else { travou_s3 = 0; }

					vTaskDelay(pdMS_TO_TICKS(50));
				}
			}

			// --- IMPLEMENTAÇÃO DAS DRIVERS FÍSICAS DO LCD ---
			void LCD_init(void) {
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

			void LCD_PulseEnable(void) {
				PORTD |= (1<<LCD_EN);  _delay_us(2);
				PORTD &= ~(1<<LCD_EN); _delay_us(100);
			}

			void LCD_SendNibble(uint8_t nibble) {
				if(nibble & 0x01) PORTD |= (1<<LCD_D4); else PORTD &= ~(1<<LCD_D4);
				if(nibble & 0x02) PORTD |= (1<<LCD_D5); else PORTD &= ~(1<<LCD_D5);
				if(nibble & 0x04) PORTD |= (1<<LCD_D6); else PORTD &= ~(1<<LCD_D6);
				if(nibble & 0x08) PORTD |= (1<<LCD_D7); else PORTD &= ~(1<<LCD_D7);
				LCD_PulseEnable();
			}

			void LCD_Command(uint8_t cmd) {
				PORTD &= ~(1<<LCD_RS);
				LCD_SendNibble(cmd >> 4);
				LCD_SendNibble(cmd & 0x0F);
				_delay_ms(2);
			}

			void LCD_Write(uint8_t data) {
				PORTD |= (1<<LCD_RS);
				LCD_SendNibble(data >> 4);
				LCD_SendNibble(data & 0x0F);
				_delay_us(100);
			}

			void LCD_Print(const char* str) {
				while (*str) LCD_Write((uint8_t)*str++);
			}

			void LCD_Clear(void) {
				LCD_Command(0x01);
				_delay_ms(2);
			}

			void LCD_set_cursor(uint8_t row, uint8_t col) {
				uint8_t pos = (row == 0) ? (0x80 + col) : (0xC0 + col);
				LCD_Command(pos);
			}

			// --- CORREÇÃO DA DRIVER UART (UBRR0H e UBRR0L Corrigidos!) ---
			void UART_init(uint16_t ubrr) {
				UBRR0H = (uint8_t)(ubrr >> 8);
				UBRR0L = (uint8_t)ubrr;
				UCSR0B = (1 << TXEN0);
				UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
			}

			void UART_send_string(const char* str) {
				while (*str) {
					while (!(UCSR0A & (1 << UDRE0)));
					UDR0 = *str++;
				}
			}

			void I2C_init(void) { TWBR = 72; }
			void MPU6050_init(void) {}
			void MPU6050_read(DadosSensor *dados) {
				dados->ax = 16384; dados->ay = 0; dados->az = 16384;
				dados->gx = 0; dados->gy = 0; dados->gz = 0; dados->temp = 26.5f;
			}
