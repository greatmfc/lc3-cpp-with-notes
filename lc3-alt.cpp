/* lc3-alt.cpp */
/* Includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
/* unix */
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>


/* Registers */
enum
{
	R_R0 = 0,
	R_R1,
	R_R2,
	R_R3,
	R_R4,
	R_R5,
	R_R6,
	R_R7,
	R_PC, /* program counter */
	R_COND,
	R_COUNT
};

/* Condition Flags */
enum
{
	FL_POS = 1 << 0, /* P */
	FL_ZRO = 1 << 1, /* Z */
	FL_NEG = 1 << 2, /* N */
};

/* Opcodes */
enum
{
	OP_BR = 0, /* branch */
	OP_ADD,    /* add  */
	OP_LD,     /* load */
	OP_ST,     /* store */
	OP_JSR,    /* jump register */
	OP_AND,    /* bitwise and */
	OP_LDR,    /* load register */
	OP_STR,    /* store register */
	OP_RTI,    /* unused */
	OP_NOT,    /* bitwise not */
	OP_LDI,    /* load indirect */
	OP_STI,    /* store indirect */
	OP_JMP,    /* jump */
	OP_RES,    /* reserved (unused) */
	OP_LEA,    /* load effective address */
	OP_TRAP    /* execute trap */
};


/* Memory Mapped Registers */
enum
{
	MR_KBSR = 0xFE00, /* keyboard status */
	MR_KBDR = 0xFE02  /* keyboard data */
};

/* TRAP Codes */
enum
{
	TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
	TRAP_OUT = 0x21,   /* output a character */
	TRAP_PUTS = 0x22,  /* output a word string */
	TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
	TRAP_PUTSP = 0x24, /* output a byte string */
	TRAP_HALT = 0x25   /* halt the program */
};


/* Memory Storage */
/* 65536 locations */
uint16_t memory[UINT16_MAX];

/* Register Storage */
uint16_t reg[R_COUNT];


/* Sign Extend */
uint16_t sign_extend(uint16_t x, int bit_count)
{
	if ((x >> (bit_count - 1)) & 1) { //位减一以获得其最高位是1还是0，如果是0则不需要额外补码
		x |= (0xFFFF << bit_count);
	}
	return x;
}

/* Swap */
uint16_t swap16(uint16_t x) //大小端转换
{
	return (x << 8) | (x >> 8);
}

/* Update Flags */
//根据输入寄存器中的值设置寄存器状态码
void update_flags(uint16_t r)
{
	if (reg[r] == 0)
	{
		reg[R_COND] = FL_ZRO;
	}
	else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
	{
		reg[R_COND] = FL_NEG;
	}
	else
	{
		reg[R_COND] = FL_POS;
	}
}

/* Read Image File */
void read_image_file(FILE* file)
{
	/* the origin tells us where in memory to place the image */
	uint16_t origin;
	fread(&origin, sizeof(origin), 1, file);
	origin = swap16(origin); //前16位规定了该程序在内存中的地址

	/* we know the maximum file size so we only need one fread */
	uint16_t max_read = UINT16_MAX - origin;
	uint16_t* p = memory + origin;
	size_t read = fread(p, sizeof(uint16_t), max_read, file);

	/* swap to little endian */
	while (read-- > 0)
	{
		*p = swap16(*p);
		++p;
	}
}

/* Read Image */
int read_image(const char* image_path)
{
	FILE* file = fopen(image_path, "rb");
	if (!file) { return 0; };
	read_image_file(file);
	fclose(file);
	return 1;
}

