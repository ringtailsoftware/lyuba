#ifndef LINEBUFFER_H
#define LINEBUFFER_H 1

struct linebuffer_s;

typedef int (*linebuffer_per_line_func)(struct linebuffer_s *lb, const char *buf, void *userdata);

struct linebuffer_s {
    char *linebuf;
    size_t size;
    size_t linebuf_index;
    void *userdata;
    linebuffer_per_line_func per_line_cb;
};
typedef struct linebuffer_s linebuffer_t;

int linebuffer_init(linebuffer_t *lb, size_t buf_len, linebuffer_per_line_func per_line_cb);
void linebuffer_reset(linebuffer_t *lb);
void linebuffer_term(linebuffer_t *lb);
int linebuffer_write(linebuffer_t *lb, const char *buf, size_t len);
void linebuffer_set_userdata(linebuffer_t *lb, void *userdata);
void *linebuffer_get_userdata(linebuffer_t *lb);
#endif


