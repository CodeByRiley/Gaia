#include <cstdio>

struct Task {
	Task() {
		std::puts("Hello Gaia from C++ GCC 17");
	}
	~Task() {
		std::puts("Goodbye Gaia!");
	}
};

int main() {
	std::puts("This is a compiler test!");
	Task task;
	return 0;
}
