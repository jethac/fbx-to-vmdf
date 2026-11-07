/****************************************************************************
//	fbx-to-vmdf
//	Command line, compatible with Ayataka's (Tools/Source/Ayataka/
//	Ayataka.cpp, Usagi Engine, Copyright (c) Vitei, Inc. 2013-2018, MIT
//	licence):
//
//	  fbx-to-vmdf -a16 -o<out.vmdf> -sk<anim dir> -lh -d<dep file>
//	              -h<skeleton.xml> <in.fbx>
//	  fbx-to-vmdf -o<out.cfx> -d<dep file> <effect.yml>
//
//	plus fbx-to-vmdf's own options (see usage()).
*****************************************************************************/
#include "Common.h"
#include "Compare.h"
#include "Exchange.h"
#include "Loader.h"
#include "MaterialDefinition.h"
#include "Options.h"
#include "Writer.h"

#include <chrono>
#include <clocale>

#ifndef FBX2VMDF_VERSION
#define FBX2VMDF_VERSION "0.1.0"
#endif

Options g_options;

namespace {

struct ConvertSettings
{
	std::string dependencyPath;
	std::string skeletonPath;
	std::string animPath;
	bool bDependencies = false;
	int alignment = (int)DEFAULT_ALIGNMENT;
	bool bBigEndian = false;
	bool bLeftHand = false;
};

void usage()
{
	printf("fbx-to-vmdf %s: FBX -> Usagi .vmdf model converter (Ayataka-compatible)\n", FBX2VMDF_VERSION);
	printf("usage\n");
	printf("==========\n");
	printf("-a<16|32|64|128>: alignment\n");
	printf("-o<path>: output path\n");
	printf("-d<path>: dependency path (if blank output file .d is used) \n");
	printf("-h<path>: bone hierarchy path (if blank output file .xml is used) \n");
	printf("-sk<path>: skeletal/material animation output directory\n");
	printf("-lh: swap X-axis for LH coordinate\n");
	printf("-be: swap endian (not supported)\n");
	printf("--instance: output as an instance (accepted, no effect, as in Ayataka)\n");
	printf("--collision: output as a collision mesh (not supported yet)\n");
	printf("--flip-uv: flip V\n");
	printf("--skeleton-only: write only the bone hierarchy\n");
	printf("\nfbx-to-vmdf options\n");
	printf("--compare <reference.vmdf>: compare the converted model with a reference, structurally\n");
	printf("--compare-files <a.vmdf> <b.vmdf>: compare two existing .vmdf files\n");
	printf("--dump <file.vmdf>: print the structure of a .vmdf\n");
	printf("--effects-dir <dir>: directory with <ShaderPak>.yml effects (default $USAGI_DIR/Data/GLSL/effects)\n");
	printf("--ayataka-compat=<bounds,uvindex>: reproduce Ayataka bugs fixed after the Windows reference cook\n");
	printf("--probe: print the per-corner FBX data (tools/sdkprobe format) instead of converting\n");
	printf("--quiet: don't print the conversion time\n");
	printf("--version\n");
}

bool checkArgument(std::string& target, const char* argument)
{
	if (startsWith(target, argument))
	{
		target.erase(0, strlen(argument));
		return true;
	}
	return false;
}

int convertModelData(const std::string& outputPath, const std::string& inputPath, const std::vector<std::string>& args, const ConvertSettings& settings,
                     const std::string& comparePath)
{
	bool bAsCollision = false, bFlipUV = false, bSkeletonOnly = false;
	for (size_t n = 1; n < args.size(); ++n)
	{
		std::string arg = args[n];
		if (checkArgument(arg, "--collision"))
			bAsCollision = true;
		else if (checkArgument(arg, "--flip-uv"))
			bFlipUV = true;
		else if (checkArgument(arg, "--skeleton-only"))
			bSkeletonOnly = true;
		// --sh-out-dir, --fx-out-dir, --instance: parsed and unused by Ayataka
	}
	if (bAsCollision)
	{
		fprintf(stderr, "fbx-to-vmdf: --collision models are not supported yet\n");
		return -1;
	}
	if (settings.bBigEndian)
	{
		fprintf(stderr, "fbx-to-vmdf: big-endian output (-be) is not supported\n");
		return -1;
	}
	if (settings.alignment <= 0 || (settings.alignment & (settings.alignment - 1)))
	{
		fprintf(stderr, "fbx-to-vmdf: alignment must be a power of two\n");
		return -1;
	}

	DependencyTracker dependencies;
	if (settings.bDependencies)
		dependencies.StartRecord(settings.dependencyPath, outputPath);

	Cmdl cmdl;
	LoadResult r = LoadFbx(inputPath, bAsCollision, bSkeletonOnly, cmdl, dependencies);
	if (!r.ok)
	{
		fprintf(stderr, "fbx-to-vmdf: couldn't load %s: %s\n", inputPath.c_str(), r.error.c_str());
		return -1;
	}
	if (r.skippedAnimation)
		warning("%s has animation curves; skeletal and material animation export (.vskla/.vmata) is not supported yet", inputPath.c_str());

	if (!bSkeletonOnly)
	{
		if (settings.bDependencies && !dependencies.ExportDependencies())
		{
			fprintf(stderr, "fbx-to-vmdf: couldn't write %s\n", settings.dependencyPath.c_str());
			return -1;
		}
		if (settings.bLeftHand)
			cmdl.ReverseCoordinate();
		if (bFlipUV)
			cmdl.FlipUV();
		if (cmdl.GetShapeNum() == 0)
		{
			fprintf(stderr, "fbx-to-vmdf: %s has no meshes\n", inputPath.c_str());
			return -1;
		}
		std::vector<uint8_t> image = StoreCmdl(cmdl, (size_t)settings.alignment);
		if (!writeFile(outputPath, image.data(), image.size()))
		{
			fprintf(stderr, "fbx-to-vmdf: couldn't write %s\n", outputPath.c_str());
			return -1;
		}
	}
	else if (settings.bLeftHand)
	{
		cmdl.ReverseCoordinate();
	}
	if (!ExportBoneHierarchy(cmdl, settings.skeletonPath))
	{
		fprintf(stderr, "fbx-to-vmdf: couldn't write %s\n", settings.skeletonPath.c_str());
		return -1;
	}
	if (!comparePath.empty() && !bSkeletonOnly)
	{
		std::vector<uint8_t> image, ref;
		if (!readFile(outputPath, image) || !readFile(comparePath, ref))
		{
			fprintf(stderr, "fbx-to-vmdf: couldn't read %s\n", comparePath.c_str());
			return -1;
		}
		CompareVerdict v = CompareVmdf(image, ref, stdout);
		if (v == CompareVerdict::Different || v == CompareVerdict::Unreadable)
			return 2;
	}
	return 0;
}

int convertMaterialDefinition(const std::string& outputPath, const std::string& inputPath, const ConvertSettings& settings)
{
	MaterialDefinition def;
	def.Load(inputPath, std::string());
	if (settings.bDependencies)
	{
		DependencyTracker deps;
		deps.StartRecord(settings.dependencyPath, outputPath);
		deps.ExportDependencies();
	}
	if (!def.ExportFile(outputPath))
	{
		fprintf(stderr, "fbx-to-vmdf: couldn't write %s\n", outputPath.c_str());
		return -1;
	}
	return 0;
}

} // namespace

