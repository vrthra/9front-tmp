TEXT	sin(SB), $0
	FMOVD	a+0(FP), F0
	FSIN
	RET

TEXT	cos(SB), $0
	FMOVD	a+0(FP), F0
	FCOS
	RET