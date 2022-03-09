# lc3-hellloworld汇编代码介绍

/* *建议搭配lc3源码阅读* */
```assembly
.ORIG x3000			//从0x3000的地址开始运行
AND R0 R0 #0		//把r0寄存器的值清0
ADD R0 R0 #15		//H的ASCII码值为72,因此从15开始累加
ADD R0 R0 R0
ADD R0 R0 R0
ADD R1 R0 #12		//e的码值为84,以下皆同
ADD R2 R1 #15
ADD R2 R2 #14
ADD R3 R2 #7
ADD R4 R3 #0
ADD R5 R4 #3
STR R1 R7 #5		//将R7寄存器中的值加上偏移值5的地址值储存到R1
STR R2 R7 #6		//下同上
STR R3 R7 #7
STR R4 R7 #8
STR R5 R7 #9
AND R0 R0 #0		//将R0重新置0
ADD R0 R0 #15		//,的码值为44,下同
ADD R0 R0 #15
ADD R0 R0 #14
ADD R1 R5 #8
ADD R2 R5 #0
ADD R3 R5 #3
ADD R5 R5 #-11
STR R0 R7 #10		//偏移值为10的地址值储存到R0中
STR R1 R7 #11
STR R2 R7 #12
STR R3 R7 #13
STR R4 R7 #14
STR R5 R7 #15
AND R0 R0 #0
ADD R0 R0 #15
ADD R0 R0 #15
ADD R0 R0 #3
STR R0 R6 #16
ADD R0 R7 #5		//由于TRAP_PUTS是通过内存偏移R0寄存器中的值
TRAP x22			//进行寻址，因此需要将相对R7偏移的地址值储存
TRAP x25			//到R0中。TRAPX25代表中断
.END
```

