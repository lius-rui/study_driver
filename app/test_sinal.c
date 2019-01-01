#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#define MAX_LEN  15
void input_handler(int num)
{

	char data[MAX_LEN];
	int len;

	/*读取并输出STDIN_FILENO上的输入*/
	len = read(STDIN_FILENO,&data,MAX_LEN);
	data[len] = 0;
	printf("input value = %s \r\n",data);

}

int main(int argc , char *argv[])
{

	int oflags;

	signal(SIGIO,input_handler);  //注册IO信号
	fcntl(STDIN_FILENO,F_SETOWN,getpid());
	oflags = fcntl(STDIN_FILENO,F_GETFL);
	fcntl(STDIN_FILENO,F_SETFL,oflags | FASYNC);

	while(1);

}
