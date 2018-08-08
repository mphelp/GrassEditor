#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** Data ***/
struct termios orig_termios;

/*** Terminal ***/
void die(const char* call, const char* funcBlock){
    char buf[50];
    snprintf(buf, sizeof(buf), "Call: %s\tFunc Block: %s\n", call, funcBlock);
    perror(buf);
    exit(1);
}
void disableRawMode(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr",__func__);
}

void enableRawMode(){
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die ("tcgetattr",__func__);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 10;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr",__func__); 
}

char editorReadKey(){
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) die("read",__func__);
    }
    return c;
}

void editorRefreshScreen(){
    write(STDOUT_FILENO, "\x1b[2J", 4);    
    write(STDOUT,FILENO, "\x1b[H", 4);
}

/*** Input ***/
void editorProcessKeypress(){
    char c = editorReadKey();    

    switch(c){
        case CTRL_KEY('q'):
            exit(0);
            break;
    }
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
