#define F_CPU 16000000UL

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
	// Mantida para conformidade com o FreeRTOS do laboratório
}

/************************************************************************/
/* DEFINICOES DE HARDWARE (MIA - PROJETO 09)                           */
/************************************************************************/
#define LCD_RS PD2
#define LCD_EN PD3
#define LCD_D4 PD4
#define LCD_D5 PD5
#define LCD_D6 PD6
#define LCD_D7 PD7

#define BUTTON_PIN PINC
#define S0 PC0
#define S1 PC1
#define S2 PC2
#define S3 PC3

#define RGB_PORT PORTB
#define LED_R   PB3  // D11
#define LED_G   PB2  // D10
#define LED_B   PB1  // D9

typedef enum { TELA_INIT, TELA_ACC, TELA_GIR, TELA_EULER } TelaAtiva;
volatile TelaAtiva tela_atual = TELA_INIT;

typedef struct {
	int16_t ax, ay, az;
	int16_t gx, gy, gz;
	float roll, pitch, yaw;
	float temp;
} DadosSensor;

QueueHandle_t xQueueDados;
char tx_buffer[144];
/************************************************************************/
/* PROTOTIPOS                                                           */
/************************************************************************/
void LCD_Init(void);
void LCD_Command(uint8_t cmd);
void LCD_Write(uint8_t data);
void LCD_Print(const char *str);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Clear(void);
void LCD_SendNibble(uint8_t nibble);
void LCD_PulseEnable(void);

void UART_Init(uint16_t ubrr);
void UART_SendString(const char* str);
void I2C_Init(void);
void I2C_Start(void);
void I2C_Stop(void);
void I2C_Write(uint8_t data);
uint8_t I2C_Read_ACK(void);
uint8_t I2C_Read_NACK(void);

void MPU6050_Init(void);
void MPU6050_Read(DadosSensor *dados);

void task_sensores(void *pvParameters);
void task_interface(void *pvParameters);
void task_botoes(void *pvParameters);

/************************************************************************/
/* MAIN                                                                 */
/************************************************************************/
int main(void) {
	// Configura LED RGB como saída e apaga (Nível ALTO)
	DDRB |= (1 << LED_R) | (1 << LED_G) | (1 << LED_B);
	RGB_PORT |= (1 << LED_R) | (1 << LED_G) | (1 << LED_B);

	// Configura botões como entrada com pull-up ativo
	DDRC &= ~((1 << S0) | (1 << S1) | (1 << S2) | (1 << S3));
	PORTC |= (1 << S0) | (1 << S1) | (1 << S2) | (1 << S3);

	// Inicialização dos drivers validados
	UART_Init(103); // 9600 bps
	I2C_Init();
	MPU6050_Init();
	LCD_Init();

	xQueueDados = xQueueCreate(1, sizeof(DadosSensor));

	if (xQueueDados != NULL) {
		xTaskCreate(task_sensores,  "SENSORES",  320, NULL, 3, NULL);
		xTaskCreate(task_interface, "INTERFACE", 320, NULL, 2, NULL);
		xTaskCreate(task_botoes,    "BOTOES",    128, NULL, 1, NULL);

		vTaskStartScheduler();
	}

	while (1);
	return 0;
}

