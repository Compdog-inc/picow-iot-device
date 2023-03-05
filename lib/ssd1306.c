/*

MIT License

Copyright (c) 2021 David Schramm

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <pico/binary_info.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ssd1306.h"
#include "font.h"
#include "../utils/debug.h"

inline static void swap(int32_t *a, int32_t *b)
{
    F_START("swap");
    int32_t *t = a;
    *a = *b;
    *b = *t;
    F_END("swap");
}

inline static void fancy_write(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, char *name)
{
    F_START("fancy_write");
    switch (i2c_write_blocking(i2c, addr, src, len, false))
    {
    case PICO_ERROR_GENERIC:
        printf("[%s] addr not acknowledged!\n", name);
        break;
    case PICO_ERROR_TIMEOUT:
        printf("[%s] timeout!\n", name);
        break;
    default:
        // printf("[%s] wrote successfully %lu bytes!\n", name, len);
        break;
    }
    F_END("fancy_write");
}

inline static void ssd1306_write(ssd1306_t *p, uint8_t val)
{
    F_START("ssd1306_write");
    uint8_t d[2] = {0x00, val};
    fancy_write(p->i2c_i, p->address, d, 2, "ssd1306_write");
    F_END("ssd1306_write");
}

bool ssd1306_init(ssd1306_t *p, uint16_t width, uint16_t height, uint8_t address, i2c_inst_t *i2c_instance)
{
    F_START("ssd1306_init");
    p->width = width;
    p->height = height;
    p->pages = height / 8;
    p->address = address;

    p->i2c_i = i2c_instance;

    p->bufsize = (p->pages) * (p->width);
    if ((p->buffer = malloc(p->bufsize + 1)) == NULL)
    {
        p->bufsize = 0;
        F_RETURNV("ssd1306_init", false);
    }

    ++(p->buffer);

    // from https://github.com/makerportal/rpi-pico-ssd1306
    uint8_t cmds[] = {
        SET_DISP,
        // timing and driving scheme
        SET_DISP_CLK_DIV,
        0x80,
        SET_MUX_RATIO,
        height - 1,
        SET_DISP_OFFSET,
        0x00,
        // resolution and layout
        SET_DISP_START_LINE,
        // charge pump
        SET_CHARGE_PUMP,
        p->external_vcc ? 0x10 : 0x14,
        SET_SEG_REMAP | 0x01,   // column addr 127 mapped to SEG0
        SET_COM_OUT_DIR | 0x08, // scan from COM[N] to COM0
        SET_COM_PIN_CFG,
        width > 2 * height ? 0x02 : 0x12,
        // display
        SET_CONTRAST,
        0xff,
        SET_PRECHARGE,
        p->external_vcc ? 0x22 : 0xF1,
        SET_VCOM_DESEL,
        0x30,          // or 0x40?
        SET_ENTIRE_ON, // output follows RAM contents
        SET_NORM_INV,  // not inverted
        SET_DISP | 0x01,
        // address setting
        SET_MEM_ADDR,
        0x00, // horizontal
    };

    for (size_t i = 0; i < sizeof(cmds); ++i)
        ssd1306_write(p, cmds[i]);

    F_RETURNV("ssd1306_init", true);
}

inline void ssd1306_deinit(ssd1306_t *p)
{
    F_START("ssd1306_deinit");
    free(p->buffer - 1);
    F_END("ssd1306_deinit");
}

inline void ssd1306_poweroff(ssd1306_t *p)
{
    F_START("ssd1306_poweroff");
    ssd1306_write(p, SET_DISP | 0x00);
    F_END("ssd1306_poweroff");
}

inline void ssd1306_poweron(ssd1306_t *p)
{
    F_START("ssd1306_poweron");
    ssd1306_write(p, SET_DISP | 0x01);
    F_END("ssd1306_poweron");
}

inline void ssd1306_contrast(ssd1306_t *p, uint8_t val)
{
    F_START("ssd1306_contrast");
    ssd1306_write(p, SET_CONTRAST);
    ssd1306_write(p, val);
    F_END("ssd1306_contrast");
}

inline void ssd1306_invert(ssd1306_t *p, uint8_t inv)
{
    F_START("ssd1306_invert");
    ssd1306_write(p, SET_NORM_INV | (inv & 1));
    F_END("ssd1306_invert");
}

inline void ssd1306_clear(ssd1306_t *p)
{
    F_START("ssd1306_clear");
    memset(p->buffer, 0, p->bufsize);
    F_END("ssd1306_clear");
}

inline void ssd1306_fill(ssd1306_t *p)
{
    F_START("ssd1306_fill");
    memset(p->buffer, 0xff, p->bufsize);
    F_END("ssd1306_fill");
}

void ssd1306_draw_pixel(ssd1306_t *p, uint32_t x, uint32_t y)
{
    F_START("ssd1306_draw_pixel");
    if (x >= p->width || y >= p->height)
        F_RETURN("ssd1306_draw_pixel");

    p->buffer[x + p->width * (y >> 3)] |= 0x1 << (y & 0x07); // y>>3==y/8 && y&0x7==y%8
    F_END("ssd1306_draw_pixel");
}

void ssd1306_reset_pixel(ssd1306_t *p, uint32_t x, uint32_t y)
{
    F_START("ssd1306_reset_pixel");
    if (x >= p->width || y >= p->height)
        F_RETURN("ssd1306_reset_pixel");

    p->buffer[x + p->width * (y >> 3)] &= ~(0x1 << (y & 0x07)); // y>>3==y/8 && y&0x7==y%8
    F_END("ssd1306_reset_pixel");
}

void ssd1306_draw_line(ssd1306_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2, bool value)
{
    F_START("ssd1306_draw_line");
    if (x1 > x2)
    {
        swap(&x1, &x2);
        swap(&y1, &y2);
    }

    if (x1 == x2)
    {
        if (y1 > y2)
            swap(&y1, &y2);
        for (int32_t i = y1; i <= y2; ++i)
        {
            ssd1306_reset_pixel(p, x1, i);
            if (value)
                ssd1306_draw_pixel(p, x1, i);
        }
        F_RETURN("ssd1306_draw_line");
    }

    float m = (float)(y2 - y1) / (float)(x2 - x1);

    for (int32_t i = x1; i <= x2; ++i)
    {
        float y = m * (float)(i - x1) + (float)y1;
        ssd1306_reset_pixel(p, i, (uint32_t)y);
        if (value)
            ssd1306_draw_pixel(p, i, (uint32_t)y);
    }
    F_END("ssd1306_draw_line");
}

void ssd1306_draw_square(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height, bool value)
{
    F_START("ssd1306_draw_square");
    for (uint32_t i = 0; i < width; ++i)
        for (uint32_t j = 0; j < height; ++j)
        {
            ssd1306_reset_pixel(p, x + i, y + j);
            if (value)
                ssd1306_draw_pixel(p, x + i, y + j);
        }
    F_END("ssd1306_draw_square");
}

void ssd13606_draw_empty_square(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height, bool value)
{
    F_START("ssd13606_draw_empty_square");
    ssd1306_draw_line(p, x, y, x + width, y, value);
    ssd1306_draw_line(p, x, y + height, x + width, y + height, value);
    ssd1306_draw_line(p, x, y, x, y + height, value);
    ssd1306_draw_line(p, x + width, y, x + width, y + height, value);
    F_END("ssd13606_draw_empty_square");
}

ssd1306_char_measure ssd1306_measure_char(const uint8_t *font, char c)
{
    uint8_t charHeight = font[1];
    uint32_t parts_per_line = (font[1] >> 3) + ((font[1] & 7) > 0);
    bool monospace = !font[0];

    uint32_t charStart;
    if (monospace)
        charStart = (c - font[4]) * font[2] * parts_per_line + 6;
    else
        charStart = (c - font[4]) * (font[2] + 1) * parts_per_line + 7; // extra offset because of width byte

    uint8_t charWidth;
    if (monospace)
        charWidth = font[2];
    else
        charWidth = font[charStart - 1]; /* width is before the char data */

    ssd1306_char_measure measure = {
        .monospace = monospace,
        .parts_per_line = parts_per_line,
        .char_start = charStart,
        .char_width = charWidth,
        .char_height = charHeight};

    return measure;
}

