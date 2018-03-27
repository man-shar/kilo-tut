/* includes */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

/* defines */
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define KILO_VERSION "0.0.1"


/* structs */

struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct abuf {
  char *b;
  int len;
};

struct editorConfig E;

/* terminal */

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("disableRawMode");
}

void enableRawMode() {
  struct termios raw = E.orig_termios;
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

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if(nread == -1 && errno != EAGAIN) die("read");
  }

  return c;
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while(i < sizeof(buf) - 1) {
    if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if(buf[i] == 'R') break;
    i++;
  }

  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // if the terminal doesn't support TIOCGWINSZ
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;

    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/* buffer */

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if(new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}


/* output */

void editorDrawRows(struct abuf *ab) {
  for (int i = 0; i < E.screenrows; ++i) {
    // write(STDOUT_FILENO, "~", 1);

    if (i == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, 80, "Kilo editor --version %s", KILO_VERSION);

      if(welcomelen > E.screencols) welcomelen = E.screencols;

      int offset = (E.screencols - welcomelen) / 2;
      char offsetstr[offset];
      offset = snprintf(offsetstr, offset, "\x1B[31m\x1b[%dC", offset);

      abAppend(ab, offsetstr, offset);
      abAppend(ab, welcome, welcomelen);
      abAppend(ab, "\x1B[0m", 4);
    } else {
      abAppend(ab, "~", 1);
    }

    // erase line to right of cursor
    abAppend(ab, "\x1b[K0", 3);
    if (i < E.screenrows - 1) {
      // write(STDOUT_FILENO, "\r\n", 2);
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  // write(STDOUT_FILENO, "\x1b[2J", 4);
  // write(STDOUT_FILENO, "\x1b[H", 3);
  abAppend(&ab, "\x1b[?25h", 5);
  // abAppend(&ab, "\x1b[2J", 4);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  // write(STDOUT_FILENO, "\x1b[H", 3);
  abAppend(&ab, "\x1b[H", 3);
  abAppend(&ab, "\x1b[?25l", 5);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/* input */

void editorProcessKeyPress() {
  char c = editorReadKey();

  switch(c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
  }
}

/* init */

void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
  enableRawMode();
  initEditor();

  while(1) {
    editorRefreshScreen();
    editorProcessKeyPress();
  }

  return 0;
}