int main(int argc, char* argv[])
{
	// Number formatting and parsing must not depend on the environment
	setlocale(LC_ALL, "C");

	std::vector<std::string> args;
	std::string comparePath, dumpPath, compareA, compareB;
	bool probe = false, verbose = false;
	for (int i = 0; i < argc; ++i)
	{
		std::string a = argv[i];
		auto value = [&](const char* name, std::string& out) -> bool {
			std::string prefix = std::string(name) + "=";
			if (a == name && i + 1 < argc)
			{
				out = argv[++i];
				return true;
			}
			if (startsWith(a, prefix.c_str()))
			{
				out = a.substr(prefix.size());
				return true;
			}
			return false;
		};
		std::string v;
		if (i > 0 && value("--compare", comparePath))
			continue;
		if (i > 0 && value("--dump", dumpPath))
			continue;
		if (i > 0 && value("--effects-dir", g_options.effectsDir))
			continue;
		if (i > 0 && a == "--compare-files" && i + 2 < argc)
		{
			compareA = argv[++i];
			compareB = argv[++i];
			continue;
		}
		if (i > 0 && value("--ayataka-compat", v))
		{
			for (const std::string& tok : { std::string("bounds"), std::string("uvindex") })
			{
				if (v.find(tok) != std::string::npos || v == "all")
				{
					if (tok == "bounds")
						g_options.legacyBounds = true;
					else
						g_options.legacyUVIndex = true;
				}
			}
			continue;
		}
		if (i > 0 && a == "--probe")
		{
			probe = true;
			continue;
		}
		if (i > 0 && a == "--quiet")
		{
			g_options.timing = false;
			continue;
		}
		if (i > 0 && a == "--verbose")
		{
			verbose = true;
			continue;
		}
		if (i > 0 && (a == "--version" || a == "-v"))
		{
			printf("fbx-to-vmdf %s\n", FBX2VMDF_VERSION);
			return 0;
		}
		if (i > 0 && a == "--help")
		{
			usage();
			return 0;
		}
		args.push_back(a);
	}

	if (!dumpPath.empty())
	{
		std::vector<uint8_t> d;
		if (!readFile(dumpPath, d))
		{
			fprintf(stderr, "fbx-to-vmdf: couldn't read %s\n", dumpPath.c_str());
			return 1;
		}
		return DumpVmdf(d, stdout, verbose) ? 0 : 1;
	}
	if (!compareA.empty())
	{
		std::vector<uint8_t> a, b;
		if (!readFile(compareA, a) || !readFile(compareB, b))
		{
			fprintf(stderr, "fbx-to-vmdf: couldn't read the files to compare\n");
			return 1;
		}
		CompareVerdict cv = CompareVmdf(a, b, stdout, verbose ? 100000 : 60);
		return cv == CompareVerdict::Different || cv == CompareVerdict::Unreadable ? 2 : 0;
	}

	if (args.size() < 2)
	{
		usage();
		return 0;
	}

	std::string outputPath, inputPath;
	ConvertSettings settings;
	for (size_t n = 1; n < args.size(); ++n)
	{
		std::string arg = args[n];
		if (arg.empty())
			continue;
		if (checkArgument(arg, "-o"))
			outputPath = arg;
		else if (arg[0] != '-')
			inputPath = arg;
		else if (checkArgument(arg, "-d"))
		{
			settings.bDependencies = true;
			settings.dependencyPath = arg;
		}
		else if (checkArgument(arg, "-h"))
			settings.skeletonPath = arg;
		else if (checkArgument(arg, "-be"))
			settings.bBigEndian = true;
		else if (checkArgument(arg, "-a"))
			settings.alignment = atoi(arg.c_str());
		else if (checkArgument(arg, "-lh"))
			settings.bLeftHand = true;
		else if (checkArgument(arg, "-sk"))
			settings.animPath = arg;
	}

	size_t found = inputPath.find_last_of('.');
	if (found == std::string::npos)
	{
		fprintf(stderr, "fbx-to-vmdf: no input file\n");
		return -1;
	}
	if (settings.dependencyPath.empty())
		settings.dependencyPath = outputPath + ".d";
	if (settings.skeletonPath.empty())
		settings.skeletonPath = outputPath + ".xml";

	std::string ext = inputPath.substr(found + 1);
	for (char& c : ext)
		c = (char)tolower((unsigned char)c);

	int ret = 0;
	if (probe)
	{
		SetProbeOutput(stdout);
		Cmdl cmdl;
		DependencyTracker deps;
		LoadResult r = LoadFbx(inputPath, false, false, cmdl, deps);
		return r.ok ? 0 : 1;
	}
	if (ext == "fbx")
	{
		auto start = std::chrono::steady_clock::now();
		ret = convertModelData(outputPath, inputPath, args, settings, comparePath);
		auto stop = std::chrono::steady_clock::now();
		if (g_options.timing)
			printf("%f\n", std::chrono::duration<double, std::milli>(stop - start).count());
	}
	else if (ext == "yml")
	{
		ret = convertMaterialDefinition(outputPath, inputPath, settings);
	}
	else
	{
		printf("That format is not supported!\n");
		ret = -1;
	}
	if (ret != 0)
		printf("ERROR!\n");
	return ret;
}
