#include "io_util.hpp"

#include <fstream>
#include <iostream>

bool loadFromFile(const std::string& filePath, std::vector<uint8_t>& data)
{
	try {
		std::ifstream file;
		file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		file.open(filePath, std::ifstream::in | std::ifstream::binary);
		data.assign(
			(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()
		);
		file.close();
		return true;
	}
	catch (const std::ios_base::failure& e) {
		std::cerr << e.what() << std::endl;
		return false;
	}
}

bool saveToFile(const std::string& filePath, const void* data, size_t dataSize)
{
	try {
		std::ofstream file;
		file.exceptions(std::ofstream::failbit | std::ofstream::badbit);
		file.open(filePath, std::ofstream::out | std::ofstream::binary);
		file.write(reinterpret_cast<const char*>(data), dataSize);
		file.close();
		return true;
	}
	catch (const std::ios_base::failure& e) {
		std::cerr << e.what() << std::endl;
		return false;
	}
}
