#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#include <stdarg.h>

static char digits[] = "0123456789ABCDEF";

static void
putc(int fd, char c)
{
  write(fd, &c, 1);
}

static void
printint(int fd, int xx, int base, int sgn, int width)
{
  char buf[16];
  int i, neg;
  uint x;

  neg = 0;
  if(sgn && xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0; //i记录了位数
  do{ //base是进制数
    // buf[i++] = digits[x % base];
    buf[i++] = "0123456789abcdef"[x % base]; //换成字符
  }while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  int num_d = i;
  int padding = width > num_d ? width - num_d : 0;

  while(padding-- > 0){
    putc(fd, ' ');
  }

  while(--i >= 0)
    putc(fd, buf[i]);
}

static void
printptr(int fd, uint64 x) {
  int i;
  putc(fd, '0');
  putc(fd, 'x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    putc(fd, digits[x >> (sizeof(uint64) * 8 - 4)]);
}


static void
printstr(int fd, char *s, int width)
{
  int len = 0;
  char *p = s;
  while(*p ++) len ++;

  int padding = width > len ? width - len : 0;
  while(padding-- > 0){
    putc(fd, ' '); //右对齐
  }

  while(*s){
    putc(fd, *s++);
  }
}


int
isdigit(int c)
{
  if(c >= '0' && c <= '9')
    return 1;
  return 0;
}
// Print to the given fd. Only understands %d, %x, %p, %s.
void
vprintf(int fd, const char *fmt, va_list ap)
{
  char *s;
  int c, i, state;
  int width = 0;
  state = 0;
  for(i = 0; fmt[i]; i++){
    c = fmt[i] & 0xff;
    if(state == 0){
      if(c == '%'){
        state = '%';
        width = 0;
      } else {
        putc(fd, c);
      }
    } else if(state == '%'){
      if(isdigit(c)){
        width = (c - '0') + width * 10;
        continue;
      }
      if(c == 'd'){
        printint(fd, va_arg(ap, int), 10, 1, width);
      } else if(c == 'l') {
        printint(fd, va_arg(ap, uint64), 10, 0, width);
      } else if(c == 'x') {
        printint(fd, va_arg(ap, int), 16, 0, width);
      } else if(c == 'p') {
        printptr(fd, va_arg(ap, uint64));
      } else if(c == 's'){
        s = va_arg(ap, char*);
        if(s == 0)
          s = "(null)";
        // while(*s != 0){
        //   putc(fd, *s);
        //   s++;
        // }
        printstr(fd, s, width);
      } else if(c == 'c'){
        putc(fd, va_arg(ap, uint));
      } else if(c == '%'){
        putc(fd, c);
      } else {
        // Unknown % sequence.  Print it to draw attention.
        putc(fd, '%');
        putc(fd, c);
      }
      state = 0;
      width = 0;
    }
  }
}

void
fprintf(int fd, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fd, fmt, ap);
}

void
printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(1, fmt, ap);
}
