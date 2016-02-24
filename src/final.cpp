#include <stdio.h>  /* printf */
#include <stdlib.h> /* system, NULL, EXIT_FAILURE */

int main(int argc, char **argv) {
  printf("Starting server...");
  int i = system("./final &");
  printf("The value returned was: %d.\n", i);
  return 0;
}