void ssd1306_draw_char_with_font(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, char c, bool value)
{
    F_START("ssd1306_draw_char_with_font");
    if (c < font[4] || c > font[5])
        F_RETURN("ssd1306_draw_char_with_font");

    ssd1306_char_measure measure = ssd1306_measure_char(font, c);

    for (uint8_t w = 0; w < measure.char_width; ++w)
    { // width
        uint32_t pp = measure.char_start + w * measure.parts_per_line;

        for (uint32_t lp = 0; lp < measure.parts_per_line; ++lp)
        {
            uint8_t line = font[pp];

            for (int8_t j = 0; j < 8; ++j, line >>= 1)
            {
                if (line & 1)
                    ssd1306_draw_square(p, x + w * scale, y + ((lp << 3) + j) * scale, scale, scale, value);
            }

            ++pp;
        }
    }

    F_END("ssd1306_draw_char_with_font");
}

ssd1306_string_measure ssd1306_measure_string(const uint8_t *font, const char *s, uint32_t scale)
{
    bool monospace = false;
    uint32_t width = 0;
    uint32_t height = 0;
    while (*s)
    {
        ssd1306_char_measure measure = ssd1306_measure_char(font, *(s++));
        if (*s)
            width += (measure.char_width + font[3]) * scale; // char width + constant spacing
        else
            width += measure.char_width * scale; // just char width

        height = measure.char_height * scale; // scale to render size
        monospace = measure.monospace;
    }

    ssd1306_string_measure m = {
        .monospace = monospace,
        .width = width,
        .height = height};
    return m;
}

