#pragma once

#include <filesystem>
#include <vector>

using Path = std::filesystem::path;

class FileSystem
{
public:
	static bool OpenFileDialog(std::vector<Path>& outPaths);
};