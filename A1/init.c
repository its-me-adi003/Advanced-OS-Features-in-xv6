// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;
  int num_of_attempts = 0;
  char username[1000];
  char password[1000];

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  
  while (num_of_attempts < 3) {
    printf(1, "Enter username: ");
    gets(username, sizeof(username));
    username[strlen(username) - 1] = '\0'; // Remove newline character

    if (strcmp(username, USERNAME) != 0) {
      printf(1, "username not valid.\n");
      num_of_attempts++;
      continue;
    }

    printf(1, "Enter password: ");
    gets(password, sizeof(password));
    password[strlen(password) - 1] = '\0'; // Remove newline character

    if (strcmp(password, PASSWORD) == 0) {
      printf(1, "Login successful\n");
      break;
    } else {
      printf(1, "password not valid.\n");
      num_of_attempts++;
    }
    if(num_of_attempts == 3) break;
  }

  if (num_of_attempts == 3) {
    printf(1, "Too many failed attempts. Authentication failed.\n");
    while(1);
    exit();
  }

  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
