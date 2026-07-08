;
; Atv02.asm
;
; Created: 30/03/2026 18:15:57
;Atualizado: 30/04/2026 16:30
; Author : Nicolas

.include "m328pdef.inc"
.org 0x0000           ; A proxima instruncao em 0x0000
rjmp main             ; salte para main:

main: ; Configura pinos e inicializa as variaveis e o ADC
	clr r1
	clr r22
	ldi r16, high(RAMEND)
    out SPH, r16
    ldi r16, low(RAMEND)
    out SPL, r16
	; configuração de entradas e saídas
	ldi r16, 0 ; configurando o portC, como entradas
	out DDRC, r16
	ldi r16, 0b00001111; configurando o port B como entradas(0) e saídas(1)
	out DDRB, r16
	ldi r16, 0b11111100 ; configurando o port D como entradas(0) e saídas(1)
	out DDRD, r16
	sbi DDRB, 0 
    sbi DDRB, 1 ; Saída LED Vermelho
    sbi DDRB, 2 ; Saída LED Verde
    sbi DDRB, 3 ; Saída LED Azul
	ldi r16, 0b00001110
	out PORTC, r16
	; configuração do ADC
    ldi r16, 0b01000000 
    sts ADMUX, r16
    ldi r16, 0b10000111
    sts ADCSRA, r16
	rcall delay_estabilizacao
	; valores iniciais por padrão
	clr r14
	clr r15
	clr r20
	clr r21
	clr r23 
	ldi r17, 0xE7
	ldi r18, 0x03
	mov r20, r14
	mov r21, r15
	ldi r19, 1
	clr r22
	clr r1 

loop_principal:
	rcall ler_adc
    rcall tratar_botoes
	tst r23
	breq modo_crescente 
	rjmp modo_decrescente 

modo_crescente: ; Soma o valor do passo e verifica o limite máximo
	add r20, r19        ; Soma o passo
    adc r21, r1
    cp r20, r17
    cpc r21, r18
    brlo fim_contagem
    mov r20, r14  
	mov r21, r15 
	rjmp fim_contagem

modo_decrescente: ; Subtrai o valor do passo e verifica o limite mínimo
	sub r20, r19        ; Subtrai o passo
    sbc r21, r1
	brcs carregar_max 
	cp r20, r14 
	cpc r21, r15 
    brsh fim_contagem 

carregar_max:
    mov r20, r17
    mov r21, r18
	rjmp fim_contagem

fim_contagem:
    rcall exibicao 
	;rcall delay_curto
    rjmp loop_principal

tratar_botoes: ; Realiza varredura dos botões S1, S2 e S3
	sbic PINC, 2
	rjmp checar_s1_s3
	rcall delay_curto

espera_s2:
    sbis PINC, 2 ; Aguarda o botão S2 ser liberado
    rjmp espera_s2
    inc r22               ;Incrementa o estado (muda o modo)
    cpi r22, 4	;Verifica se passou do ultimo estado (3)
    brlo atualiza_led
    clr r22 ; Reseta para o estado de contagem (0)

atualiza_led:
    rcall set_led
	ret

set_led:
	cbi PORTB, 1
	cbi PORTB, 2
	cbi PORTB, 3
	cpi r22, 1
	breq liga_red
	cpi r22, 2
	breq liga_green
	cpi r22, 3
	breq liga_blue
	ret

liga_red:	
	sbi PORTB, 1
	ret

liga_green:
	sbi PORTB, 2
	ret

liga_blue:
	sbi PORTB, 3
	ret

checar_s1_s3:
	cpi r22, 0
	brne ajustar_param
	sbic PINC, 1
	rjmp checar_s3
	clr r23
	ret

checar_s3:
	sbic PINC, 3        ; Se S3 pressionado (0)
    ret                 ; Senão, sai
    ldi r23, 1          ; r23 = 1 (Decrescente)
    ret

fim_botoes: 
	ret

ajustar_param:
	cpi r22, 1
	breq s_min
	cpi r22, 2
	breq s_max
	mov r19, r30
	lsr r19
	lsr r19
	lsr r19
	lsr r19
	inc r19
	cpi r19, 16
	brlo fim_ajuste
	ldi r19, 15

fim_ajuste:
	ret

s_min:
	mov r14, r30
	mov r15, r31
	ret
