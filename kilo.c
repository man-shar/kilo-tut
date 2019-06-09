#include <unistd.h>

#define QUIT_KEY 'q'

int main() {
  char c;
  while (read(STDIN_FILENO, &c, 1) == 1 && c != QUIT_KEY);
  
  return 0;
}
