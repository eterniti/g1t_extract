#include "DOA6/G1tFile.h"
#include "debug.h"

static std::string format_to_extension(int dds_format)
{
	switch (dds_format)
	{
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			return ".rgba.dds";
			
		case DXGI_FORMAT_BC1_UNORM:
			return ".dxt1.dds";
			
		case DXGI_FORMAT_BC2_UNORM:
			return ".dxt3.dds";
			
		case DXGI_FORMAT_BC3_UNORM:
			return ".dxt5.dds";
			
		case DXGI_FORMAT_BC4_UNORM:
			return ".bc4.dds";
			
		case DXGI_FORMAT_BC6H_UF16:
			return ".bc6h.dds";
	}
	
	std::string name = DdsFile::GetFormatName(dds_format);
	size_t pos = name.rfind("_UNORM");
	if (pos != std::string::npos && pos != 0)
	{
		name = name.substr(0, pos);
		return "." + Utils::ToLowerCase(name) + ".dds";
	}
	
	return ".dds";
}

static int extract_textures(const std::vector<G1tTexture> &textures, const std::string &path, const std::string &prefix)
{
	std::string extract_dir = Utils::GetDirNameString(path);
	std::string fn_base = Utils::GetFileNameString(path);
	
	fn_base = fn_base.substr(0, fn_base.length()-4);	
	
	if (textures.size() > 1)
	{
		extract_dir = Utils::MakePathString(extract_dir, fn_base);
		Utils::CreatePath(extract_dir, true);
	}
	
	for (size_t i = 0; i < textures.size(); i++)
	{
		DdsFile *dds = G1tFile::ToDDS(textures[i]);
		if (!dds)
		{
			DPRINTF("Failed to convert to dds (index = %Id).\n", i);
			return -1;
		}
		
		std::string extension = format_to_extension(dds->GetFormat());
		
		std::string out = (textures.size() == 1) ? Utils::MakePathString(extract_dir, fn_base + extension) : Utils::MakePathString(extract_dir, prefix + Utils::ToString(i) + extension);
		bool ret = dds->SaveToFile(out);		
		delete dds;
		
		if (!ret)
		{
			DPRINTF("Failed to save dds file (index = %Id).\n", i);
			return -1;
		}
	}
	
	if (textures.size() == 1)
		UPRINTF("Texture sucessfully extracted!\n");
	else
		UPRINTF("The %Id textures have been succesfully extracted.\n", textures.size());
	
	return 0;
}

int g1t_extract(const std::string &path)
{
	G1tFile g1t;
	std::string extract_dir;
	
	if (!g1t.LoadFromFile(path))
		return -1;
	
	bool array = (g1t.GetNumTextures() == 1 && g1t.IsArrayTexture(0));
	
	if (array)
	{
		std::vector<G1tTexture> textures;
		
		if (!g1t.DecomposeArrayTexture(0, textures, false, true))
			return -1;
		
		return extract_textures(textures, path, "Arr ");
	}
	else
	{
		return extract_textures(g1t.GetTextures(), path, "Tex ");
	}
	
	return -1;
}

static bool g1t_visitor(const std::string &path, bool, void *)
{
	if (Utils::EndsWith(path, ".g1t", false))
	{
		DPRINTF("Extracting %s\n", Utils::GetFileNameString(path).c_str());
		g1t_extract(path);
	}
	
	return true;
}

int mass_extract(const std::string &path)
{
	bool ret = Utils::VisitDirectory(path, true, false, false, g1t_visitor);
	if (ret)
		return 0;
	
	return -1;
}

static int inject_textures(std::vector<G1tTexture> &textures, const std::string &path, bool array)
{
	uint8_t array_format = 0; // g1t
	uint32_t array_width = 0, array_height = 0;
	
	bool multiple = (textures.size() > 1);
	size_t num = 0;
	
	for (size_t i = 0; i < textures.size(); i++)
	{
		G1tTexture &tex = textures[i];
		std::string fpath;		
		
		if (multiple)
		{
			WIN32_FIND_DATAA data;
			
			std::string pattern = (array) ? "Arr " : "Tex ";
			pattern += Utils::ToString(i) + "*.dds";
			
			HANDLE h = FindFirstFileA(Utils::MakePathString(path, pattern).c_str(), &data);
			if (h == INVALID_HANDLE_VALUE)
				continue;
			
			fpath = Utils::MakePathString(path, data.cFileName);
			
			BOOL ret = FindNextFileA(h, &data);
			FindClose(h);
			
			if (ret)
			{
				DPRINTF("Error: there are multiple \"%s\" files.\n", pattern.c_str());
				return -1;
			}
		}
		else
		{
			fpath = path;
		}
		
		DdsFile dds;
		
		if (!dds.LoadFromFile(fpath))
		{
			DPRINTF("Failed to load dds file \"%s\"", fpath.c_str());
			return -1;
		}
		
		uint8_t fmt, prev_fmt;
		uint8_t previous_mips = tex.mips;
		
		if (!G1tFile::FromDDS(tex, dds, &fmt, &prev_fmt))
		{
			DPRINTF("Failed to replace texture %Id (input file %s).\n", i, fpath.c_str());
			return -1;
		}
		
		if (array)
		{
			if (num == 0)
			{
				array_format = tex.format;
				array_width = tex.width;
				array_height = tex.height;
			}
			else
			{
				if (tex.format != array_format)
				{
					DPRINTF("Error: in an array textures, all images formats must match. (Mismatch: %s != %s).\n", 
							DdsFile::GetFormatName(G1tFile::G1tToDdsFormat(tex.format)).c_str(), 
							DdsFile::GetFormatName(G1tFile::G1tToDdsFormat(array_format)).c_str());
							
					return -1;
				}
				
				if (tex.width != array_width || tex.height != array_height)
				{
					DPRINTF("Error: in an array textures, all images dimensions must match (Mismatch: %ux%u != %ux%u).\n", tex.width, tex.height, array_width, array_height);
					return -1;
				}
			}
		}
		else
		{
			if (tex.mips > previous_mips)
			{
				if (!G1tFile::ReduceMipsLevel(tex, previous_mips))
				{
					DPRINTF("Internal error while reducing mips levels.\n");
					return false;
				}
			}
		}
		
		if (multiple)
			DPRINTF("Texture %Id succesfully replaced.\n", i);
		else
			DPRINTF("Texture succesfully replaced.\n");
		
		if (fmt != prev_fmt)
		{
			DPRINTF("^Notice: the format of the texture was changed from %s to %s.\n", 
			DdsFile::GetFormatName(G1tFile::G1tToDdsFormat(prev_fmt)).c_str(),
			DdsFile::GetFormatName(G1tFile::G1tToDdsFormat(fmt)).c_str());
		}
		
		num++;
	}
	
	if (multiple && num == 0)
	{
		DPRINTF("***No files were replaced because the directory didn't contain textures or weren't named as expected.\n");
		return -1;
	}
	
	if (array)
	{		
		uint8_t lower_mips = textures.front().mips;
		bool patch_needed = false;
		
		for (size_t i = 1; i < textures.size(); i++)
		{
			const G1tTexture &tex = textures[i];
			
			if (tex.mips != lower_mips)
			{
				patch_needed = true;
				
				if (tex.mips < lower_mips)
					lower_mips = tex.mips;
			}
		}
		
		if (patch_needed)
		{
			for (G1tTexture &tex: textures)
			{
				if (tex.mips != lower_mips && !G1tFile::ReduceMipsLevel(tex, lower_mips))
				{
					DPRINTF("Internal error while reducing mips levels to make array images match.\n");
					return -1;
				}					
			}
		}
	}
	
	return 0;
}

