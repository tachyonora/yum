// includes and defines
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#define YUM_VER "1.0"
#define TAB_STOP 4
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

enum textHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

void setStatusMessage(const char *fmt, ...);
void refreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));
int readKey();

// data
struct termios orig_termios;

typedef struct erow {
  int idx;
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
} erow;

struct editorConf {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax;
  struct termios orig_termios;
};

struct editorConf E;

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_coment_start;
  char *multiline_coment_start;
  char *multiline_coment_end;
  int flags;
};

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

// terminal
void disableRaw() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRaw() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRaw);

  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disableRaw);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

// highlight
int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

// syntax
char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {"switch",    "if",      "while",   "for",    "break",
                         "continue",  "return",  "else",    "struct", "union",
                         "typedef",   "static",  "enum",    "class",  "case",
                         "int|",      "long|",   "double|", "float|", "char|",
                         "unsigned|", "signed|", "void|",   NULL};

struct editorSyntax HLDB[] = {
    {"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

void updateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->size);
  memset(row->hl, HL_NORMAL, row->size);

  if (E.syntax == NULL)
    return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->singleline_coment_start;
  char *mcs = E.syntax->multiline_coment_start;
  char *mce = E.syntax->multiline_coment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mcs_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
          i += mcs_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        if (c == '\\' && i + 1 < row->size) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        row->hl[i] = HL_STRING;
        if (c == in_string)
          in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2)
          klen--;

        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    updateSyntax(&E.row[row->idx + 1]);
}

int syntaxColor(int hl) {
  switch (hl) {
  case HL_COMMENT:
  case HL_MLCOMMENT:
    return 36;
  case HL_KEYWORD1:
    return 33;
  case HL_KEYWORD2:
    return 32;
  case HL_STRING:
    return 35;
  case HL_NUMBER:
    return 31;
  case HL_MATCH:
    return 34;
  default:
    return 37;
  }
}

void selectSyntaxHighlight() {
  E.syntax = NULL;
  if (E.filename == NULL)
    return;

  char *ext = strrchr(E.filename, '.');

  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;

        int filerow;
        for (filerow = 0; filerow < E.numrows; filerow++) {
          updateSyntax(&E.row[filerow]);
        }

        return;
      }
      i++;
    }
  }
}

// row operations
int cxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

int rxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
    cur_rx++;

    if (cur_rx > rx)
      return cx;
  }
  return cx;
}

void updateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t')
      tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs * (TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  updateSyntax(row);
}

void insertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows)
    return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  for (int j = at + 1; j <= E.numrows; j++)
    E.row[j].idx++;

  E.row[at].idx = at;

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  updateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void freeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void eraseRow(int at) {
  if (at < 0 || at >= E.numrows)
    return;
  freeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) + (E.numrows - at - 1));
  for (int j = at + 1; j <= E.numrows; j++)
    E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

void rowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  updateRow(row);
  E.dirty++;
}

void rowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  updateRow(row);
  E.dirty++;
}

void rowEraseChar(erow *row, int at) {
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  updateRow(row);
  E.dirty++;
}

// operations
void insertChar(int c) {
  if (E.cy == E.numrows) {
    insertRow(E.cy, "", 0);
  }

  rowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void insertNewlineChar() {
  if (E.cx == 0) {
    insertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    insertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    updateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void eraseChar() {
  if (E.cy == E.numrows)
    return;
  if (E.cx == 0 && E.cy == 0)
    return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    rowEraseChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    rowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    eraseRow(E.cy);
    E.cy--;
  }
}

// file management
void openFile(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  selectSyntaxHighlight();

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    insertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);

  E.dirty = 0;
}

char *rowToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void saveFile() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (E.filename == NULL) {
      setStatusMessage("Save aboted");
      return;
    }
    selectSyntaxHighlight();
  }

  int len;
  char *buf = rowToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        setStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  setStatusMessage("can't save. Error: %s", strerror(errno));
}

void findCallback(char *query, int key) {
  static int last_match = -1;
  static int next_match = 1;

  static int hl_line_backup;
  static char *hl_backup = NULL;

  if (hl_backup != NULL && hl_line_backup >= 0 && hl_line_backup < E.numrows) {
    memcpy(E.row[hl_line_backup].hl, hl_backup, E.row[hl_line_backup].rsize);
    free(hl_backup);
    hl_backup = NULL;
  }

  if (key == '\r' || key == '\x1b') {
    last_match = 1;
    next_match = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    next_match = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    next_match = -1;
  } else {
    last_match = -1;
    next_match = 1;
  }

  if (last_match == -1)
    next_match = 1;
  int current = last_match;
  int i;
  for (i = 0; i < E.numrows; i++) {
    current += next_match;
    if (current == -1)
      current = E.numrows - 1;
    else if (current == E.numrows)
      current = 0;

    erow *row = &E.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = rxToCx(row, match - row->render);
      E.cx = match - row->render;
      E.rowoff = E.numrows;

      hl_line_backup = current;
      hl_backup = malloc(row->rsize);
      memcpy(hl_backup, row->hl, row->size);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void findText() {
  int cx_backup = E.cx;
  int cy_backup = E.cy;
  int coloff_backup = E.coloff;
  int rowoff_backkup = E.rowoff;

  char *query =
      editorPrompt("Search: %s (Use ESC, arrows or enter)", findCallback);

  if (query) {
    free(query);
  } else {
    E.cx = cx_backup;
    E.cy = cy_backup;
    E.coloff = coloff_backup;
    E.rowoff = rowoff_backkup;
  }
}

// buffer
struct abuf {
  char *b;
  int len;
};

void bufAppend(struct abuf *buf, const char *s, int len) {
  char *new = realloc(buf->b, buf->len + len);

  if (new == NULL)
    return;
  memcpy(&new[buf->len], s, len);
  buf->b = new;
  buf->len += len;
}

void bufFree(struct abuf *buf) { free(buf->b); }

// input
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    setStatusMessage(prompt, buf);
    refreshScreen();

    int c = readKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0)
        buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      setStatusMessage("");
      if (callback)
        callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        setStatusMessage("");
        if (callback)
          callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if (callback)
      callback(buf, c);
  }
}

int readKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }

    } else if (seq[0] == '0') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }

    return '\x1b';

  } else {
    return c;
  }
}

void moveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;
    } else if (E.cy > 0) {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
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
    if (E.cx != E.numrows - 1) {
      E.cy++;
    }
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProccessPress() {
  int c = readKey();

  switch (c) {
  case '\r':
    insertNewlineChar();
    break;

  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case CTRL_KEY('s'):
    saveFile();
    break;

  case CTRL_KEY('f'):
    findText();
    break;

  case HOME_KEY:
    E.cx = 0;
    break;

  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;

  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    if (c == DEL_KEY)
      moveCursor(ARROW_RIGHT);
    eraseChar();
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    if (c == PAGE_UP) {
      E.cy = E.rowoff;
    } else if (c == PAGE_DOWN) {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }
    int times = E.screenrows;
    while (times--)
      moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    moveCursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b':
    break;

  default:
    insertChar(c);
    break;
  }
}

// output
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = cxToRx(&E.row[E.cy], E.cx);
  }
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void drawRows(struct abuf *buf) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0) {
        int start_y = E.screenrows / 2;
        char *welcome[] = {"Yum Editor", "ver. " YUM_VER};
        int num_lines = sizeof(welcome) / sizeof(welcome[0]);

        if (y >= start_y && y < start_y + num_lines) {
          int i = y - start_y;
          int linelen = strlen(welcome[i]);
          int padding = (E.screencols - linelen) / 2;

          if (padding) {
            bufAppend(buf, "", 1);
            padding--;
          }
          while (padding--)
            bufAppend(buf, " ", 1);
          bufAppend(buf, welcome[i], linelen);
        } else {
          bufAppend(buf, "", 1);
        }
      } else {
        bufAppend(buf, "", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          bufAppend(buf, "\x1b[7m", 4);
          bufAppend(buf, &sym, 1);
          bufAppend(buf, "\x1b[m", 3);
          if (current_color != -1) {
            char colorBuf[16];
            int clen =
                snprintf(colorBuf, sizeof(colorBuf), "\x1b[%dm", current_color);
            bufAppend(buf, colorBuf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            bufAppend(buf, "\x1b[39m", 5);
            current_color = -1;
          }
          bufAppend(buf, &c[j], 1);
        } else {
          int color = syntaxColor(hl[j]);
          if (color != current_color) {
            current_color = color;
            char colorBuf[16];
            int clen = snprintf(colorBuf, sizeof(colorBuf), "\x1b[%dm", color);
            bufAppend(buf, colorBuf, clen);
          }
          bufAppend(buf, &c[j], 1);
        }
      }
      bufAppend(buf, "\x1b[39m", 5);
    }

    bufAppend(buf, "\x1b[K", 3);
    bufAppend(buf, "\r\n", 2);
  }
}

// output
void drawStatusBar(struct abuf *buf) {
  bufAppend(buf, "\x1b[7m", 4);
  char status[80], rstatus[80];

  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No name]", E.numrows,
                     E.dirty ? "(modified)" : "");

  int rlen =
      snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
               E.syntax ? E.syntax->filetype : "no fl", E.cy + 1, E.numrows);

  if (len > E.screencols)
    len = E.screencols;
  bufAppend(buf, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      bufAppend(buf, rstatus, rlen);
      len += len;
    } else {
      bufAppend(buf, " ", 1);
      len++;
    }
  }
  bufAppend(buf, "\x1b[m", 3);
  bufAppend(buf, "\r\n", 2);
}

void drawMessageBar(struct abuf *buf) {
  bufAppend(buf, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    bufAppend(buf, E.statusmsg, msglen);
}

void refreshScreen() {
  editorScroll();

  struct abuf buf = ABUF_INIT;

  bufAppend(&buf, "\x1b[?25l", 6);
  bufAppend(&buf, "\x1b[H", 3);

  drawRows(&buf);
  drawStatusBar(&buf);
  drawMessageBar(&buf);

  char bufCoord[32];
  snprintf(bufCoord, sizeof(bufCoord), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1);
  bufAppend(&buf, bufCoord, strlen(bufCoord));

  bufAppend(&buf, "\x1b[?25h", 6);

  write(STDOUT_FILENO, buf.b, buf.len);
  bufFree(&buf);
}

void setStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

// init
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRaw();
  initEditor();
  if (argc >= 2) {
    openFile(argv[1]);
  }

  setStatusMessage("HELP: CTRL-S = save | CTRL-Q = quit | CTRL-F = find");

  while (1) {
    refreshScreen();
    editorProccessPress();
  }
  return 0;
}
