// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
static void consputc(int);

static int panicked = 0;

char last_commands[15][30];        
int commands_size[15] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};         
int filled_commands[15] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};          
int command_pointer = 0;                                // pointer of where we should add commands

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);
  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    if(pos > 0) --pos;
  } else
    crt[pos++] = (c&0xff) | 0x0700;  // black on white

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");

  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);
  cgaputc(c);
}



#define INPUT_BUF 128
struct {
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} input;

#define C(x)  ((x)-'@')  // Control-x



// added functions by Alireza :
void delete_numbers_in_current_line(){
  char temp_buff[100];
  int pointer = 0;
  for(int i = input.w ; i < input.e ; i++){
    char temp = input.buf[i];
    if(!(temp >= 48 && temp <= 57)){
      temp_buff[pointer] = temp;
      pointer++;
    }
    input.buf[i] = 0;
    consputc(BACKSPACE);
  }
  input.e = input.w;
  for(int i = 0 ; i < pointer ; i++){
    input.buf[input.e] = temp_buff[i];
    consputc(temp_buff[i]);
    input.e++;
  }
}


void reverse_row(){
  char temp_buff[100];
  int counter = 0;
  for(int i = input.w ; i < input.e ; i++){
    char temp = input.buf[i];
    temp_buff[counter] = temp;
    counter++;
    input.buf[i] = 0;
    consputc(BACKSPACE);
  }

  input.e = input.w;
  for(int i = counter - 1 ; i >= 0 ; i--){
    input.buf[input.e] = temp_buff[i];
    consputc(temp_buff[i]);
    input.e++;
  }
}

void save_command(char buffer[] , int buffer_size){
  int counter = 0;
  for(int i = input.w ; i < input.e ; i++){
    last_commands[command_pointer %15][counter] = buffer[i];
    counter++;
  }
  commands_size[command_pointer % 15] = buffer_size - 1;
  command_pointer += 1;
}

void clear_buff(int buff_size){
  memset(input.buf , 0 , buff_size);
  input.e = 0;
  input.w = 0;
  input.r = 0;
}

void recommend_command(){
  char current_buff[100];
  int counter = 0;
  for(int i = input.w ; i < input.e ; i++){
    current_buff[counter] = input.buf[i];
    input.buf[i] = 0;
    consputc(BACKSPACE);
    counter++;
  }
  int current_buff_size = input.e - input.w;
  input.e = input.w;
  int recommended_successfully = 0;
  int i; int j;
  for(i = (command_pointer % 15) - 1; i >= 0 ; i--){
    if(current_buff_size >= commands_size[i]){
      break;
    }
    for(j = 0 ; j < current_buff_size ; j++){
      if(current_buff[j] == last_commands[i][j]){
        if(j == current_buff_size - 1){
          recommended_successfully = 1;
          break;
        }
      }
      else
        break;
    }
    if(recommended_successfully)
      break;
  }
  if(command_pointer >= 15 && recommended_successfully == 0){
    for(i = 14; i >= (command_pointer % 15) ; i--){
    if(current_buff_size >= commands_size[i]){
      break;
    }
    for(j = 0 ; j < current_buff_size ; j++){
      if(current_buff[j] == last_commands[i][j]){
        if(j == current_buff_size - 1){
          recommended_successfully = 1;
          break;
        }
      }
      else
        break;
    }
    if(recommended_successfully)
      break;
  }
  }
  if(recommended_successfully){
    for(int x = 0 ; x < commands_size[i] ; x++){
      input.buf[input.e] = last_commands[i][x];
      consputc(last_commands[i][x]);
      input.e++;
    }
  }
  else{
    for(int x = 0 ; x < current_buff_size ; x++){
      input.buf[input.e] = current_buff[x];
      consputc(current_buff[x]);
      input.e++;
    }
  }
}


// end added functions


void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('N'):
      delete_numbers_in_current_line();
      break;
    case C('R'):
      reverse_row();
      break;
    case '\t': // case '\x09':
      recommend_command();
      break;
    case C('P'):  // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'):  // Kill line.
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    case C('H'): case '\x7f':  // Backspace
      if(input.e != input.w){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;
        consputc(c);
        if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
          int buff_size1 = input.e - input.w;
          save_command(input.buf , buff_size1);
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();  // now call procdump() wo. cons.lock held
  }
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while(n > 0){
    while(input.r == input.w){
      if(myproc()->killed){
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}

