/*
MIT License
Copyright (c) 2021 Klaus Zerbe
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

#ifndef _GETLINE_H
#define _GETLINE_H

#include <stdio.h>
#include <pico/stdlib.h>

const uint startLineLength = 8;
const char eof = 255;

/*
 *  read a line of any  length from stdio (grows)
 *
 *  @param fullDuplex input will echo on entry (terminal mode) when false
 *  @param linebreak defaults to "\n", but "\r" may be needed for terminals
 *  @return entered line on heap - don't forget calling free() to get memory back
 */
static char *getLine(bool fullDuplex, char lineBreak, size_t *length)
{
    // th line buffer
    // will allocated by pico_malloc module if <cstdlib> gets included
    char *pStart = (char *)malloc(startLineLength);
    char *pPos = pStart;             // next character position
    size_t maxLen = startLineLength; // current max buffer size
    size_t len = maxLen;             // current max length
    int c;
    size_t actualLen = 0;

    if (!pStart)
    {
        *length = 0;
        return NULL; // out of memory or dysfunctional heap
    }

    while (1)
    {
        c = getchar(); // expect next character entry
        if (c == eof || c == lineBreak)
        {
            break; // non blocking exit
        }

        if (fullDuplex)
        {
            printf("%c", c); // echo for fullDuplex terminals
        }

        if (--len == 0)
        { // allow larger buffer
            len = maxLen;
            int prevOffset = pPos - pStart;
            // double the current line buffer size
            char *pNew = (char *)realloc(pStart, maxLen *= 2);
            if (!pNew)
            {
                free(pStart);
                *length = 0;
                return NULL; // out of memory abort
            }
            // fix pointer for new buffer
            pPos = pNew + prevOffset;
            pStart = pNew;
        }

        // stop reading if lineBreak character entered
        if ((*pPos++ = c) == lineBreak)
        {
            break;
        }

        actualLen++;
    }

    *pPos = '\0'; // set string end mark
    *length = actualLen;
    return pStart;
}

/*
 *  read a line of any  length from stdio (grows)
 *
 *  @param fullDuplex input will echo on entry (terminal mode) when false
 *  @param linebreak defaults to "\n", but "\r" may be needed for terminals
 *  @return entered line on heap - don't forget calling free() to get memory back
 */
static char *waitForLine(bool fullDuplex, char lineBreak, size_t *length)
{
    char *pLine;
    do
    {
        pLine = getLine(fullDuplex, lineBreak, length);
    } while (!*pLine);
    return pLine;
}

#endif