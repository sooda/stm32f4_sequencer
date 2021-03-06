#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include "stm32f4xx_conf.h"
#include "utils.h"
#include "Audio.h"
#include "adc.h"
#include "synth.h"
#include "seq.h"

// Private variables
volatile uint32_t time_var1, time_var2;

static void AudioCallback(void *context,int buffer);
void Delay(volatile uint32_t nCount);
void init();

volatile int nextbuf;
volatile int buf_consumed;
#define AUDIOBUFSIZE 2048
static int16_t audio_buffer[2][AUDIOBUFSIZE];

#define BUTTON (GPIOA->IDR & GPIO_Pin_0)
void fillbuf(int16_t* buf) {
	//static uint32_t time;

	GPIO_SetBits(GPIOD, GPIO_Pin_14);
	for (int i = 0; i < AUDIOBUFSIZE/2; i++) {
		//int16_t sample = 0x7fff * (2.0 * time * (1.0f / 44100.0) * 123.0);
		//int16_t sample = 0x7fff * sinf(2.0 * 3.14159 * time * (1.0f / 44100.0) * 123.0);
		//float t = time * (1.0 / 44100.0);
		int16_t sample = synth_sample();// 0x7fff * tejeezfilt(saw(t * 123.0));
		//time++;
		buf[2*i] = sample;
		buf[2*i+1] = sample;
	}
	GPIO_ResetBits(GPIOD, GPIO_Pin_14);
}

volatile int dodump;

int accelinit(void);
void accelread(uint8_t*);

int main(void) {
	init();
	int volume = 0;

	InitializeAudio(Audio48000HzSettings);
	adc_init();

	synth_init();
	seq_init();

	accelinit();

	SetAudioVolume(0xCF);
	PlayAudioWithCallback(AudioCallback, 0);
	fillbuf(audio_buffer[0]);

	for(;;) {
		/*
		 * Check if user button is pressed
		 */
		//printf("* %d  %d\r\n", adc_read1(), adc_read2());
		int8_t acce[3];
		accelread((uint8_t*)acce);
		synth_setvolume((50 - abs(acce[1])) / 50.0);
		synth_setparams(adc_read1(), 0);
		synth_setparams(adc_read2(), 1);
		//printf("%+03d %+03d %+03d\r\n", acce[0], acce[1], acce[2]);
		if (BUTTON) {
			// Debounce
			Delay(10);
			if (BUTTON) {

				synth_kill();
				seq_init();

				// Toggle audio volume
				if (volume) {
					volume = 0;
					SetAudioVolume(0xCF);
				} else {
					volume = 1;
					SetAudioVolume(0xfF);
				}

				while(BUTTON){};
			}
		}
		if (dodump) {
			dodump = 0;
			synth_dump();
		}
		if (buf_consumed) {
			int buf = nextbuf;
			buf_consumed = 0;
			fillbuf(audio_buffer[buf]);
		}
	}

	return 0;
}

/*
 * Called by the audio driver when it is time to provide data to
 * one of the audio buffers (while the other buffer is sent to the
 * CODEC using DMA). One mp3 frame is decoded at a time and
 * provided to the audio driver.
 */
static void AudioCallback(void *context, int buffer) {
#if 0
	if (buffer) {
		GPIO_SetBits(GPIOD, GPIO_Pin_13);
		//GPIO_ResetBits(GPIOD, GPIO_Pin_14);
	} else {
		//GPIO_SetBits(GPIOD, GPIO_Pin_14);
		GPIO_ResetBits(GPIOD, GPIO_Pin_13);
	}
#else
	GPIO_SetBits(GPIOD, GPIO_Pin_13);
#endif

#if 0
	nextbuf = 1 - buffer;
	buf_consumed = 1;
#else
	fillbuf(audio_buffer[buffer]);
#endif

	//fillbuf(audio_buffer[buffer]);
	ProvideAudioBuffer(audio_buffer[buffer], AUDIOBUFSIZE);
#if 1
	GPIO_ResetBits(GPIOD, GPIO_Pin_13);
#endif
}

