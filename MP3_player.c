#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <system.h>
#include <sys/alt_alarm.h>
#include <io.h>

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"

#include <altera_up_avalon_audio.h>
#include <altera_up_avalon_audio_and_video_config.h>

#include "board_diag.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"
#include <stdbool.h>

/*=========================================================================*/
/*  DEFINE: All Structures and Common Constants                            */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Macros                                                         */
/*=========================================================================*/

#define PSTR(_a)  _a

/*=========================================================================*/
/*  DEFINE: Definition of all local Data                                   */
/*=========================================================================*/

//static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer;   /* 1000Hz increment timer */

FATFS Fatfs[_VOLUMES];
FIL File1;
uint8_t Buff[8192] __attribute__ ((aligned(4)));
uint32_t ofs =0, cnt;
uint32_t s1, s2;
uint8_t res = 0;
char *ptr=' ';
DIR Dir;
FILINFO Finfo;
alt_up_audio_dev *audio_dev;
int songIndexVal = 0;
char filename1[20][20];
unsigned long fileSize1[20];
int numOfSongs = 0;
long counter = 0;
bool playPauseFlag=0;
bool stopFlag = 0;
bool nextFlag = 0;
bool prevFlag = 0;
//bool seekFlag = 0;
//bool rewindFlag = 0;

static void isr_routine(void* context, alt_u32 id)
{
	// If button 1 is asserted (play/pause), 1101 in binary will be the result of IORD or 13 in decimal
	if(IORD(BUTTON_PIO_BASE, 0) == 13)
	{
		playPauseFlag = !playPauseFlag;
		stopFlag = 0;
	}
	else if (IORD(BUTTON_PIO_BASE, 0) == 14)
	{
		nextFlag = 1;
	}
	else if (IORD(BUTTON_PIO_BASE, 0) == 11)
	{
		stopFlag = 1;
	}
	else if (IORD(BUTTON_PIO_BASE, 0) == 7)
	{
		prevFlag = 1;
	}

	IOWR(BUTTON_PIO_BASE, 3, 0);
	//clear the interrupt
}

static
void put_rc(FRESULT rc)
{
    const char *str =
        "OK\0" "DISK_ERR\0" "INT_ERR\0" "NOT_READY\0" "NO_FILE\0" "NO_PATH\0"
        "INVALID_NAME\0" "DENIED\0" "EXIST\0" "INVALID_OBJECT\0" "WRITE_PROTECTED\0"
        "INVALID_DRIVE\0" "NOT_ENABLED\0" "NO_FILE_SYSTEM\0" "MKFS_ABORTED\0" "TIMEOUT\0"
        "LOCKED\0" "NOT_ENOUGH_CORE\0" "TOO_MANY_OPEN_FILES\0";
    FRESULT i;

    for (i = 0; i != rc && *str; i++) {
        while (*str++);
    }
    xprintf("rc=%u FR_%s\n", (uint32_t) rc, str);
}

static void IoInit(void)
{
	// Open the Audio Port
	audio_dev = alt_up_audio_open_dev("/dev/Audio");
	if( audio_dev == NULL ) {
		alt_printf("Error: could not open audio device\n");
	} else {
		alt_printf("Opened audio device \n");
	}

    uart0_init(115200);

    /* Init diskio interface */
    ffs_DiskIOInit();

    // di disk initialize
    xprintf("rc=%d\n",disk_initialize((uint8_t) 0));

	// fi force initialize
	put_rc(f_mount((uint8_t) 0, &Fatfs[0]));

} /* IoInit */

int isWav(char *filename)
{
	int length = strlen(filename);
	if (filename[length-1] == 'V' && filename[length-2] == 'A' && filename[length-3] == 'W' && filename[length-4] == '.')
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
/*
void GetInputString( char* entry, int size, FILE * stream )
{
  int i;
  int ch = 0;

  for(i = 0; (ch != '\n') && (i < size); )
  {
    if( (ch = getc(stream)) != '\r')
    {
      entry[i] = ch;
      i++;
    }
  }
}
*/

void displayLCD(char* songName, int songNum)
{
	FILE *lcd;

	lcd = fopen("/dev/lcd_display", "w");

	/* Write some simple text to the LCD. */
	if (lcd != NULL )
	{
		fprintf(lcd, "%d\n", songNum + 1);
		fprintf(lcd, "%s\n", songName);
	}

	return;
}


void clearLCD()
{
	FILE *lcd;
	lcd = fopen("/dev/lcd_display", "w");
	fprintf(lcd, "%c%s", ESC, CLEAR_LCD_STRING);
    fclose( lcd );
	return;
}

int playaudio(long p1, char* ptr1)
{
	// Open file
    put_rc(f_open(&File1, ptr1, (uint8_t) 1));

    // Display on LCD

    // Play file
	ofs = File1.fptr;
	// p1 is the number of bytes to read from the file
	unsigned int l_buf = 0;
	unsigned int r_buf = 0;


	while (p1>0) {
		if ((uint32_t) p1 >= 512) {
			cnt = 512;
			p1 -= 512;
		} else {
			cnt = p1;
			p1 = 0;
		}
		res = f_read(&File1, Buff, cnt, &cnt);

		// Read and echo audio data
		int i;
		for(i = 0; i < 512; i = i+4) {

			r_buf = (uint32_t) (Buff[i+1] << 8 | Buff[i]);
			l_buf = (uint32_t) (Buff[i+3] << 8 | Buff[i+2]);

			while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_LEFT) == 0 || alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT) == 0);

			alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
			alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);

			// If the playPauseFlag is set (pause state), do nothing
			//play = 0
			//pause = 1
			while (playPauseFlag == 1)
			{
				if (nextFlag == 1)
				{
					nextFlag = 0;
					return(0);
				}

				else if (prevFlag == 1)
				{
					songIndexVal = songIndexVal - 2;
					prevFlag = 0;
					return(0);
				}
			}

			while (stopFlag == 1)
			{
				while(stopFlag == 1)
				{
					if (nextFlag == 1)
					{
						nextFlag = 0;
						return(0);
					}

					else if (prevFlag == 1)
					{
						songIndexVal = songIndexVal - 2;
						prevFlag = 0;
						return(0);
					}
				}
				playaudio(p1, ptr1);
				return(0);
			}

			// FAST FORWARD
			// If the nextFlag button is high and the track is playing...
			if(nextFlag == 1 && (playPauseFlag == 0 || stopFlag == 0))
			{
				// ... check to see that the button is still high and fast forward
				if (IORD(BUTTON_PIO_BASE, 0) == 14)
				{
					i = i + 4;
					continue;
				}
			}

			// REWIND
			// If the prevFlag button is high and the track is playing...
			else if(prevFlag == 1 && (playPauseFlag == 0 || stopFlag == 0))
			{
				// ... check to see that the button is still high and rewind
				if (IORD(BUTTON_PIO_BASE, 0) == 7)
				{
						i = i - 8;
						continue;
				}
			}

		}
	}
	return(0);
}

void songIndex(char fileName[20][20], unsigned long fileSize[20])
{
	//store directory listing
	res = f_opendir(&Dir, ptr);
	if (res) {
		put_rc(res);
		alt_printf("Error occur\n");
		// Error occurred
		return;
	}
	//p1 = s1 = s2 = 0;
	while(1) {
		res = f_readdir(&Dir, &Finfo);
		if (res != FR_OK || !Finfo.fname[0]) {
			xprintf("Number of valid songs: %d \n", numOfSongs);
			break;
		}
		//Check if Filename is a valid song
		if (isWav(Finfo.fname)) {
			strcpy(fileName[numOfSongs], &(Finfo.fname[0]));
			fileSize[numOfSongs] = Finfo.fsize;
			xprintf("%d %s\n", fileSize[numOfSongs], fileName[numOfSongs]);
			numOfSongs++;
		}


		//xprintf("%9lu  %s \n", Finfo.fsize, &(Finfo.fname[0]));
	}
}

int main()
{
	//char filename1[20][20];
	//unsigned long fileSize1[20];

	// Code for Interrupt
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, 0xF);
	alt_irq_register(BUTTON_PIO_IRQ, (void *) 0, isr_routine);
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0x0);

	IoInit();

	songIndex(filename1, fileSize1);

	while (songIndexVal < numOfSongs)
	{
		clearLCD();
		displayLCD(filename1[songIndexVal], songIndexVal);
		playaudio(fileSize1[songIndexVal], filename1[songIndexVal]);
		songIndexVal++;
		if(songIndexVal == numOfSongs)
		{
			songIndexVal = 0;
		}
	}

	return 0;
}