/* Check Key */
uint16_t check_key() //监听输入
{
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

/* Memory Access */
//把指定的值写入到该地址所在的内存中
void mem_write(uint16_t address, uint16_t val)
{
	memory[address] = val;
}

//返回该地址在内存中的值
uint16_t mem_read(uint16_t address)
{
	if (address == MR_KBSR)
	{
		if (check_key())
		{
			memory[MR_KBSR] = (1 << 15);
			memory[MR_KBDR] = getchar();
		}
		else
		{
			memory[MR_KBSR] = 0;
		}
	}
	return memory[address];
}

/* Input Buffering */
struct termios original_tio;

void disable_input_buffering()
{
	tcgetattr(STDIN_FILENO, &original_tio); //返回当前终端的属性
	struct termios new_tio = original_tio;
	new_tio.c_lflag &= ~ICANON & ~ECHO; //取消使用标准输入模式和显示输入字符
	tcsetattr(STDIN_FILENO, TCSANOW, &new_tio); //TCSANOW：不等数据传输完毕就立即改变属性
}

void restore_input_buffering() //恢复输入缓存
{
	tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

/* Handle Interrupt */
void handle_interrupt(int signal) //处理中断，When the program is interrupted, we want to restore the terminal settings back to normal.
{
	restore_input_buffering();
	printf("\n");
	exit(-2);
}


int running = 1;
/* Instruction C++ */
template <unsigned op>
void ins(uint16_t instr)
{
	uint16_t r0, r1, r2, imm5, imm_flag;
	//pc相对寻址是指相对于PC的偏移量，基类相对寻址是指相对于指令指定的基类寄存器的偏移量
	uint16_t pc_plus_off, base_plus_off;

	//以下到238行都是公共逻辑
	constexpr uint16_t opbit = (1 << op);
	if (0x4EEE & opbit) { r0 = (instr >> 9) & 0x7; } //前面的均为掩码，不同的命令op导致的opbit数值不同则执行的选项也不同
	if (0x12F3 & opbit) { r1 = (instr >> 6) & 0x7; }
	if (0x0022 & opbit)
	{
		imm_flag = (instr >> 5) & 0x1;

		if (imm_flag)
		{
			imm5 = sign_extend(instr & 0x1F, 5); //与0x1F是为了取出立即数的五位
		}
		else
		{
			r2 = instr & 0x7;
		}
	}
	if (0x00C0 & opbit)
	{   // Base + offset（基址偏移寻址）
		base_plus_off = reg[r1] + sign_extend(instr & 0x3F, 6);
	}
	if (0x4C0D & opbit)
	{
		// Indirect address（PC相对寻址）
		pc_plus_off = reg[R_PC] + sign_extend(instr & 0x1FF, 9);
	}
	if (0x0001 & opbit)
	{
		// BRanch
		//如果三个全为0则不检查任何条件码。如果位1，则看上一条指令对寄存器的操作满不满足条件。（三个条件码全为1则是无条件跳转）
		uint16_t cond = (instr >> 9) & 0x7;
		if (cond & reg[R_COND]) { reg[R_PC] = pc_plus_off; }
	}
	if (0x0002 & opbit)  // ADD 0x0002的二进制是0010
	{
		if (imm_flag)
		{
			reg[r0] = reg[r1] + imm5;
		}
		else
		{
			reg[r0] = reg[r1] + reg[r2];
		}
	}
	if (0x0020 & opbit)  // AND 同理，0x0020的二进制是0010 0000
	{
		if (imm_flag)
		{
			reg[r0] = reg[r1] & imm5;
		}
		else
		{
			reg[r0] = reg[r1] & reg[r2];
		}
	}
	if (0x0200 & opbit) { reg[r0] = ~reg[r1]; } // NOT
	if (0x1000 & opbit) { reg[R_PC] = reg[r1]; } // JMP
	if (0x0010 & opbit)  // JSR(jump register)
	{
		uint16_t long_flag = (instr >> 11) & 1;
		reg[R_R7] = reg[R_PC];
		if (long_flag) //如果该位为1说明该指令为长的JSR需要取其后11位
		{
			pc_plus_off = reg[R_PC] + sign_extend(instr & 0x7FF, 11);
			reg[R_PC] = pc_plus_off;
		}
		else //否则该指令是JSRR
		{
			reg[R_PC] = reg[r1];
		}
	}

	if (0x0004 & opbit) { reg[r0] = mem_read(pc_plus_off); } // LD
	if (0x0400 & opbit) { reg[r0] = mem_read(mem_read(pc_plus_off)); } // LDI
	if (0x0040 & opbit) { reg[r0] = mem_read(base_plus_off); }  // LDR(load register)
	if (0x4000 & opbit) { reg[r0] = pc_plus_off; } // LEA(load effctive address)
	if (0x0008 & opbit) { mem_write(pc_plus_off, reg[r0]); } // ST(store)
	if (0x0800 & opbit) { mem_write(mem_read(pc_plus_off), reg[r0]); } // STI(store indirect)
	if (0x0080 & opbit) { mem_write(base_plus_off, reg[r0]); } // STR(store register)
	if (0x8000 & opbit)  // TRAP
	{
		 /* TRAP */
		 switch (instr & 0xFF)
		 {
			 case TRAP_GETC:
				 /* TRAP GETC */
				 /* read a single ASCII char */
				 reg[R_R0] = (uint16_t)getchar();
				 update_flags(R_R0);

				 break;
			 case TRAP_OUT:
				 /* TRAP OUT */
				 putc((char)reg[R_R0], stdout);
				 fflush(stdout);

				 break;
			 case TRAP_PUTS:
				 /* TRAP PUTS */
				 {
					 /* one char per word */
					 uint16_t* c = memory + reg[R_R0];
					 while (*c)
					 {
						 putc((char)*c, stdout);
						 ++c;
					 }
					 fflush(stdout);
				 }

				 break;
			 case TRAP_IN:
				 /* TRAP IN */
				 {
					 printf("Enter a character: ");
					 char c = getchar();
					 putc(c, stdout);
					 fflush(stdout);
					 reg[R_R0] = (uint16_t)c;
					 update_flags(R_R0);
				 }

				 break;
			 case TRAP_PUTSP:
				 /* TRAP PUTSP */
				 {
					 /* one char per byte (two bytes per word)
						here we need to swap back to
						big endian format */
					 uint16_t* c = memory + reg[R_R0];
					 while (*c)
					 {
						 char char1 = (*c) & 0xFF;
						 putc(char1, stdout);
						 char char2 = (*c) >> 8;
						 if (char2) putc(char2, stdout);
						 ++c;
					 }
					 fflush(stdout);
				 }

				 break;
			 case TRAP_HALT:
				 /* TRAP HALT */
				 puts("HALT");
				 fflush(stdout);
				 running = 0;

				 break;
		 }

	}
	//if (0x0100 & opbit) { } // RTI
	if (0x4666 & opbit) { update_flags(r0); }
}

/* Op Table */
static void (*op_table[16])(uint16_t) = {
	ins<0>, ins<1>, ins<2>, ins<3>,
	ins<4>, ins<5>, ins<6>, ins<7>,
	NULL, ins<9>, ins<10>, ins<11>,
	ins<12>, NULL, ins<14>, ins<15>
};


int main(int argc, const char* argv[])
{
	/* Load Arguments */
	if (argc < 2)
	{
		/* show usage string */
		printf("lc3 [image-file1] ...\n");
		exit(2);
	}
	
	for (int j = 1; j < argc; ++j)
	{
		if (!read_image(argv[j]))
		{
			printf("failed to load image: %s\n", argv[j]);
			exit(1);
		}
	}

	/* Setup */
	signal(SIGINT, handle_interrupt);
	disable_input_buffering();


	reg[R_COND] = FL_ZRO;

	enum { PC_START = 0x3000 };
	reg[R_PC] = PC_START;

	while (running)
	{
		uint16_t instr = mem_read(reg[R_PC]++);
		uint16_t op = instr >> 12; //取头部的四位命令码
		op_table[op](instr);
	}
	/* Shutdown */
	restore_input_buffering();

}

