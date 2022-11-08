#include <Arduino.h>
#include <Wire.h>
#include <stdbool.h>
#include "shell.h"

#define CMDBUF_SIZE_BYTES 128
#define MAXARGS 2

static uint8_t cmdbuf_len;
static char cmdbuf[CMDBUF_SIZE_BYTES+1];    // always null terminated
static bool got_line;

static void shell_prompt(void) {
    Serial.printf("> ");
}

static void shell_banner(void) {
    Serial.printf("\r\nLyuba\r\n");
}

void shell_init(void) {
    got_line = false;
    cmdbuf_len = 0;
    cmdbuf[cmdbuf_len] = 0;
    shell_banner();
    shell_prompt();
}

static bool shell_getch(uint8_t *c) {
    if (Serial.available()) {
        *c = Serial.read();
        return true;
    }

    return false;
}

static void shell_run_cmd(uint8_t argc, const char **argv) {
    uint8_t i = 0;

    if (0 == strcmp(argv[0], "help")) {
        while(NULL != cmdtab[i].name) {
            Serial.printf("%s %s\r\n", cmdtab[i].name, cmdtab[i].desc);
            i++;
        }
    } else {
        while(NULL != cmdtab[i].name) {
            if (0 == strcmp(argv[0], cmdtab[i].name)) {
                cmdtab[i].func(argc-1, argv+1);
                break;
            }
            i++;
        }
    }
}

static void shell_run_line(char *line) {
    uint8_t argc = 0;
    const char *argv[MAXARGS];
    char c;

    argv[argc++] = line;
    while((argc < MAXARGS) && (c = *line) != 0) {
        if (' ' == c) {   // separator
            *line = 0;
            argv[argc++] = line + 1;
        }
        line++;
    }
    shell_run_cmd(argc, argv);
}

void shell_loop(void) {
    uint8_t c;

    while(got_line == false && shell_getch(&c)) {
        if (got_line) { // throw away until line is handled
            return;
        }

        switch(c) { // handle characters
            case 0x0D: // \r
            case 0x0A: // \n
                got_line = true;
                Serial.print("\r\n");
            break;

            case '\b':  // backspace
            case 0x7F:  // del
                if (cmdbuf_len > 0) {
                    cmdbuf_len--;
                    cmdbuf[cmdbuf_len] = 0;
                    Serial.printf("\b \b"); // backspace overwrite
                }
            break;

            default:
                if (cmdbuf_len < CMDBUF_SIZE_BYTES) {
                    Serial.printf("%c", c); // echo
                    cmdbuf[cmdbuf_len++] = c;
                    cmdbuf[cmdbuf_len] = 0;
                } else {
                    Serial.printf("\a");    // bell
                }
            break;
        }
    }
    if (got_line) {
        if (cmdbuf_len > 0) { // non-empty
            shell_run_line(cmdbuf);
        }
        cmdbuf_len = 0;
        cmdbuf[cmdbuf_len] = 0;
        shell_prompt();
        got_line = false;
    }

}