/************************************************************************/
/* DRIVER DO LCD (SUA VERSÃO QUE FUNCIONOU NA BANCADA)                  */
/************************************************************************/
void LCD_PulseEnable(void) {
	PORTD |= (1<<LCD_EN);
	_delay_us(50);
	PORTD &= ~(1<<LCD_EN);
	_delay_us(100);
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

void LCD_Clear(void) {
	LCD_Command(0x01);
	_delay_ms(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col) {
	uint8_t addr = (row == 0) ? (0x00 + col) : (0x40 + col);
	LCD_Command(0x80 | addr);
}

void LCD_Print(const char *str) {
	while(*str) LCD_Write(*str++);
}

void LCD_Init(void) {
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

/************************************************************************/
/* TASKS DO SISTEMA REAL-TIME (FREERTOS)                                */
/************************************************************************/

void task_sensores(void *pvParameters) {
	DadosSensor dados;
	TickType_t xLastWakeTime = xTaskGetTickCount();
	const TickType_t xPeriodo = pdMS_TO_TICKS(200);

	while (1) {
		MPU6050_Read(&dados);
		
		// Trigonometria básica dos ângulos de Euler
		dados.roll  = atan2f((float)dados.ay, (float)dados.az) * 57.29578f;
		dados.pitch = atan2f(-(float)dados.ax, sqrtf((float)dados.ay * dados.ay + (float)dados.az * dados.az)) * 57.29578f;
		dados.yaw   = 0;

		// Envia logo para a fila
		xQueueOverwrite(xQueueDados, &dados);

		// Protocolo Serial - Convertemos TUDO para inteiros antes de mandar para o sprintf
		// Eliminamos o %f por completo para não travar a task
		int r_int = (int)dados.roll;
		int p_int = (int)dados.pitch;
		//int t_int = (int)(dados.temp * 10.0f); // Ex: 26.5V vira 265
		
		int temp_inteira = (int)dados.temp;
		int temp_decimal = abs((int)(dados.temp * 10.0f) % 10);
		
		int ax_g = (int)(((float)dados.ax / 16384.0f) * 100.0f); 
		int ay_g = (int)(((float)dados.ay / 16384.0f) * 100.0f);
		int az_g = (int)(((float)dados.az / 16384.0f) * 100.0f);
		
		 int gx_ds = (int)((float)dados.gx / 131.0f);
		 int gy_ds = (int)((float)dados.gy / 131.0f);
		 int gz_ds = (int)((float)dados.gz / 131.0f);

// 		sprintf(tx_buffer, "[acc(g): %d; %d; %d; gir(d/s): %d; %d; %d; eul(d): %d; %d; %d; temp: %d.%d]\r\n",
// 		(int)(dados.ax/16384), (int)(dados.ay/16384), (int)(dados.az/16384),
// 		(int)(dados.gx/131),   (int)(dados.gy/131),   (int)(dados.gz/131),
// 		r_int, p_int, 0, (t_int / 10), abs(t_int % 10));
		sprintf(tx_buffer, "[acc(g*100): %d; %d; %d; gir(d/s): %d; %d; %d; eul(d): %d; %d; %d; temp: %d.%d]\r\n",
		ax_g, ay_g, az_g,
		gx_ds, gy_ds, gz_ds,
		r_int, p_int, 0,
		temp_inteira, temp_decimal);
		
		UART_SendString(tx_buffer);

		// Lógica do LED RGB (Inversa - ativa em LOW)
		float mod_euler = sqrtf(dados.roll * dados.roll + dados.pitch * dados.pitch);
		if (mod_euler < 5.0f) {
			RGB_PORT |= (1 << LED_R) | (1 << LED_G) | (1 << LED_B); // Tudo apagado
			} else if (mod_euler >= 5.0f && mod_euler < 25.0f) {
			RGB_PORT &= ~(1 << LED_G); // Liga Verde
			RGB_PORT |= (1 << LED_R) | (1 << LED_B);
			} else {
			RGB_PORT &= ~(1 << LED_R); // Liga Vermelho
			RGB_PORT |= (1 << LED_G) | (1 << LED_B);
		}

		vTaskDelayUntil(&xLastWakeTime, xPeriodo);
	}
}

void task_interface(void *pvParameters) {
	DadosSensor dados_lcd;
	char buffer_lin1[17];
	char buffer_lin2[17];
	
	LCD_SetCursor(0, 0);
	LCD_Print("MIA HC-05 INIT");
	LCD_SetCursor(1, 0);
	LCD_Print("Addr: 98:D3:XX");
	vTaskDelay(pdMS_TO_TICKS(3000));

	tela_atual = TELA_EULER;

	while (1) {
		if (xQueueReceive(xQueueDados, &dados_lcd, pdMS_TO_TICKS(100)) == pdTRUE) {
			LCD_Clear();
			
			int r_int = (int)dados_lcd.roll;
			int p_int = (int)dados_lcd.pitch;
			int t_int = (int)(dados_lcd.temp * 10.0f);

			switch (tela_atual) {
				case TELA_ACC:
				sprintf(buffer_lin1, "X(g)  Y(g)  Z(g) ");
				sprintf(buffer_lin2, "%4d  %4d  %4d ", (int)dados_lcd.ax/16384, (int)dados_lcd.ay/16384, (int)dados_lcd.az/16384);
				break;
				case TELA_GIR:
				sprintf(buffer_lin1, "X(d)  Y(d)  Z(d) ");
				sprintf(buffer_lin2, "%4d  %4d  %4d ", (int)dados_lcd.gx/131, (int)dados_lcd.gy/131, (int)dados_lcd.gz/131);
				break;
				case TELA_EULER:
				default:
				sprintf(buffer_lin1, "X    Y    Z Temp");
				sprintf(buffer_lin2, "%3d  %3d  %3d %2d.%d", r_int, p_int, 0, (t_int / 10), abs(t_int % 10));
				break;
			}
			LCD_SetCursor(0, 0);
			LCD_Print(buffer_lin1);
			LCD_SetCursor(1, 0); 
			LCD_Print(buffer_lin2);
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

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
					//_delay_ms(50);
				}
			}

			/************************************************************************/
			/* COMUNICAÇÃO                                                          */
			/************************************************************************/
			void UART_Init(uint16_t ubrr) {
				UBRR0H = (uint8_t)(ubrr >> 8);
				UBRR0L = (uint8_t)ubrr;
				UCSR0B = (1 << TXEN0);
				UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
			}

			void UART_SendString(const char* str) {
				while (*str) {
					while (!(UCSR0A & (1 << UDRE0)));
					UDR0 = *str++;
				}
			}

			void I2C_Init(void) {
				TWBR = 72;
				TWSR = 0;

			}
			void I2C_Start(void) {
				TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
				while (!(TWCR & (1 << TWINT)));
			}
			void I2C_Stop(void) {
				TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
				_delay_us(10);
			}
			void I2C_Write(uint8_t data) {
				TWDR = data;
				TWCR = (1 << TWINT) | (1 << TWEN);
				while (!(TWCR & (1 << TWINT)));
			}
			uint8_t I2C_Read_ACK(void) {
				TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
				while (!(TWCR & (1 << TWINT)));
				return TWDR;
			}
			uint8_t I2C_Read_NACK(void) {
				TWCR = (1 << TWINT) | (1 << TWEN);
				while (!(TWCR & (1 << TWINT)));
				return TWDR;
			}
			void MPU6050_Init(void) {
				_delay_ms(150);
				I2C_Start();
				I2C_Write(0xD0); // Endereço de Escrita do MPU6050 (0x68 << 1)
				I2C_Write(0x6B); // Registrador PWR_MGMT_1
				I2C_Write(0x00); // Acorda o sensor
				I2C_Stop();
			}

			void MPU6050_Read(DadosSensor *dados) {
				uint8_t r_buffer[14];

				// Aponta para o endereço inicial de leitura (0x3B)
				I2C_Start();
				I2C_Write(0xD0);
				I2C_Write(0x3B);
				I2C_Stop();

				// Leitura sequencial Burst de 14 registradores
				I2C_Start();
				I2C_Write(0xD1); // Modo Leitura

				for (uint8_t i = 0; i < 13; i++) {
					r_buffer[i] = I2C_Read_ACK();
				}
				r_buffer[13] = I2C_Read_NACK();
				I2C_Stop();

				// Concatena os bytes HIGH e LOW
				dados->ax = (int16_t)((r_buffer[0]  << 8) | r_buffer[1]);
				dados->ay = (int16_t)((r_buffer[2]  << 8) | r_buffer[3]);
				dados->az = (int16_t)((r_buffer[4]  << 8) | r_buffer[5]);
				
				int16_t raw_temp = (int16_t)((r_buffer[6] << 8) | r_buffer[7]);
				dados->temp = ((float)raw_temp / 340.0f) + 36.53f;

				dados->gx = (int16_t)((r_buffer[8]  << 8) | r_buffer[9]);
				dados->gy = (int16_t)((r_buffer[10] << 8) | r_buffer[11]);
				dados->gz = (int16_t)((r_buffer[12] << 8) | r_buffer[13]);
			}
