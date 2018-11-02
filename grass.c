/*** Includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>

/*** Defines ***/
#define GRASS_VERSION "0.0.1"

#define GRASS_TAB_STOP 4
#define CTRL_KEY(k) ((k) & 0x1f)
#define die(str) write(STDOUT_FILENO,"\x1b[2J",4);write(STDOUT_FILENO,"\x1b[H",3);char _buf[80]; \
	snprintf(_buf,sizeof(_buf),"Call %s failed in...%s():%d\r\n",str,__func__,__LINE__);perror(_buf);printf("\r");exit(1)
#define ABUF_INIT {NULL, 0}

enum editorKey {
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

/*** Data ***/
typedef struct erow {
	int size;
	int rsize;
	char* chars;
	char* render;
} erow;
struct editorConfig {
	int cx, cy; // cursor position on screen (chars field)
	int rx; // render field
	int rowoff; // current line index of file (represents top line of screen)
	int coloff; // horizontal scroll index
	int screenrows; // screen height
	int screencols; // screen width
	int numrows; // num rows in file open
	erow* row; // array of text rows
	struct termios orig_termios; // terminal attributes struct
};
struct editorConfig E;

/*** Terminal ***/
void errmess(const char* failedCall, int lineNum){ 
	write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen when unsuccessful call 

	char buf[80];
	snprintf(buf, sizeof(buf), "%s():%d\n", failedCall, lineNum);
	perror(buf);
	exit(1); 
}
void disableRawMode(){
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {die("tcsetattr");}
}
void enableRawMode(){
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {die("tcgetattr");}
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 10;

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {die("tcsetattr");}
}
int editorReadKey(){
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
		if (nread == -1 && errno != EAGAIN) {die("read");}
	}

	// escape seq
	if (c == '\x1b'){
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '['){
			if (seq[1] >= '0' && seq[1] <= '9'){
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~'){
					switch (seq[1]){
						// -- <esc>[#~
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			} else {
				switch (seq[1]){
					// -- <esc>[letter
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == 'O'){
			switch (seq[1]){
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		return '\x1b';
	} else {
		return c;
	}
}
int getCursorPosition(int* rows, int* cols){
	char buf[32];
	unsigned int i = 0;
	// func unfinished
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	while (i < sizeof(buf)-1){
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';
	
	if (buf[i] != '\x1b' || buf[1] != '[') return -1;
	if ((sscanf(&buf[2], "%d;%d", rows, cols) != 2)) return -1;

	return 0;
}
int getWindowSize(int*rows, int*cols){
	struct winsize ws;
	
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** Row Operations ***/
void editorUpdateRow(erow* row){
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++){
		if (row->chars[j] == '\t') tabs++;
	}

	free(row->render);
	row->render = malloc(row->size + tabs*(GRASS_TAB_STOP - 1) + 1);

	int idx = 0;
	for (j = 0; j < row->size; j++){
		if (row->chars[j] == '\t'){
			row->render[idx++] = ' ';
			while (idx % GRASS_TAB_STOP != 0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}
void editorAppendRow(char* s, size_t len){
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
}

/*** File I/O ***/
void editorOpen(char* filename){
	FILE* fp = fopen(filename, "r");
	if (!fp) {die("fopen");}
	
	char* line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1){
		while (linelen > 0 && (line[linelen - 1] == '\r' || line[linelen - 1] == '\n'))
			linelen--;
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
}

/*** Append Buffer ***/
struct abuf{
	char* b;
	int len;
};
void abAppend(struct abuf* ab, const char* s, int len){
	char* new = realloc(ab->b, ab->len + len);
	
	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}
void abFree(struct abuf* ab){
	free(ab->b);
}

/*** Output ***/
void editorScroll(){
	E.rx = E.cx;
	if (E.cy < E.rowoff) E.rowoff = E.cy;
	if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
	if (E.rx < E.coloff) E.coloff = E.rx;
	if (E.rx >= E.coloff + E.screencols) E.coloff = E.rx - E.screencols + 1;
}
void editorDrawWelcomeRow(struct abuf* ab){
	// Add Welcome message to abuf
	char welcome[80];
	int welcomelen = snprintf(welcome, sizeof(welcome),"Grass Editor -- version %s", GRASS_VERSION);
	if (welcomelen > E.screencols) welcomelen = E.screencols;
	int padding = (E.screencols - welcomelen) / 2;
	if (padding){
		abAppend(ab, "@" , 1);
		padding--;
	}
	while (padding--) abAppend(ab, " ", 1);
	abAppend(ab, welcome, welcomelen);
}
void editorDebugDisplayCursorPos(struct abuf* ab){
	/* Display cursor location: (debugging purposes) */
	char location[80]; int locationlen = snprintf(location, sizeof(location), "\t\tcx:%d;cy:%d", E.cx, E.cy);
	abAppend(ab, location, locationlen);
}
void editorDrawRows(struct abuf* ab){
	// More like "add" to buffer than "draw"
	int y, filerow;
	for (y=0; y < E.screenrows; y++){
		// For each line:
		filerow = y + E.rowoff;
		// Special Rows
		if (filerow >= E.numrows){
			if (E.numrows == 0 && y == E.screenrows / 3){
				editorDrawWelcomeRow(ab);
			} else {
				abAppend(ab, "@", 1);
			}
		} else {
			// Normal Text Rows
			// TEMPORARY maybe
			// add line number to left side
			/*char lineNum[4];
			snprintf(lineNum, sizeof(lineNum), "%*d", 4, filerow);
			abAppend(ab, lineNum, 4);*/

			// Display each line of text buffer
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}

		abAppend(ab, "\x1b[K", 3); // clear line after cursor
		if (y < E.screenrows - 1){
			abAppend(ab, "\r\n", 2);
		} else {
			//editorDebugDisplayCursorPos(ab);
		}
	}
}
void editorRefreshScreen(){
	editorScroll(); // adjust row offset based on cursor position

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len); // write out abuf buffer (contains setting cursor pos and entire file contents erow)
	abFree(&ab);
}

/*** Input ***/
void editorMoveCursor(int key){
	erow* row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	switch(key){
		case ARROW_LEFT:
			if (E.cx != 0) 
				E.cx--; 
			else if (E.cy > 0){
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) 
				E.cy--; 
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size) 
				E.cx++; 
			else if (row && E.cx == row->size){
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows) 
				E.cy++; 
			break; // cursor can extend below so long as EOF not reached
	}
	// Snap cursor to end of line if needed
	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}
void editorProcessKeypress(){
	int c = editorReadKey();	

	switch(c){
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen on safe exit
			exit(0);
			break;
		case HOME_KEY:
			E.cx = 0; 
			break;
		case END_KEY:
			E.cx = E.screencols - 1; 
			break;
		case DEL_KEY:
			// do nothing currently
			break;
		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = E.screenrows;
				while (times--) editorMoveCursor(c == PAGE_UP? ARROW_UP:ARROW_DOWN);
			}
			break;
		case ARROW_UP:
		case ARROW_LEFT:
		case ARROW_DOWN:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

/*** Init ***/
void initEditor(){
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) {die("getWindowSize");}
}
int main(int argc, char* argv[]){
	enableRawMode();
	initEditor();
	if (argc >= 2){
		editorOpen(argv[1]);
	}

	while (1){
		editorRefreshScreen();
		editorProcessKeypress();	
	}
	return 0;
}
