#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
	int secret, guess;
	int tries = 0;

	srand(time(NULL));
	secret = rand() % 100 + 1;

	printf("Guess an integer between 1 and 100:\n");

	while (1) {
		printf("Your guess: ");
		scanf("%d", &guess);
		tries++;

		if (guess < secret) {printf("Too low!\n");}
		else if (guess > secret) {printf("Too high!\n");}
		else {printf("Correct! You used %d tries.\n", tries); break;}
	}

	return 0;

}
