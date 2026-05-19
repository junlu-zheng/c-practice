#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
	int user_choice;
	int computer_choice;

	srand(time(NULL));

	printf("Rock Paper Scissor Game\n");
	printf("Choose one:\n");
	printf("1 = Rock\n");
	printf("2 = Paper\n");
	printf("3 = Scissors\n");
	printf("Your choice: ");

	scanf("%d", &user_choice);

	if (user_choice < 1 || user_choice >3) {
		printf("Invalid choice. Please choose 1, 2, or 3.\n");
		return 1;
	}

	computer_choice = rand() % 3 + 1;

	printf("You chose: ");

	if (user_choice == 1) {printf("Rock\n");}
	else if (user_choice == 2) {printf("Paper\n");}
	else {printf("Scissors\n");}

	printf("Computer chose: ");
	
	if (computer_choice == 1) {printf("Rock\n");}
	else if (computer_choice == 2) {printf("Paper\n");}
	else {printf("Scissors\n");}

	if (user_choice == computer_choice) {printf("It's a tie!\n");}
	else if (
		(user_choice == 1 && computer_choice == 3) ||
		(user_choice == 2 && computer_choice == 1) ||	
		(user_choice == 3 && computer_choice == 2) 	
	){
	printf("You win!\n");
	}
	else {printf("You lose!\n");}

	return 0;
}
