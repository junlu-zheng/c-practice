#include <stdio.h>

int main(void) {
	double height = 1.87;
	double weight =95.0;
	double bmi = weight / (height * height);

	printf("Haohao's BMI is %.2f\n", bmi);

	height = 1.58;
	weight =56;
	bmi = weight / (height * height);

	printf("Lulu's BMI is %.2f\n", bmi);


	return 0;
}