void ssd1306_draw_string_with_font(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, const char *s, bool value)
{
    F_START("ssd1306_draw_string_with_font");
    int32_t x_n = x;
    int32_t y_n = y;
    while (*s)
    {
        ssd1306_char_measure measure = ssd1306_measure_char(font, *s);
        ssd1306_draw_char_with_font(p, x_n, y_n, scale, font, *s, value);
        x_n += (measure.char_width + font[3]) * scale; // char width + constant spacing
        if (*s == '\n')                                // supports multiline text
        {
            x_n = x;
            y_n += (measure.char_height + 1) * scale;
        }
        s++;
    }
    F_END("ssd1306_draw_string_with_font");
}

void ssd1306_draw_char(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, char c, bool value)
{
    F_START("ssd1306_draw_char");
    ssd1306_draw_char_with_font(p, x, y, scale, font_8x5, c, value);
    F_END("ssd1306_draw_char");
}

void ssd1306_draw_string(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const char *s, bool value)
{
    F_START("ssd1306_draw_string");
    ssd1306_draw_string_with_font(p, x, y, scale, font_8x5, s, value);
    F_END("ssd1306_draw_string");
}

static inline uint32_t ssd1306_bmp_get_val(const uint8_t *data, const size_t offset, uint8_t size)
{
    F_START("ssd1306_bmp_get_val");
    switch (size)
    {
    case 1:
        F_RETURNV("ssd1306_bmp_get_val",
                  data[offset]);
    case 2:
        F_RETURNV("ssd1306_bmp_get_val",
                  data[offset] | (data[offset + 1] << 8));
    case 4:
        F_RETURNV("ssd1306_bmp_get_val",
                  data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24));
    default:
        __builtin_unreachable();
    }
    __builtin_unreachable();
    F_END("ssd1306_bmp_get_val");
}

