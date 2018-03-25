/* includes */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/* globals */

struct termios orig_termios;

/* terminal */

void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("disableRawMode");
}

void enableRawMode() {
  struct termios raw = orig_termios;
  atexit(disableRawMode);

  if (tcgetattr(STDIN_FILENO, &raw) == -1)
    die("tcgetattr");

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 10;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

/* init */

int main() {
  enableRawMode();

  while(1) {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
      die("read");
    }

    if(iscntrl(c)) {
      printf("%d\x0D\n", c);
    }

    else {
      printf("(%d)%c\x0D\n", c, c);
    }

    if (c == 'q')
      break;
  }

  return 0;
}