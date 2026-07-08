;
; Atv01.asm
;
; Created: 16/03/2026 17:05:46
; Author : Nicolas
;
.include "m328pdef.inc"
.cseg
.org 0x0000         
rjmp main 


array:
	.db 10, 25, 50, 100, 150, 200, 220, 240, 250, 255
main: 

	ldi r16, high(RAMEND)
    out SPH, r16
    ldi r16, low(RAMEND)
    out SPL, r16

	ldi r16, 0b11110000
	out DDRD, r16

	clr     r16
	clr     r18 
	clr     r19  
	clr     r20

reset_ponteiro:

	ldi ZH, high(array << 1) 
    ldi ZL, low(array << 1)
	ldi r21, 10

loop_leitura:
	lpm r16, Z+
	
	;rotina de saída dos dados
contador_centenas:
	cpi r16, 100
	brlo contador_dezenas
	subi r16, 100
	inc r18
	rjmp contador_centenas

contador_dezenas:
	cpi r16, 10
	brlo contador_unidades
	subi r16, 10
	inc r19
	rjmp contador_dezenas

contador_unidades:
	cpi r16, 1
	brlo mostrar_display
	subi r16, 1
	inc r20

rcall mostrar_display

	dec r21
	brne loop_leitura

	rjmp reset_ponteiro


mostrar_display:
	ldi r22, 61       
loop_ext:
    ldi r24, 0xFF       
    ldi r25, 0xFF     
loop_int:
    sbiw r24, 1

	swap r18
    out  PORTD, r18 ;DP0
	swap r19
    out  PORTD, r19 ;DP3
	swap r20
    out  PORTD, r20 ;DP2
	    
    brne loop_int      

    dec r22             
    brne loop_ext      
    ret
/*
delay_1s:
    ldi r22, 61       
loop_ext:
    ldi r24, 0xFF       
    ldi r25, 0xFF     
loop_int:
    sbiw r24, 1         
    brne loop_int      
    
    dec r22             
    brne loop_ext      
    ret
*/