void init() {
	GPIO_InitTypeDef  GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	// ---------- SysTick timer -------- //
	if (SysTick_Config(SystemCoreClock / 1000)) {
		// Capture error
		while (1){};
	}

	// Enable full access to FPU (Should be done automatically in system_stm32f4xx.c):
	//SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));  // set CP10 and CP11 Full Access

	// GPIOD Periph clock enable
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

	// Configure PD12, PD13, PD14 and PD15 in output pushpull mode
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13| GPIO_Pin_14| GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIO_InitStructure);


	// ------ UART ------ //

	// Clock
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

	// IO
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; // pushpull
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	GPIO_PinAFConfig(GPIOD, GPIO_PinSource5, GPIO_AF_USART2);
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource6, GPIO_AF_USART2);

	// Conf
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_Init(USART2, &USART_InitStructure);

	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
	
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	// Enable
	USART_Cmd(USART2, ENABLE);
}

static int seqtime, seqenabled;

void USART2_IRQHandler(void) {
	static int channum, chanstate, note, notevel;
	if (USART_GetITStatus(USART2, USART_IT_RXNE)) {
		int t = USART2->DR;
#if 1
		if (isdigit(t)) {
			channum = t - '0';
		} else if (isupper(t)) {
			synth_note_on(t - 'A' + 42, channum, 1.0);
			if (seqenabled)
				seq_add_event2(seqtime, channum, SEQ_EVTYPE_KEYON, t - 'A' + 42, 1.0);
			dodump = 1;
		} else if (islower(t)) {
			synth_note_off(t - 'a' + 42, channum);
			if (seqenabled)
				seq_add_event(seqtime, channum, SEQ_EVTYPE_KEYOFF, t - 'a' + 42);
			dodump = 1;
		} else if (t == ' ') {
			seqenabled = !seqenabled;
		}
#else
		if (t & 0x80) {
			channum = t & 0xf;
			chanstate = t & 0x40;
		} else if ((t & 0x40) && (t & 0x20)) {
			note = (t & 0xf) << 4;
		} else if ((t & 0x40) && (t & 0x10)) {
			note |= (t & 0xf);
		} else if (t & 0x20) {
			notevel = (t & 0xf) << 4;
		} else if (t & 0x10) {
			notevel |= (t & 0xf);
			if (chanstate)
				synth_note_on(note,channum,notevel / 127.0);
			else
				synth_note_off(note,channum);
		}
#endif
	}
}

void seqplay(int tick) {
	struct seqevent *ev = seq_events_at(tick);
	int n = 0;
	while (ev) {
		switch (ev->type) {
		case SEQ_EVTYPE_KEYON:
			//printf("on  %d %d\r\n", ev->param1, ev->instrument);
			synth_note_on(ev->param1, ev->instrument, ev->param2);
			break;
		case SEQ_EVTYPE_KEYOFF:
			//printf("off %d %d\r\n", ev->param1, ev->instrument);
			synth_note_off(ev->param1, ev->instrument);
			break;
		}
		ev = ev->next;
		n++;
	}
	//printf("evs %d\r\n", n);
}
/*
 * Called from systick handler
 */
void timing_handler() {
	extern __IO uint32_t TimingDelay;
	static int prescale, pre2, tik;
	if (prescale++ == 1000/16) {
		prescale = 0;
		if (pre2++ == 16) {
			pre2 = 0;
			printf("ping %d\r\n", tik);
			if (++tik == 8) {
				tik = 0;
				printf("******** RESTART *********\r\n");
			}
		}

		seqtime++;
		if (seqenabled)
			seqplay(seqtime);
	}
	if(TimingDelay)TimingDelay--;
	if (time_var1) {
		time_var1--;
	}

	time_var2++;
}

/*
 * Delay a number of systick cycles
 */
void Delay(volatile uint32_t nCount) {
	time_var1 = nCount;

	while(time_var1){};
}

/*
 * Dummy function to avoid compiler error
 */
void _init() {

}