s_max:
	mov r17, r30
	mov r18, r31
	ret

ler_adc:
	lds r16, ADCSRA
	ori r16, (1<<ADSC)
	sts ADCSRA, r16

aguarda_adc:
	lds r16, ADCSRA
    sbrc r16, ADSC     
    rjmp aguarda_adc
    lds r30, ADCL       
    lds r31, ADCH       
    ret

exibicao:
	push r20
	push r21
	cpi r22, 1 
	breq val_min_16
    cpi r22, 2
	breq val_max_16
    cpi r22, 3 
	breq val_passo_16
    rjmp exec_bcd

val_min_16: 
	mov r20, r14
	mov r21, r15
	rjmp exec_bcd

val_max_16: 
	mov r20, r17
	mov r21, r18
	rjmp exec_bcd

val_passo_16: 
	mov r20, r19
	clr r21

exec_bcd:
	rcall separar_bcd
	pop r21
	pop r20
	ret	

separar_bcd: ; Converte o valor em centenas, dezenas e unidades
    ldi r16, low(1000)
    cp r20, r16
    ldi r16, high(1000)
    cpc r21, r16
    brlo inicia_conversao ; Se for < 1000, tudo certo
    ldi r20, 0xE7        ; Caso contrário, força exibir 999
    ldi r21, 0x03

inicia_conversao:
	clr r24 ; centenas
	clr r25 ; dezenas
	clr r26 ; unidades

	contador_centenas:
		ldi r16, 100
		cp r20, r16
		cpc r21, r1
		brlo contador_dezenas
		subi r20, 100 ; subtrai 100
		sbci r21, 0
		inc r24 ; de r18 para r24 incrementa contador de centenas
		rjmp contador_centenas

	contador_dezenas:
		cpi r20, 10
		brlo contador_unidades
		subi r20, 10 ; subtrai 10
		inc r25 ; de r19 p r25 incrementa contador de dezenas
		rjmp contador_dezenas

	contador_unidades:
		mov r26, r20
		rcall mostrar_display
		ret

mostrar_display: ; Multiplexa os displays (efeito POV)
	ldi r27, 9  ; de r22 para r27

loop_longo:
	ldi r28, 100     

loop_ext:
	mov r29, r24 ; carrega o dígito
	clr r30
	sbrc r29, 0
    ori  r30, 8         ; (bit 0 -> 3)
    sbrc r29, 1
    ori  r30, 4         ; (bit 1 -> 2)
    sbrc r29, 2
    ori  r30, 2         ; (bit 2 -> 1)
    sbrc r29, 3
    ori  r30, 1         ; (bit 3 -> 0)
	swap r30 ; joga os bits para os nibbles altos
	out PORTD, r30
	sbi PORTB, 0
	rcall delay_curto
	cbi PORTB, 0
    
loop_int:
	rcall delay_curto
	mov r29, r25
	clr r30
	sbrc r29, 0
    ori  r30, 8         ; (bit 0 -> 3)
    sbrc r29, 1
    ori  r30, 4         ; (bit 1 -> 2)
    sbrc r29, 2
    ori  r30, 2         ; (bit 2 -> 1)
    sbrc r29, 3
    ori  r30, 1         ; (bit 3 -> 0)
	swap r30
	ori r30, 0b00001000
	out PORTD, r30
	rcall delay_curto

loop_intint:
	rcall delay_curto
	cbi PORTD, 3
	mov r29, r26
	clr r30
	sbrc r29, 0
    ori  r30, 8         ; (bit 0 -> 3)
    sbrc r29, 1
    ori  r30, 4         ; (bit 1 -> 2)
    sbrc r29, 2
    ori  r30, 2         ; (bit 2 -> 1)
    sbrc r29, 3
    ori  r30, 1         ; (bit 3 -> 0)
	swap r30
	ori r30, 0b00000100
	out PORTD, r30
	rcall delay_curto
    dec r28           
    brne loop_ext      
	dec r27
	brne loop_longo
    ret

delay_curto:
	push r24
	ldi r24, 0xFF

d1:
	dec r24
	brne d1
	pop r24
	ret

delay_estabilizacao:
	push r23
    ldi r16, 100

loop_est_1:
    ldi r23, 255

loop_est_2:
    dec r23
    brne loop_est_2
    dec r16
    brne loop_est_1
	pop r23
    ret

