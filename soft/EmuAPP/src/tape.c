#include "tape.h"

#include "ets.h"
#include "i8080_hal.h"
#include "ffs.h"
#include "ui.h"
#include "vg75.h"
#include "ps2.h"


#define IN_SYNC_COUNT	16
#define IN_TIMEOUT	5000
#define IN_BUF_SIZE	(24*1024)
static struct in
{
    uint8_t  buf[IN_BUF_SIZE];
    
    uint32_t prev_cycles;
    uint8_t  bit, byte, bit_cnt;
    uint8_t  start, c;
    uint8_t  sync;
    uint16_t period;
    
    uint16_t size;
} in;


#define OUT_SYNC_COUNT	256
#define OUT_BIT_TIME	1600
#define OUT_BUF_SIZE	512
static struct out
{
    uint8_t  buf[OUT_BUF_SIZE];
    uint16_t pos;
    uint32_t prev_cycles;
    uint32_t dataPtr;
    uint16_t dataSize;
    uint16_t sync;
    uint8_t  byte, bit;
    uint8_t  start, c, out;
} out;


void tape_init(void)
{
    in.prev_cycles=0;
    in.bit=0;
    in.byte=0;
    in.bit_cnt=0;
    in.start=0;
    in.c=0;
    in.sync=IN_SYNC_COUNT;
    in.period=0;
    in.size=0;
    
    out.prev_cycles=0;
    out.dataPtr=0;
    out.dataSize=0;
    out.sync=0;
    out.byte=0;
    out.bit=0;
    out.start=0;
    out.out=0;
    out.pos=0;
}


static void tape_in_bit(void)
{
    // ��������� ���
    in.byte=(in.byte << 1) | in.bit;
    in.bit_cnt++;
    
    if (in.bit_cnt==8)
    {
	// ������ ���� - ������ � �����
	if (in.size < IN_BUF_SIZE)
	    in.buf[in.size++]=in.byte;
	
	// �������� ����� ���������� �����
	in.byte=0;
	in.bit_cnt=0;
    }
}


void tape_in(void)
{
    // �������� ���-�� ������ � �������� ������ ������
    uint32_t T=i8080_cycles - in.prev_cycles;
    in.prev_cycles=i8080_cycles;
    
    if (T > IN_TIMEOUT)
    {
	// �������
	in.sync=IN_SYNC_COUNT;
	in.start=0;
	return;
    }
    
    // �������������� �������
    if (in.sync > 0)
    {
	// �������������
	if ( (T < in.period/2) || (T > in.period+in.period/2) )
	{
	    // ������� ������
	    in.period=T;
	    in.sync=IN_SYNC_COUNT;
	    in.start=0;
	    return;
	} else
	{
	    // ���������
	    in.period=(in.period+T)/2;
	    in.sync--;
	}
    } else
    {
	uint8_t d;
	
	// ���������� - �������� ��� ������� ������
	if ( (T < in.period/2) || (T > in.period*3) )
	{
	    // ������� ������
	    in.period=T;
	    in.sync=IN_SYNC_COUNT;
	    in.start=0;
	    return;
	} else
	if (T >= in.period+in.period/2)
	{
	    // �������
	    d=1;
	    in.period=(in.period+T/2)/2;
	} else
	{
	    // ��������
	    d=0;
	    in.period=(in.period+T)/2;
	}
	
	// ������������ ������
	if (! in.start)
	{
	    // ���� ��������� ���
	    if (d)
	    {
		// ������ ������
		in.bit=1;
		in.start=1;
		in.c=1;
		in.byte=1;
		in.bit_cnt=1;
		in.size=0;
	    }
	} else
	{
	    if (d)
	    {
		// ������� - ����� ��������
		in.bit=in.bit ^ 1;
		in.c=1;
		tape_in_bit();
	    } else
	    {
		// ������ ������ �������� - ������ ����
		in.c^=1;
		if (in.c) tape_in_bit();
	    }
	}
    }
}


