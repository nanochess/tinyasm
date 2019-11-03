	mov	ah,9
	mov	dx,msg
	int	0x21
	jmp	short overmsg
msg:	db	"Hello, world!$"
overmsg:
