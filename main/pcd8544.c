/*
 * pcd8544.c
 *
 *  PCD8544 bit-banging driver ported from http://playground.arduino.cc/Code/PCD8544
 *
 *  Created on: Jan 7, 2015
 *      Author: Eadf
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_system.h"
#include "pcd8544.h"

static const char *TAG = "pcd8544";

// These default pin definitions can be changed to any valid GPIO pin.
// The code in PCD8544_init() should automagically adapt

#define PCD8544_RST_PIN 16  // reset
#define PCD8544_CE_PIN 14   // chip enable
#define PCD8544_DC_PIN 13   // Data/command selection
#define PCD8544_SCLK_PIN 12 // clk output
#define PCD8544_SDIN_PIN 15 // serial output
#define PCD8544_LED_PIN 2   // led background

#define delay_us ets_delay_us

#define GPIO_OUTPUT_PIN_SEL ((1ULL << PCD8544_RST_PIN) | (1ULL << PCD8544_CE_PIN) | (1ULL << PCD8544_DC_PIN) | (1ULL << PCD8544_SCLK_PIN) | (1ULL << PCD8544_SDIN_PIN) | (1ULL << PCD8544_LED_PIN))

#define Set_RST_Pin(x) gpio_set_level(PCD8544_RST_PIN, x)
#define Set_CE_Pin(x) gpio_set_level(PCD8544_CE_PIN, x)
#define Set_DC_Pin(x) gpio_set_level(PCD8544_DC_PIN, x)
#define Set_SCLK_Pin(x) gpio_set_level(PCD8544_SCLK_PIN, x)
#define Set_SDIN_Pin(x) gpio_set_level(PCD8544_SDIN_PIN, x)
#define Set_LED_Pin(x) gpio_set_level(PCD8544_LED_PIN, x)

#define PCD8544_FUNCTIONSET 0x20
#define PCD8544_SETVOP 0x80
#define PCD8544_EXTENDEDINSTRUCTION 0x01

#define LOW 0
#define HIGH 1
#define LCD_CMD LOW
#define LCD_DATA HIGH

#define LCD_X 84
#define LCD_Y 48

#define CLOCK_HIGH_TIME 14 // 14 us

static void PCD8544_lcdWrite8(bool dc, uint8_t data);
static void PCD8544_shiftOut8(bool msbFirst, uint8_t data);

static const uint8_t ASCII[][5] =
    {
        {0x00, 0x00, 0x00, 0x00, 0x00} // 20
        ,
        {0x00, 0x00, 0x5f, 0x00, 0x00} // 21 !
        ,
        {0x00, 0x07, 0x00, 0x07, 0x00} // 22 "
        ,
        {0x14, 0x7f, 0x14, 0x7f, 0x14} // 23 #
        ,
        {0x24, 0x2a, 0x7f, 0x2a, 0x12} // 24 $
        ,
        {0x23, 0x13, 0x08, 0x64, 0x62} // 25 %
        ,
        {0x36, 0x49, 0x55, 0x22, 0x50} // 26 &
        ,
        {0x00, 0x05, 0x03, 0x00, 0x00} // 27 '
        ,
        {0x00, 0x1c, 0x22, 0x41, 0x00} // 28 (
        ,
        {0x00, 0x41, 0x22, 0x1c, 0x00} // 29 )
        ,
        {0x14, 0x08, 0x3e, 0x08, 0x14} // 2a *
        ,
        {0x08, 0x08, 0x3e, 0x08, 0x08} // 2b +
        ,
        {0x00, 0x50, 0x30, 0x00, 0x00} // 2c ,
        ,
        {0x08, 0x08, 0x08, 0x08, 0x08} // 2d -
        ,
        {0x00, 0x60, 0x60, 0x00, 0x00} // 2e .
        ,
        {0x20, 0x10, 0x08, 0x04, 0x02} // 2f /
        ,
        {0x3e, 0x51, 0x49, 0x45, 0x3e} // 30 0
        ,
        {0x00, 0x42, 0x7f, 0x40, 0x00} // 31 1
        ,
        {0x42, 0x61, 0x51, 0x49, 0x46} // 32 2
        ,
        {0x21, 0x41, 0x45, 0x4b, 0x31} // 33 3
        ,
        {0x18, 0x14, 0x12, 0x7f, 0x10} // 34 4
        ,
        {0x27, 0x45, 0x45, 0x45, 0x39} // 35 5
        ,
        {0x3c, 0x4a, 0x49, 0x49, 0x30} // 36 6
        ,
        {0x01, 0x71, 0x09, 0x05, 0x03} // 37 7
        ,
        {0x36, 0x49, 0x49, 0x49, 0x36} // 38 8
        ,
        {0x06, 0x49, 0x49, 0x29, 0x1e} // 39 9
        ,
        {0x00, 0x36, 0x36, 0x00, 0x00} // 3a :
        ,
        {0x00, 0x56, 0x36, 0x00, 0x00} // 3b ;
        ,
        {0x08, 0x14, 0x22, 0x41, 0x00} // 3c <
        ,
        {0x14, 0x14, 0x14, 0x14, 0x14} // 3d =
        ,
        {0x00, 0x41, 0x22, 0x14, 0x08} // 3e >
        ,
        {0x02, 0x01, 0x51, 0x09, 0x06} // 3f ?
        ,
        {0x32, 0x49, 0x79, 0x41, 0x3e} // 40 @
        ,
        {0x7e, 0x11, 0x11, 0x11, 0x7e} // 41 A
        ,
        {0x7f, 0x49, 0x49, 0x49, 0x36} // 42 B
        ,
        {0x3e, 0x41, 0x41, 0x41, 0x22} // 43 C
        ,
        {0x7f, 0x41, 0x41, 0x22, 0x1c} // 44 D
        ,
        {0x7f, 0x49, 0x49, 0x49, 0x41} // 45 E
        ,
        {0x7f, 0x09, 0x09, 0x09, 0x01} // 46 F
        ,
        {0x3e, 0x41, 0x49, 0x49, 0x7a} // 47 G
        ,
        {0x7f, 0x08, 0x08, 0x08, 0x7f} // 48 H
        ,
        {0x00, 0x41, 0x7f, 0x41, 0x00} // 49 I
        ,
        {0x20, 0x40, 0x41, 0x3f, 0x01} // 4a J
        ,
        {0x7f, 0x08, 0x14, 0x22, 0x41} // 4b K
        ,
        {0x7f, 0x40, 0x40, 0x40, 0x40} // 4c L
        ,
        {0x7f, 0x02, 0x0c, 0x02, 0x7f} // 4d M
        ,
        {0x7f, 0x04, 0x08, 0x10, 0x7f} // 4e N
        ,
        {0x3e, 0x41, 0x41, 0x41, 0x3e} // 4f O
        ,
        {0x7f, 0x09, 0x09, 0x09, 0x06} // 50 P
        ,
        {0x3e, 0x41, 0x51, 0x21, 0x5e} // 51 Q
        ,
        {0x7f, 0x09, 0x19, 0x29, 0x46} // 52 R
        ,
        {0x46, 0x49, 0x49, 0x49, 0x31} // 53 S
        ,
        {0x01, 0x01, 0x7f, 0x01, 0x01} // 54 T
        ,
        {0x3f, 0x40, 0x40, 0x40, 0x3f} // 55 U
        ,
        {0x1f, 0x20, 0x40, 0x20, 0x1f} // 56 V
        ,
        {0x3f, 0x40, 0x38, 0x40, 0x3f} // 57 W
        ,
        {0x63, 0x14, 0x08, 0x14, 0x63} // 58 X
        ,
        {0x07, 0x08, 0x70, 0x08, 0x07} // 59 Y
        ,
        {0x61, 0x51, 0x49, 0x45, 0x43} // 5a Z
        ,
        {0x00, 0x7f, 0x41, 0x41, 0x00} // 5b [
        ,
        {0x02, 0x04, 0x08, 0x10, 0x20} // 5c ¥
        ,
        {0x00, 0x41, 0x41, 0x7f, 0x00} // 5d ]
        ,
        {0x04, 0x02, 0x01, 0x02, 0x04} // 5e ^
        ,
        {0x40, 0x40, 0x40, 0x40, 0x40} // 5f _
        ,
        {0x00, 0x01, 0x02, 0x04, 0x00} // 60 `
        ,
        {0x20, 0x54, 0x54, 0x54, 0x78} // 61 a
        ,
        {0x7f, 0x48, 0x44, 0x44, 0x38} // 62 b
        ,
        {0x38, 0x44, 0x44, 0x44, 0x20} // 63 c
        ,
        {0x38, 0x44, 0x44, 0x48, 0x7f} // 64 d
        ,
        {0x38, 0x54, 0x54, 0x54, 0x18} // 65 e
        ,
        {0x08, 0x7e, 0x09, 0x01, 0x02} // 66 f
        ,
        {0x0c, 0x52, 0x52, 0x52, 0x3e} // 67 g
        ,
        {0x7f, 0x08, 0x04, 0x04, 0x78} // 68 h
        ,
        {0x00, 0x44, 0x7d, 0x40, 0x00} // 69 i
        ,
        {0x20, 0x40, 0x44, 0x3d, 0x00} // 6a j
        ,
        {0x7f, 0x10, 0x28, 0x44, 0x00} // 6b k
        ,
        {0x00, 0x41, 0x7f, 0x40, 0x00} // 6c l
        ,
        {0x7c, 0x04, 0x18, 0x04, 0x78} // 6d m
        ,
        {0x7c, 0x08, 0x04, 0x04, 0x78} // 6e n
        ,
        {0x38, 0x44, 0x44, 0x44, 0x38} // 6f o
        ,
        {0x7c, 0x14, 0x14, 0x14, 0x08} // 70 p
        ,
        {0x08, 0x14, 0x14, 0x18, 0x7c} // 71 q
        ,
        {0x7c, 0x08, 0x04, 0x04, 0x08} // 72 r
        ,
        {0x48, 0x54, 0x54, 0x54, 0x20} // 73 s
        ,
        {0x04, 0x3f, 0x44, 0x40, 0x20} // 74 t
        ,
        {0x3c, 0x40, 0x40, 0x20, 0x7c} // 75 u
        ,
        {0x1c, 0x20, 0x40, 0x20, 0x1c} // 76 v
        ,
        {0x3c, 0x40, 0x30, 0x40, 0x3c} // 77 w
        ,
        {0x44, 0x28, 0x10, 0x28, 0x44} // 78 x
        ,
        {0x0c, 0x50, 0x50, 0x50, 0x3c} // 79 y
        ,
        {0x44, 0x64, 0x54, 0x4c, 0x44} // 7a z
        ,
        {0x00, 0x08, 0x36, 0x41, 0x00} // 7b {
        ,
        {0x00, 0x00, 0x7f, 0x00, 0x00} // 7c |
        ,
        {0x00, 0x41, 0x36, 0x08, 0x00} // 7d }
        ,
        {0x10, 0x08, 0x08, 0x10, 0x08} // 7e ←
        ,
        {0x00, 0x06, 0x09, 0x09, 0x06} // 7f →
};

// void
// PCD8544_printBinary(uint32_t data)
// {
//   int i = 0;
//   for (i = 8 * sizeof(uint32_t) - 1; i >= 0; i--)
//   {
//     if (i > 1 && (i + 1) % 4 == 0)
//       os_printf(" ");
//     if (data & 1 << i)
//     {
//       os_printf("1");
//     }
//     else
//     {
//       os_printf("0");
//     }
//   }
// }

// PCD8544_gotoXY routine to position cursor
// x - range: 0 to 84
// y - range: 0 to 5
void PCD8544_gotoXY(int x, int y)
{
  PCD8544_lcdWrite8(LCD_CMD, 0x80 | x); // Column.
  PCD8544_lcdWrite8(LCD_CMD, 0x40 | y); // Row.
}

void PCD8544_lcdCharacter(char character)
{
  if (character == '\n' || character == '\r' || character == '\t')
  {
    // ignore these for now
    return;
  }
  PCD8544_lcdWrite8(LCD_DATA, 0x00);
  if (character < 0x20 || character > 0x7f)
  {
    character = '?';
  }
  int index = 0;
  for (; index < 5; index++)
  {
    PCD8544_lcdWrite8(LCD_DATA, ASCII[character - 0x20][index]);
  }
  PCD8544_lcdWrite8(LCD_DATA, 0x00);
}

void PCD8544_lcdPrint(char *characters)
{
  while (*characters)
  {
    PCD8544_lcdCharacter(*characters++);
  }
}

/**
 * print ' ' a number of times
 */
void PCD8544_lcdPad(int16_t spaces)
{
  uint8_t i = 0;
  //os_printf("PCD8544_lcdPad: padding %d spaces\n", spaces);
  for (i = 0; i < spaces; i++)
  {
    PCD8544_lcdCharacter(' ');
  }
}

void PCD8544_lcdClear(void)
{
  int index = 0;
  PCD8544_gotoXY(0, 0);
  for (; index < LCD_X * LCD_Y / 8; index++)
  {
    PCD8544_lcdWrite8(LCD_DATA, 0x00);
  }
}

void PCD8544_lcdImage(uint8_t *image)
{
  int index = 0;
  PCD8544_gotoXY(0, 0);
  for (; index < LCD_X * LCD_Y / 8; index++)
  {
    PCD8544_lcdWrite8(LCD_DATA, image[index]);
  }
}

static void
PCD8544_shiftOut8(bool msbFirst, uint8_t data)
{
  bool dataBit = false;
  int bit = 7; // byte indexes
  if (msbFirst)
  {
    for (bit = 7; bit >= 0; bit--)
    {
      dataBit = (data >> bit) & 1;
      delay_us(CLOCK_HIGH_TIME);
      Set_SCLK_Pin(0);
      Set_SDIN_Pin(dataBit);
      delay_us(CLOCK_HIGH_TIME);
      Set_SCLK_Pin(1);
      delay_us(CLOCK_HIGH_TIME);
      Set_SCLK_Pin(0);
    }
  }
  else
  {
    for (bit = 0; bit <= 7; bit++)
    {
      dataBit = (data >> bit) & 1;
      delay_us(CLOCK_HIGH_TIME);
      Set_SCLK_Pin(0);
      Set_SDIN_Pin(dataBit);
      delay_us(CLOCK_HIGH_TIME);
      Set_SCLK_Pin(1);
      delay_us(CLOCK_HIGH_TIME);
      Set_SCLK_Pin(0);
    }
  }
  delay_us(2 * CLOCK_HIGH_TIME);
}

static void
PCD8544_lcdWrite8(bool dc, uint8_t data)
{
  Set_DC_Pin(dc);
  Set_CE_Pin(LOW);
  PCD8544_shiftOut8(true, data);
  Set_CE_Pin(HIGH);
  delay_us(CLOCK_HIGH_TIME);
}

/**
 * draws a box
 */
void PCD8544_drawLine(void)
{
  uint8_t j;
  for (j = 0; j < 84; j++)
  { // top
    PCD8544_gotoXY(j, 0);
    PCD8544_lcdWrite8(LCD_DATA, 0x01);
  }
  for (j = 0; j < 84; j++)
  { //Bottom

    PCD8544_gotoXY(j, 5);
    PCD8544_lcdWrite8(LCD_DATA, 0x80);
  }
  for (j = 0; j < 6; j++)
  { // Right

    PCD8544_gotoXY(83, j);
    PCD8544_lcdWrite8(LCD_DATA, 0xff);
  }
  for (j = 0; j < 6; j++)
  { // Left

    PCD8544_gotoXY(0, j);
    PCD8544_lcdWrite8(LCD_DATA, 0xff);
  }
}

/**
 * Sets the contrast [0x00 - 0x7f].
 * Useful, visible range is about 40-60.
 */
void PCD8544_setContrast(uint8_t contrast)
{
  if (contrast != 0)
  {
    contrast = 0x80 | (contrast & 0x7f);
  }
  PCD8544_lcdWrite8(LCD_CMD, PCD8544_FUNCTIONSET | PCD8544_EXTENDEDINSTRUCTION);
  PCD8544_lcdWrite8(LCD_CMD, PCD8544_SETVOP | contrast);
  PCD8544_lcdWrite8(LCD_CMD, PCD8544_FUNCTIONSET);
}

/**
 * Set up the GPIO pins and the rest of the environment
 */
void PCD8544_init(void)
{
  gpio_config_t io_conf;
  //disable interrupt
  io_conf.intr_type = GPIO_INTR_DISABLE;
  //set as output mode
  io_conf.mode = GPIO_MODE_OUTPUT;
  //bit mask of the pins that you want to set,e.g.GPIO15/16
  io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
  //disable pull-down mode
  io_conf.pull_down_en = 0;
  //disable pull-up mode
  io_conf.pull_up_en = 0;
  //configure GPIO with the given settings
  gpio_config(&io_conf);

  Set_RST_Pin(HIGH);
  Set_CE_Pin(HIGH);
  Set_DC_Pin(HIGH);
  Set_SDIN_Pin(LOW);
  Set_SCLK_Pin(LOW);
  Set_LED_Pin(HIGH);

  PCD8544_initLCD();
}

/**
 * initiate the LCD itself
 */
void PCD8544_initLCD(void)
{
  delay_us(10000);
  Set_RST_Pin(LOW);
  delay_us(CLOCK_HIGH_TIME * 3);
  Set_RST_Pin(HIGH);
  delay_us(10000);

  PCD8544_lcdWrite8(LCD_CMD, 0x21); // LCD Extended Commands.

  PCD8544_lcdWrite8(LCD_CMD, 0xB1); // Set LCD Vop (Contrast). //B1
  PCD8544_lcdWrite8(LCD_CMD, 0x04); // Set Temp coefficent. //0x04
  PCD8544_lcdWrite8(LCD_CMD, 0x14); // LCD bias mode 1:48. //0x13
  PCD8544_lcdWrite8(LCD_CMD, 0x0C); // LCD in normal mode. 0x0d for inverse

  PCD8544_lcdWrite8(LCD_CMD, 0x20);
  PCD8544_lcdWrite8(LCD_CMD, 0x0C);
  delay_us(100000);
  PCD8544_lcdClear();
  delay_us(10000);

  PCD8544_lcdPrint("hello world!!!");
}