void ssd1306_bmp_show_image_with_offset(ssd1306_t *p, const uint8_t *data, const long size, uint32_t x_offset, uint32_t y_offset, ssd1306_bmp_rotation_t rotation, bool value)
{
    F_START("ssd1306_bmp_show_image_with_offset");
    if (size < 54) // data smaller than header
        F_RETURN("ssd1306_bmp_show_image_with_offset");

    const uint32_t bfOffBits = ssd1306_bmp_get_val(data, 10, 4);
    const uint32_t biSize = ssd1306_bmp_get_val(data, 14, 4);
    const int32_t biWidth = (int32_t)ssd1306_bmp_get_val(data, 18, 4);
    const int32_t biHeight = (int32_t)ssd1306_bmp_get_val(data, 22, 4);
    const uint16_t biBitCount = (uint16_t)ssd1306_bmp_get_val(data, 28, 2);
    const uint32_t biCompression = ssd1306_bmp_get_val(data, 30, 4);

    if (biBitCount != 1) // image not monochrome
        F_RETURN("ssd1306_bmp_show_image_with_offset");

    if (biCompression != 0) // image compressed
        F_RETURN("ssd1306_bmp_show_image_with_offset");

    const int table_start = 14 + biSize;
    uint8_t color_val;

    for (uint8_t i = 0; i < 2; ++i)
    {
        if (!((data[table_start + i * 4] << 16) | (data[table_start + i * 4 + 1] << 8) | data[table_start + i * 4 + 2]))
        {
            color_val = i;
            break;
        }
    }

    uint32_t bytes_per_line = (biWidth / 8) + (biWidth & 7 ? 1 : 0);
    if (bytes_per_line & 3)
        bytes_per_line = (bytes_per_line ^ (bytes_per_line & 3)) + 4;

    const uint8_t *img_data = data + bfOffBits;

    int step = biHeight > 0 ? -1 : 1;
    int border = biHeight > 0 ? -1 : biHeight;
    for (uint32_t y = biHeight > 0 ? biHeight - 1 : 0; y != border; y += step)
    {
        for (uint32_t x = 0; x < biWidth; ++x)
        {
            if (((img_data[x >> 3] >> (7 - (x & 7))) & 1) == color_val)
            {
                switch (rotation)
                {
                case ROTATE_NONE:
                    ssd1306_reset_pixel(p, x_offset + x, y_offset + y);
                    if (value)
                        ssd1306_draw_pixel(p, x_offset + x, y_offset + y);
                    break;
                case ROTATE_90:
                    ssd1306_reset_pixel(p, x_offset + (biHeight - y), y_offset + x);
                    if (value)
                        ssd1306_draw_pixel(p, x_offset + (biHeight - y), y_offset + x);
                    break;
                case ROTATE_180:
                    ssd1306_reset_pixel(p, x_offset + (biWidth - x), y_offset + (biHeight - y));
                    if (value)
                        ssd1306_draw_pixel(p, x_offset + (biWidth - x), y_offset + (biHeight - y));
                    break;
                case ROTATE_270:
                    ssd1306_reset_pixel(p, x_offset + y, y_offset + (biWidth - x));
                    if (value)
                        ssd1306_draw_pixel(p, x_offset + y, y_offset + (biWidth - x));
                    break;
                }
            }
        }
        img_data += bytes_per_line;
    }

    F_END("ssd1306_bmp_show_image_with_offset");
}

inline void ssd1306_bmp_show_image(ssd1306_t *p, const uint8_t *data, const long size, bool value)
{
    F_START("ssd1306_bmp_show_image");
    ssd1306_bmp_show_image_with_offset(p, data, size, 0, 0, ROTATE_NONE, value);
    F_END("ssd1306_bmp_show_image");
}

void ssd1306_draw_badge_5x5(ssd1306_t *p, uint32_t x_offset, uint32_t y_offset, const char *s, bool value)
{
    F_START("ssd1306_draw_badge_5x5");
    ssd1306_draw_square(p, x_offset, y_offset, 5, 5, !value);
    ssd1306_draw_string(p, x_offset, y_offset, 1, s, value);
    F_END("ssd1306_draw_badge_5x5");
}

void ssd1306_clear_status_icon_area(ssd1306_t *p, ssd1306_status_icon icon, uint32_t l_outset, uint32_t t_outset, uint32_t r_outset, uint32_t b_outset)
{
    F_START("ssd1306_clear_status_icon_area");
    ssd1306_draw_square(p, icon.x_offset - l_outset, icon.y_offset - t_outset, icon.width + l_outset + r_outset, icon.height + t_outset + b_outset, !icon.value);
    F_END("ssd1306_clear_status_icon_area");
}

void ssd1306_clear_status_icon_array_area(ssd1306_t *p, ssd1306_status_icon_array icons, uint32_t l_outset, uint32_t t_outset, uint32_t r_outset, uint32_t b_outset)
{
    F_START("ssd1306_clear_status_icon_array_area");
    ssd1306_draw_square(p, icons.x_offset - l_outset, icons.y_offset - t_outset, icons.width + l_outset + r_outset, icons.height + t_outset + b_outset, !icons.value);
    F_END("ssd1306_clear_status_icon_array_area");
}

