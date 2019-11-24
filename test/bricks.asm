	;
	; Bricks game in one boot sector
	;
	; by Oscar Toledo G.
	;
	; Creation date: Nov/02/2019.
	;

	cpu 8086

	;
	; Press Left Shift to start the game
	; Press Left Ctrl to move the paddle to left
	; Press Left Alt to move the paddle to right
	;

    %ifdef com_file
        org 0x0100
    %else
        org 0x7c00
    %endif

old_time:	equ 16	; Old time 
ball_x:		equ 14	; X-coordinate of ball (8.8 fraction)
ball_y:		equ 12	; Y-coordinate of ball (8.8 fraction)
ball_xs:	equ 10	; X-speed of ball (8.8 fraction)
ball_ys:	equ 8	; Y-speed of ball (8.8 fraction)
beep:		equ 6	; Frame count to turn off sound
bricks:		equ 4	; Remaining bricks
balls:         equ 2	; Remaining balls
score:         equ 0	; Current score

	;
	; Start of the game
	;
start:
	mov ax,0x0002		; Text mode 80x25x16 colors
	int 0x10		; Setup
	mov ax,0xb800		; Address of video screen
	mov ds,ax		; Setup DS
	mov es,ax		; Setup ES
	sub sp,32
	xor ax,ax
	push ax			; Reset score
	mov al,4		
	push ax			; Balls remaining
	mov bp,sp		; Setup stack frame for globals
	;
	; Start another level 
	;
another_level:
	mov word [bp+bricks],273	; 273 bricks on screen
	xor di,di
	mov ax,0x01b1		; Draw top border
	mov cx,80
	cld
	rep stosw
	mov cl,24		; 24 rows
.1:
	stosw			; Draw left border
	mov ax,0x20		; No bricks on this row
	push cx
	cmp cl,23
	jnb .2
	sub cl,15
	jbe .2
	mov al,0xdb		; Bricks on this row
	mov ah,cl
.2:
	mov cl,39		; 39 bricks per row
.3:
	stosw
	stosw
	inc ah			; Increase attribute color
	cmp ah,0x08
	jne .4
	mov ah,0x01
.4:
	loop .3
	pop cx

	mov ax,0x01b1		; Draw right border
	stosw
	loop .1

	;
	; Start another ball
	;
	mov di,0x0f4a		; Position of paddle
another_ball:
	mov byte [bp+ball_x+1],0x28	; Center X
	mov byte [bp+ball_y+1],0x14	; Center Y
	xor ax,ax
	mov [bp+ball_xs],ax	; Static on screen
	mov [bp+ball_ys],ax
	mov byte [bp+beep],0x01

	mov si,0x0ffe		; Don't erase ball yet
game_loop:
	call wait_frame		; Wait 1/18.2 secs.

	mov word [si],0x0000	; Erase ball
	
	call update_score	; Update score
	
	mov ah,0x02		; Read modifier keys
	int 0x16
	test al,0x04		; Left ctrl
	je .1
	mov byte [di+6],0	; Erase right side of paddle
	mov byte [di+8],0
	sub di,byte 4		; Move paddle to left
	cmp di,0x0f02		; Limit
	ja .1
	mov di,0x0f02
.1:
	test al,0x08		; Left alt
	je .2
	xor ax,ax		; Erase left side of paddle
	stosw
	stosw			; DI increased automatically
	cmp di,0x0f94		; Limit
	jb .2
	mov di,0x0f94	
.2:
	test al,0x02		; Left shift
	je .15
	mov ax,[bp+ball_xs]	; Ball moving?
	add ax,[bp+ball_ys]
	jne .15			; Yes, jump
				; Setup movement of ball
	mov word [bp+ball_xs],0xff40
	mov word [bp+ball_ys],0xff80
.15:
	mov ax,0x0adf		; Paddle graphic and color
	push di
	stosw			; Draw paddle
	stosw
	stosw
	stosw
	stosw
	pop di

	mov bx,[bp+ball_x]		; Draw ball
	mov ax,[bp+ball_y]
	call locate_ball	; Locate on screen
	test byte [bp+ball_y],0x80	; Y-coordinate half fraction?
	mov ah,0x60		; Interchange colors for smooth mov.
	je .12
	mov ah,0x06
.12:	mov al,0xdc		; Graphic
	mov [bx],ax		; Draw
	push bx
	pop si

