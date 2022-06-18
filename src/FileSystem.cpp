#include "FileSystem.h"

#include <ShlObj_core.h>
#include <commdlg.h>

void GetPaths(IShellItemArray* shellItems, std::vector<Path>& outPaths)
{
	DWORD numItems;
	shellItems->GetCount(&numItems);
	for (DWORD i = 0; i < numItems; i++)
	{
		IShellItem* shellItem = nullptr;
		shellItems->GetItemAt(i, &shellItem);
		SFGAOF attribs;
		shellItem->GetAttributes(SFGAO_FILESYSTEM, &attribs);

		if (!(attribs & SFGAO_FILESYSTEM))
			continue;
		LPWSTR name;
		shellItem->GetDisplayName(SIGDN_FILESYSPATH, &name);
		outPaths.push_back(Path(name));
		CoTaskMemFree(name);
	}
}

bool FileSystem::OpenFileDialog(std::vector<Path>& outPaths)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	IFileDialog* fileDialog = nullptr;
	IID classId = CLSID_FileOpenDialog;
	CoCreateInstance(classId, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&fileDialog));

	bool finalResult = false;
	if (SUCCEEDED(fileDialog->Show(nullptr)))
	{
		IShellItem* shellItem = nullptr;
		fileDialog->GetResult(&shellItem);
		LPWSTR filePath = nullptr;
		shellItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
		outPaths.push_back(Path(filePath));
		CoTaskMemFree(filePath);
		shellItem->Release();
		finalResult = true;
	}

	CoUninitialize();
	return finalResult;
}