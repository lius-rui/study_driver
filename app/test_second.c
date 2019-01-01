#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

int main()
{
	int fd;
	int counter = 0;
	int old_counter;

	fd = open("/dev/second",O_RDONLY);
	if(fd != -1)
	{
		while(1)
		{
			read(fd,&counter,sizeof(unsigned int));  //读取目前经历的秒数
			if(counter != old_counter)
			{
				printf("second after open /dev/second: %d \r\n",counter);
			}

		}

	}

}
