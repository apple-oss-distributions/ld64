#include <CoreFoundation/CFString.h>

extern void bar();

void foo()
{
	CFStringGetLength(CFSTR("hello"));
	CFStringGetLength(CFSTR("world"));
}


int main() 
{
	CFStringGetLength(CFSTR("live"));
	bar();
	return 0; 
}


