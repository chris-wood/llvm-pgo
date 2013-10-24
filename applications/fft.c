#include <stdio.h>
#include <math.h>
#include <complex.h>
#include <stdlib.h>
#include <string.h>
 
double PI;
typedef double complex cplx;
 
void _fft(cplx buf[], cplx out[], int n, int step)
{
	if (step < n) {
		_fft(out, buf, n, step * 2);
		_fft(out + step, buf + step, n, step * 2);
 
		for (int i = 0; i < n; i += 2 * step) {
			cplx t = cexp(-I * PI * i / n) * out[i + step];
			buf[i / 2]     = out[i] + t;
			buf[(i + n)/2] = out[i] - t;
		}
	}
}
 
void fft(cplx buf[], int n)
{
	cplx out[n];
	for (int i = 0; i < n; i++) out[i] = buf[i];
 
	_fft(buf, out, n, 1);
}

// Have never seen a nested function C before ...
void show(const char * s, cplx buf[], int n) {
	printf("%s", s);
	for (int i = 0; i < n; i++)
		if (!cimag(buf[i]))
			printf("%g ", creal(buf[i]));
		else
			printf("(%g, %g) ", creal(buf[i]), cimag(buf[i]));
}
 
int main(int argc, char* argv[])
{
	// Input x.
	// Repeat the dummy input of the original version x times.

	if (argc != 2) {
    printf("usage: %s x-repeat\n", argv[0]);
    return -1;
  }

  int x = atoi(argv[1]);

  if ((x & (x-1)) != 0) {
  	printf("input must be power of 2\n");
  	return -2;
  }

	PI = atan2(1, 1) * 4;
	// cplx buf[] = {1, 1, 1, 1, 0, 0, 0, 0};
	cplx template[] = {1, 1, 1, 1, 0, 0, 0, 0};

	int n = x * 8;
	cplx buf[n];

	for (int i = 0; i < x; i++) {
		memcpy(buf + i * 8, template, 8 * sizeof(cplx)); // dest, src, count
	}

	// show("Data: ", buf, n); // console output
	fft(buf, n);
	// show("\nFFT : ", buf, n); // console output
 
 	// printf("\n"); // console output
	return 0;
}
