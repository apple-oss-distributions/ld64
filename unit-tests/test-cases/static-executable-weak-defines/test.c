


__attribute__((weak)) int foo()
{
	return 0;
}


int entry()
{
	return foo();
}
