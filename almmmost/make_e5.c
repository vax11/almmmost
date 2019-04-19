#include <string.h>
#include <stdio.h>
#include <unistd.h>

int main() {

	char buffer[512];

	memset(buffer, 0xe5, sizeof(buffer));

	while (1) {
		write(1, buffer, sizeof(buffer));
	}
	return 0;
}
