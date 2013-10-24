#include <stdio.h>
#include <stdlib.h>
 
typedef struct {
    int *list;
    short count; 
} Factors;
 
void xferFactors( Factors *fctrs, int *flist, int flix ) 
{
    int ix, ij;
    int newSize = fctrs->count + flix;
    if (newSize > flix)  {
        fctrs->list = realloc( fctrs->list, newSize * sizeof(int));
    }
    else {
        fctrs->list = malloc(  newSize * sizeof(int));
    }
    for (ij=0,ix=fctrs->count; ix<newSize; ij++,ix++) {
        fctrs->list[ix] = flist[ij];
    }
    fctrs->count = newSize;
}
 
Factors *factor( int num, Factors *fctrs)
{
    int flist[301], flix;
    int dvsr;
    flix = 0;
    fctrs->count = 0;
    free(fctrs->list);
    fctrs->list = NULL;
    for (dvsr=1; dvsr*dvsr < num; dvsr++) {
        if (num % dvsr != 0) continue;
        if ( flix == 300) {
            xferFactors( fctrs, flist, flix );
            flix = 0;
        }
        flist[flix++] = dvsr;
        flist[flix++] = num/dvsr;
    }
    if (dvsr*dvsr == num) 
        flist[flix++] = dvsr;
    if (flix > 0)
        xferFactors( fctrs, flist, flix );
 
    return fctrs;
}
 
int main(int argc, char*argv[])
{
    // Input: n.
    // Instead of computing factors for a few random numbers,
    // calculate the factors of a single number n.
    // Another idea (if the above is too fast): Calculate factors
    // of large number n-times.
    
    if (argc != 2) {
        printf("usage: %s number\n", argv[0]);
        return -1;
    }

    int n = atoi(argv[1]);

    // int nums2factor[] = { 2059, 223092870, 3135, 45 };
    // Factors ftors = { NULL, 0};
    char sep;
    int i,j;
 
    // for (i=0; i<4; i++) {
    for (i=0; i<100000; i++) {

        Factors ftors = { NULL, 0};

        factor( n, &ftors );
        // console output
        // printf("\nfactors of %d are:\n  ", nums2factor[i]);
        // sep = ' ';
        // for (j=0; j<ftors.count; j++) {
        //     printf("%c %d", sep, ftors.list[j]);
        //     sep = ',';
        // }
        // printf("\n");
        // 
        
        // This loop is a memory leak... ignore it!
    }
    return 0;
}
