#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** Data ***/
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;

/*** Terminal ***/
void die(const char* failedCall, int lineNum){ 
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen when unsuccessful call 

    char buf[80];
    snprintf(buf, sizeof(buf), "%s:%d\n", failedCall, lineNum);
    perror(buf);
    exit(1); 
}
void disableRawMode(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr",__LINE__);
}
void enableRawMode(){
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr",__LINE__); 
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 10;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr",__LINE__);
}
char editorReadKey(){
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) die("read",__LINE__);
    }
    return c;
}
int getWindowSize(int*rows, int*cols){
    struct winsize ws;
    
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** Input ***/
void editorProcessKeypress(){
    char c = editorReadKey();    

    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen on safe exit
            exit(0);
            break;
    }
}

/*** Output ***/
void editorDrawRows(){
    int y;    
    for (y=0; y<24; y++){
        write(STDOUT_FILENO, "=\r\n", 3);    
    }
}
void editorRefreshScreen(){
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen before processing key
    write(STDOUT_FILENO, "\x1b[H", 3);
    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[1;2H", 7);
}

/*** Init ***/
int main(){
    enableRawMode();

    while (1){
        editorRefreshScreen();
        editorProcessKeypress();    
    }
    return 0;
}