bool tape_out(void)
{
    uint32_t T=i8080_cycles - out.prev_cycles;
    
    // ���� �������� ����
    if (T < OUT_BIT_TIME/2)
    {
	// ���� ���� ������� ������
	return out.out;
    }
    
    // ���������, ���� ������ �� ����� ������� ���������
    if (T < OUT_BIT_TIME)
	out.prev_cycles+=OUT_BIT_TIME/2; else
	out.prev_cycles=i8080_cycles;
    
    // ��������� �� ����� ��������
    if (! out.start) return false;
    
    // ������� �������� ���� ��� ����������
    out.c^=1;
    if (out.c) return out.out ^ 1;
    
    // ������� ����� ����, ���� ����
    if (out.bit==0)
    {
	if (out.sync > 0)
	{
	    // �������������
	    //ets_printf("TAPE: sync %d\n", out.sync);
	    out.byte=0x55;
	    out.bit=0x80;
	    out.sync--;
	} else
	{
	    // ������
	    if (out.dataSize==0)
	    {
		// ����� ������
		ets_printf("TAPE: end\n");
		out.start=false;
		return false;
	    }
	    
	    // �������� - �������� �� ������ � ������
	    if (out.pos >= OUT_BUF_SIZE)
	    {
		// ���� ���������� ������ �� ����� � �����
		uint16_t s=out.dataSize;
		if (s > OUT_BUF_SIZE)
		    s=OUT_BUF_SIZE; else
		    s=(s+3) & ~0x03;
		//ets_printf("TAPE: load from flash size=%d ptr=0x%05X buf=0x%08X\n", s, out.dataPtr, (uint32_t)out.buf);
		SPIRead(out.dataPtr, out.buf, s);
		out.dataPtr+=s;
		out.pos=0;
	    }
	    
	    // ����� ��������� ���� �� ������
	    out.byte=out.buf[out.pos++];
	    out.bit=0x80;
	    out.dataSize--;
	    //ets_printf("TAPE: data=0x%02X\n", out.byte);
	}
    }

    // ����������� ���
    out.out=(out.byte & out.bit) ? 1 : 0;
    out.bit>>=1;
    
    return out.out;
}


bool tape_periodic(void)
{
    // ��������� ����� ������
    if (in.start)
    {
	// ���� ������, �������� �� ������� ����
	uint32_t T=i8080_cycles - in.prev_cycles;
	if (T > IN_TIMEOUT)
	{
	    // ������� - ��������� �����
	    ets_printf("TAPE IN DONE size=%d !\n", in.size);
	    in.start=false;
	    return true;
	}
    }
    return false;
}


void tape_save(void)
{
    const char *name;
    
    // �������� ��� �����
again:
    name=ui_input_text("������� ��� ����� ��� ����������:", 8);
    if (! name) return;
    
    // ���� - ����� ����� ���� ��� ����
    if (ffs_find(name) >= 0)
    {
	// ��� ���� ����� ����
	ui_draw_text(10, 12, "���� � ����� ������ ��� ���������� !");
	ui_sleep(2000);
	goto again;
    }
    
    // ������� ����
    screen.cursor_y=99;
    ui_draw_text(10, 12, "������ � ����...");
    if (! ffs_write(name, TYPE_TAPE, in.buf, in.size))
    {
	// ������ ������
	ui_draw_text(10, 12, "������ ������ � ���� (���� �����?) !");
	ui_sleep(2000);
    }
}


void tape_load(void)
{
    // �������� ����
    int16_t n=ui_select_file(TYPE_TAPE);
    if (n < 0) return;
    
    // �������� ������
    out.dataPtr=ffs_flash_addr(n);
    out.dataSize=fat[n].size;
    out.pos=OUT_BUF_SIZE;
    out.sync=OUT_SYNC_COUNT;
    out.start=1;
}