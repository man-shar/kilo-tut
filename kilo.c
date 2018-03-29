/* includes */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>


/* defines */
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define KILO_VERSION "0.0.1"
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  END_KEY,
  HOME_KEY,
  PAGE_UP,
  PAGE_DOWN,
  DEL_KEY
};


/* structs */

typedef struct {
  int size;
  char *chars;
} erow;

struct editorConfig {
  // contain cursor positions **relative to file**. so essentially:
  // cy = cursor y position relative to terminal window + rowoff
  // cx = cursor x position relative to terminal window + coloff
  int cx;
  int cy;
  // contain terminal dimensions
  int screenrows;
  int screencols;
  // contains rows in the file
  int numrows;
  // owoff refers to what’s at the top of the screen in the file.
  int rowoff;
  // coloff refers to what's at the left of the screen in the file.
  int coloff;
  // points to the stored file
  erow *row;
  // original terminal attributes to restore atexit
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

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if(nread == -1 && errno != EAGAIN) die("read");
  }

  // check for escape sequences
  if (c == '\x1b') {
    char seq[3];

    if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if(seq[0] == '[') {
      if (seq[1] <= 9 && seq[1] >= 0) {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';

        if (seq[2] == '~')
        {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '7': return HOME_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '4': return END_KEY;
            case '8': return END_KEY;
            case '3': return DEL_KEY;
          }
        }
      }

      else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    }

    else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  }

  else {
    return c;
  }
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
  }

  else {
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
    // row of the file the user is currently scrolled to.
    int filerow = i + E.rowoff;

    if (filerow >= E.numrows) {
      if (E.numrows == 0 && i == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, 80, "Kilo editor --version %s", KILO_VERSION);

        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int offset = (E.screencols - welcomelen) / 2;

        /* CANT SIMPLY MOVE CURSOR HERE AS IT INTERFERES WITH CURSOR MOVEMENT VIA KEYS
        char offsetstr[offset];
        offset = snprintf(offsetstr, offset, "\x1b[%dC", offset);
        */

        if (offset) {
          abAppend(ab, "~", 1);
          offset--;
        }
        while (offset--) abAppend(ab, " ", 1);

        // red colour
        abAppend(ab, "\x1b[31m", 5);
        // append welcome string
        abAppend(ab, welcome, welcomelen);
        // back to default colour
        abAppend(ab, "\x1b[0m", 4);
      }

      else {
        abAppend(ab, "~", 1);
      }
    } else {
      // append from file.
      int len = E.row[filerow].size - E.coloff;
      if (len < 0) len = 0;
      if(len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].chars[E.coloff], len);
    }

    // erase line to right of cursor
    abAppend(ab, "\x1b[K0", 3);
    if (i < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

// to find where the user wants to be in the file. and save that to rowoff.
void editorScroll() {
  // vertical
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }

  // horizontal
  if (E.cx < E.coloff) {
    E.coloff = E.cx;
  }

  // as we said, cy now saves cursor positions **relative to file**. so essentially:
  // cy = cursor y position relative to terminal window + rowoff
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

  // as we said, cx now saves cursor positions **relative to file**. so essentially:
  // cx = cursor x position relative to terminal window + coloff
  if (E.cx >= E.coloff + E.screencols) {
    E.coloff = E.cx - E.screencols + 1;
  }
}

void editorRefreshScreen() {
  // since editorRefreshScreen is called before processing keypress, we can insert logic of 
  editorScroll();

  struct abuf ab = ABUF_INIT;

  // hide cursor
  abAppend(&ab, "\x1b[?25h", 5);
  // send cursor to 0, 0
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  // send cursor to cx and cy
  char movecursor[32];
  snprintf(movecursor, sizeof(movecursor), "\x1b[%d;%dH", (E.cy - E.rowoff + 1), (E.cx - E.coloff + 1));
  abAppend(&ab, movecursor, strlen(movecursor));

  // show cursor
  abAppend(&ab, "\x1b[?25l", 5);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/* input */

void editorMoveCursor(int key) {
  // to check right limit, get the current line.
  erow *row = (E.cy > E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;

  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0){
        // move to previous row
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      // now we test whether the cursor is past the end of the current line, if so, we limit it to one character past it.
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      // if y position is less than number of rows in file: scroll.
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:    
      E.cx = rowlen;
      break;
  }

  // check if the user's cursor is past the end of a line.
  // this can still happen if the user was on a long line with a short line below it, and presses arrow down when beyond the shorter line.
  // get new row
  row = (E.cy >E.numrows) ? NULL : &E.row[E.cy];
  rowlen = row ? row->size : 0;
  // check if cursor is past line.
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeyPress() {
  int c = editorReadKey();

  switch(c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case ARROW_UP:
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_RIGHT:
    case HOME_KEY:
    case END_KEY:
      editorMoveCursor(c);
      break;
  }
}

/* row operations */

void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int row_number = E.numrows;
  E.row[row_number].size = len;
  E.row[row_number].chars = malloc(len + 1);
  memcpy(E.row[row_number].chars, s, len);
  E.row[row_number].chars[len + 1] = '\0';
  E.numrows++;
}


/* file i/o */

void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if(!fp) die ("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    // strip off carriage returns as we are storing these in erow anyways.
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;

    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}

/* init */

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.numrows = 0;
  E.row = NULL;
  E.rowoff = 0;
  E.coloff = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char **argv) { 
  enableRawMode();
  initEditor();

  if(argc >= 2) {
    editorOpen(argv[1]);
  }

  while(1) {
    editorRefreshScreen();
    editorProcessKeyPress();
  }

  return 0;
}