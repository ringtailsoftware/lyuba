#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "linebuffer.h"

#include <Wire.h>
#include <Arduino.h>


int linebuffer_init(linebuffer_t *lb, size_t buf_len, linebuffer_per_line_func per_line_cb)
{
    lb->per_line_cb = per_line_cb;
    if (NULL == (lb->linebuf = (char *)malloc(buf_len+1)))
        goto fail;
    lb->userdata = NULL;
    lb->size = buf_len;
    linebuffer_reset(lb);
    return 0;
fail:
    return 1;
}

void linebuffer_reset(linebuffer_t *lb)
{
    lb->linebuf_index = 0;
    lb->linebuf[0] = 0;
}

void linebuffer_term(linebuffer_t *lb)
{
    if (NULL != lb->linebuf)
        free(lb->linebuf);
}

static int linebuffer_write_char(linebuffer_t *lb, char c)
{
    int rc = 1;
    if (c == '\n')
    {
        if (lb->linebuf_index < lb->size)
        {
            if (lb->linebuf_index && lb->linebuf[lb->linebuf_index-1] == '\r')
                lb->linebuf[lb->linebuf_index-1] = 0;
            lb->per_line_cb(lb, lb->linebuf, lb->userdata);
            linebuffer_reset(lb);
            rc = 0;
        }
        else
        {
            linebuffer_reset(lb);
        }
    }
    else
    if (lb->linebuf_index < lb->size)
    {
        if (isprint(c))
        {
            lb->linebuf[lb->linebuf_index++] = c;
            lb->linebuf[lb->linebuf_index] = 0;
        }
        rc = 0;
    }
    return rc;
}

int linebuffer_write(linebuffer_t *lb, const char *buf, size_t len)
{
    while(len--)
    {
        if (0 != linebuffer_write_char(lb, *buf++))
            return 1;
    }
    return 0;
}

void linebuffer_set_userdata(linebuffer_t *lb, void *userdata)
{
    lb->userdata = userdata;
}

void *linebuffer_get_userdata(linebuffer_t *lb)
{
    return lb->userdata;
}