int g1t_inject(const std::string &path)
{
	G1tFile g1t;
	std::string g1t_path;
	bool expecting_multiple = false;
	
	if (Utils::DirExists(path))
	{
		g1t_path = Utils::NormalizePath(path);
		
		while (g1t_path.length() > 0 && g1t_path.back() == '/')
			g1t_path.pop_back();
		
		g1t_path += ".g1t";
		expecting_multiple = true;
	}
	else if (Utils::FileExists(path))
	{
		std::vector<std::string> comps;
		std::string dir = Utils::GetDirNameString(path);
		std::string fn = Utils::GetFileNameString(path);
		
		if (dir == path)
			dir = "./";
		
		Utils::GetMultipleStrings(fn, comps, '.');
		g1t_path = Utils::MakePathString(dir, comps[0] + ".g1t");
	}
	else
	{
		DPRINTF("Error: \"%s\" doesn't exist.\n", path.c_str());
		return -1;
	}
	
	if (!Utils::FileExists(g1t_path))
	{
		DPRINTF("Error: I was expecting g1t file \"%s\" to exist.\n", g1t_path.c_str());
		return -1;
	}
	
	if (!g1t.LoadFromFile(g1t_path))
	{
		DPRINTF("Load of \"%s\" failed.\n", g1t_path.c_str());
		return -1;
	}
	
	bool array = (g1t.GetNumTextures() == 1 && g1t.IsArrayTexture(0));
	
	if ((array || g1t.GetNumTextures() > 1) && !expecting_multiple)
	{
		DPRINTF("Error: Multi-texture .g1t, I was expecting a directory as parameter, not a file.\n");
		return -1;
	}
	
	int ret;
	
	if (array)
	{
		std::vector<G1tTexture> textures;
		
		if (!g1t.DecomposeArrayTexture(0, textures, false, true))
			return -1;
		
		ret = inject_textures(textures, path, array);
		if (ret >= 0)
		{
			if (!g1t.ComposeArrayTexture(0, textures, true))
				ret = -1;
		}
	}
	else
	{
		ret = inject_textures(g1t.GetTextures(), path, array);
	}
	
	if (ret < 0)
		return ret;
	
	if (!g1t.SaveToFile(g1t_path))
	{
		DPRINTF("Error: updating file failed.\n");
		return -1;
	}
	
	UPRINTF("File updated succesfully.\n");	
	return 0;
}

int main(int argc, char *argv[])
{
    bool bad_usage = false;
	std::string file;
	
	if (argc == 2)
	{
		file = argv[1];		
	}
	else
	{
		bad_usage = true;
	}
	
	if (bad_usage)
    {
        DPRINTF("Bad usage. Usage: %s file|dir\n", argv[0]);
		UPRINTF("Press enter to exit.");
		getchar();
        return -1;
    }
	
	int ret = -1;
	
#ifdef G1T_REPLACE

	if (Utils::DirExists(file))
	{
		ret = g1t_inject(file);
	}
	else if (Utils::EndsWith(file, ".dds", false))
	{
		ret = g1t_inject(file);
	}
	else
	{
		DPRINTF("Bad usage: I was expecting a .dds file or a directory as input.\n");
	}

#else

	if (Utils::DirExists(file))
	{
		mass_extract(file);
	}
	else if (Utils::EndsWith(file, ".g1t", false))
	{
		ret = g1t_extract(file);
	}
	else
	{
		DPRINTF("Bad usage: I was expecting a .g1t file or a directory as input.\n");
	}	

#endif	

	fseek(stdin, 0, SEEK_END);
	UPRINTF("Press enter to exit.");
    getchar();

    return ret;
}
