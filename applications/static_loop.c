#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
	if (argc !=2) {
		printf("usage: %s count\n", argv[0]);
		return -1;
	}

	// count is NOT used!
	int count = atoi(argv[1]);

	for(int i=0; i < 10; i++) {
		printf("%d\n", i);
	}

	return 0;
}