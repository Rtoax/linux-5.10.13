#include <errno.h>
#include <stdio.h>

int f1(int a)
{
	return errno;
}

int f2(int a)
{
	return errno;
}

#if defined(HAS_MAIN)
int main()
{
	return 0;
}
#endif
