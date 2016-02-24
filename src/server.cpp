#include <fstream>
#include <asio.hpp>

int main(int argc, char** argv)
{
	std::ofstream ofs("test.txt");
	ofs << "Hello, World!" << std::endl;
	return 0;
}