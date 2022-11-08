#ifndef SHELL_H
#define SHELL_H 1

void shell_init(void);
void shell_loop(void);

extern const struct cmdtable_s cmdtab[];

struct cmdtable_s {
    const char *name;
    const char *desc;
    void (*func)(uint8_t argc, const char **argv);
};

#endif