.14:
	mov bx,[bp+ball_x]		; Ball position
	mov ax,[bp+ball_y]
	add bx,[bp+ball_xs]	; Add movement speed
	add ax,[bp+ball_ys]
	push ax
	push bx
	call locate_ball	; Locate on screen
	mov al,[bx]
	cmp al,0xb1		; Touching borders
	jne .3
	mov cx,5423		; 1193180 / 220
	call speaker		; Generate sound
	pop bx
	pop ax
	cmp bh,0x4f
	je .8
	test bh,bh
	jne .7
.8:
	neg word [bp+ball_xs]	; Negate X-speed if touches sides
.7:	
	test ah,ah
	jnz .9
	neg word [bp+ball_ys]	; Negate Y-speed if touches sides
.9:	jmp .14

.3:
	cmp al,0xdf		; Touching paddle
	jne .4
	sub bx,di		; Subtract paddle position
	sub bx,byte 4
	mov cl,6		; Multiply by 64
	shl bx,cl
	mov [bp+ball_xs],bx	; New X speed for ball
	mov word [bp+ball_ys],0xff80	; Update Y speed for ball
	mov cx,2711		; 1193180 / 440
	call speaker		; Generate sound
	pop bx
	pop ax
	jmp .14

.4:
	cmp al,0xdb		; Touching brick
	jne .5
	mov cx,1355		; 1193180 / 880
	call speaker		; Generate sound
	test bl,2		; Aligned with brick?
	jne .10			; Yes, jump
	dec bx			; Align
	dec bx
.10:	xor ax,ax		; Erase brick
	mov [bx],ax
	mov [bx+2],ax
	inc word [bp+score]	; Increase score
	neg word [bp+ball_ys]	; Negate Y speed (rebound)
	pop bx
	pop ax
	dec word [bp+bricks]	; One brick less on screen
	jne .14			; Fully completed? No, jump.
	jmp another_level	; Start another level

.5:
	pop bx
	pop ax
.6:
	mov [bp+ball_x],bx		; Update ball position
	mov [bp+ball_y],ax
	cmp ah,0x19		; Ball exited through bottom?
	je ball_lost		; Yes, jump
	jmp game_loop		; No, repeat game loop

	;
	; Ball lost
	; 
ball_lost:
	mov cx,10846		; 1193180 / 110
	call speaker		; Generate sound

	mov word [si],0		; Erase ball
	dec byte [bp+balls]	; One ball less
	js .1			; All finished? Yes, jump
	jmp another_ball	; Start another ball

.1:	call wait_frame.2	; Turn off sound
	int 0x20		; Exit to DOS / bootOS

wait_frame:
.0:
	mov ah,0x00		; Read ticks
	int 0x1a		; Call BIOS
	cmp dx,[bp+old_time]	; Wait for change
	je .0
	mov [bp+old_time],dx

	dec byte [bp+beep]		; Decrease time to turn off beep
	jne .1
.2:
	in al,0x61
	and al,0xfc		; Turn off
	out 0x61,al
.1:

	ret

	;
	; Generate sound on PC speaker
	;
speaker:
	mov al,0xb6		; Setup timer 2
	out 0x43,al
	mov al,cl		; Low byte of timer count
	out 0x42,al
	mov al,ch		; High byte of timer count
	out 0x42,al
	in al,0x61
	or al,0x03		; Connect PC speaker to timer 2
	out 0x61,al
	mov byte [bp+beep],3	; Duration
	ret

	;
	; Locate ball on screen
	;
locate_ball:
	mov al,0xa0
	mul ah			; AH = Y coordinate (row)
	mov bl,bh		; BH = X coordinate (column)
	mov bh,0
	shl bx,1
	add bx,ax
	ret

	;
	; Update score indicator (from bootRogue)
	;
update_score:
	mov bx,0x0f98		; Point to bottom right corner
	mov ax,[bp+score]
	call .1
	mov al,[bp+balls]
.1:
	xor cx,cx              ; CX = Quotient
.2:	inc cx
	sub ax,10              ; Division by subtraction
	jnc .2
	add ax,0x0a3a          ; Convert remainder to ASCII digit + color
	call .3                ; Put on screen
	xchg ax,cx
	dec ax                 ; Quotient is zero?
	jnz .1                 ; No, jump to show more digits.

.3:	mov [bx],ax
	dec bx
	dec bx
	ret

    %ifdef com_file
    %else
	times 510-($-$$) db 0x4f
	db 0x55,0xaa           ; Make it a bootable sector
    %endif