void ssd1306_draw_status_icon(ssd1306_t *p, ssd1306_status_icon icon)
{
    F_START("ssd1306_draw_status_icon");
    ssd1306_clear_status_icon_area(p, icon, 0, 0, 0, 0);
    ssd1306_draw_status_icon_overlay(p, icon);
    F_END("ssd1306_draw_status_icon");
}

void ssd1306_draw_status_icon_overlay(ssd1306_t *p, ssd1306_status_icon icon)
{
    F_START("ssd1306_draw_status_icon_overlay");
    ssd1306_bmp_show_image_with_offset(p, icon.data, icon.size, icon.x_offset, icon.y_offset, ROTATE_NONE, icon.value);
    F_END("ssd1306_draw_status_icon_overlay");
}

void ssd1306_draw_status_icon_with_badge(ssd1306_t *p, ssd1306_status_icon icon, const char *s)
{
    F_START("ssd1306_draw_status_icon_with_badge");
    ssd1306_clear_status_icon_area(p, icon, 0, 0, 4, 3);
    ssd1306_draw_status_icon_with_badge_overlay(p, icon, s);
    F_END("ssd1306_draw_status_icon_with_badge");
}

void ssd1306_draw_status_icon_with_badge_overlay(ssd1306_t *p, ssd1306_status_icon icon, const char *s)
{
    F_START("ssd1306_draw_status_icon_with_badge_overlay");
    ssd1306_draw_status_icon_overlay(p, icon);
    ssd1306_draw_badge_5x5(p, icon.x_offset + icon.width - 4, icon.y_offset + icon.height - 4, s, icon.value);
    F_END("ssd1306_draw_status_icon_with_badge_overlay");
}

void ssd1306_draw_status_icon_array(ssd1306_t *p, ssd1306_status_icon_array icons, size_t index)
{
    F_START("ssd1306_draw_status_icon_array");
    ssd1306_clear_status_icon_array_area(p, icons, 0, 0, 0, 0);
    ssd1306_draw_status_icon_array_overlay(p, icons, index, ROTATE_NONE);
    F_END("ssd1306_draw_status_icon_array");
}

void ssd1306_draw_status_icon_array_overlay(ssd1306_t *p, ssd1306_status_icon_array icons, size_t index, ssd1306_bmp_rotation_t rotation)
{
    F_START("ssd1306_draw_status_icon_array_overlay");
    ssd1306_bmp_show_image_with_offset(p, icons.data[index], icons.size[index], icons.x_offset, icons.y_offset, rotation, icons.value);
    F_END("ssd1306_draw_status_icon_array_overlay");
}

void ssd1306_draw_status_icon_array_with_badge(ssd1306_t *p, ssd1306_status_icon_array icons, size_t index, const char *s)
{
    F_START("ssd1306_draw_status_icon_array_with_badge");
    ssd1306_clear_status_icon_array_area(p, icons, 0, 0, 4, 3);
    ssd1306_draw_status_icon_array_with_badge_overlay(p, icons, index, s);
    F_END("ssd1306_draw_status_icon_array_with_badge");
}

void ssd1306_draw_status_icon_array_with_badge_overlay(ssd1306_t *p, ssd1306_status_icon_array icons, size_t index, const char *s)
{
    F_START("ssd1306_draw_status_icon_array_with_badge_overlay");
    ssd1306_draw_status_icon_array_overlay(p, icons, index, ROTATE_NONE);
    ssd1306_draw_badge_5x5(p, icons.x_offset + icons.width - 4, icons.y_offset + icons.height - 4, s, icons.value);
    F_END("ssd1306_draw_status_icon_array_with_badge_overlay");
}

void ssd1306_show(ssd1306_t *p)
{
    F_START("ssd1306_show");
    uint8_t payload[] = {SET_COL_ADDR, 0, p->width - 1, SET_PAGE_ADDR, 0, p->pages - 1};
    if (p->width == 64)
    {
        payload[1] += 32;
        payload[2] += 32;
    }

    for (size_t i = 0; i < sizeof(payload); ++i)
        ssd1306_write(p, payload[i]);

    *(p->buffer - 1) = 0x40;

    fancy_write(p->i2c_i, p->address, p->buffer - 1, p->bufsize + 1, "ssd1306_show");
    F_END("ssd1306_show");
}
